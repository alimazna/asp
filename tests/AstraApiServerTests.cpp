// TST-DEP-0002 - Public API server transport: auth, CORS, idempotent
// ingestion, durability, sync reconciliation.
//
// Spawns the real `astra_server` executable (built by the same build graph)
// against a temp data dir and exercises the served surface end to end:
//   * unauthenticated ingestion/sync are rejected 401;
//   * the same candle sent twice is stored once (already_stored on replay);
//   * accepted candles are queryable through /api/v1/candles;
//   * the sync endpoint returns the authoritative watermark;
//   * the store survives a server restart (durable persistence);
//   * malformed bodies are rejected 400 and disallowed CORS origins 403.

#include "TestHarness.h"

#include "api/BackendApiSchema.h"
#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

namespace {

std::uint16_t freePort() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 8797;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8797;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return 8797;
    }
    std::uint16_t port = 0;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    return port;
}

std::string binaryPath() {
    fs::path base = fs::current_path();
#ifdef _WIN32
    return (base / "astra_server.exe").string();
#else
    return (base / "astra_server").string();
#endif
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string makeDataDir() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch()
                            .count();
    return (fs::temp_directory_path() /
            ("astra_server_test_" + std::to_string(unique)))
        .string();
}

HttpResponse postIngest(std::uint16_t port, const std::string& token,
                        const std::string& body,
                        const std::string& origin = "") {
    HttpRequest request;
    request.host = "127.0.0.1";
    request.port = port;
    request.path = "/api/v1/candles/ingest";
    request.method = "POST";
    request.body = body;
    request.headers = "Authorization: Bearer " + token + "\r\n";
    if (!origin.empty()) {
        request.headers += "Origin: " + origin + "\r\n";
    }
    request.connectTimeoutMillis = 3000;
    request.readTimeoutMillis = 8000;
    return httpRequest(request);
}

}  // namespace

TEST_CASE(astra_server_requires_sender_auth) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();

    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "s3cret-tok"};
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    // Wait for the API to bind.
    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(ready);

    // Unauthenticated ingestion is rejected.
    const HttpResponse noAuth = postIngest(port, "", "{}");
    CHECK_EQ(noAuth.statusCode, 401);
    CHECK(contains(noAuth.body, "\"code\":\"unauthorized\""));

    // Wrong token is rejected.
    const HttpResponse wrong = postIngest(port, "wrong", "{}");
    CHECK_EQ(wrong.statusCode, 401);

    // Unauthenticated sync is rejected.
    HttpRequest sync;
    sync.host = "127.0.0.1";
    sync.port = port;
    sync.path = "/api/v1/sync/latest";
    const HttpResponse syncNoAuth = httpGet(sync);
    CHECK_EQ(syncNoAuth.statusCode, 401);

    // Malformed body with a valid token is a 400, not a 500.
    const HttpResponse bad = postIngest(port, "s3cret-tok", "not json");
    CHECK_EQ(bad.statusCode, 400);
    CHECK(contains(bad.body, "\"code\":\"malformed_json\""));

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

TEST_CASE(astra_server_ingestion_is_idempotent_and_durable) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();

    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "s3cret-tok"};
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(ready);

    const std::string body =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":1792045800,\"open\":2653.1,\"high\":2655.2,"
        "\"low\":2651.8,\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
        "\"real_volume\":0}]}";

    // First send: stored_new.
    const HttpResponse first = postIngest(port, "s3cret-tok", body);
    CHECK_EQ(first.statusCode, 200);
    CHECK(contains(first.body, "\"state\":\"stored_new\""));
    CHECK(contains(first.body, "\"ingested\":1"));

    // Replay: already_stored, never a duplicate.
    const HttpResponse replay = postIngest(port, "s3cret-tok", body);
    CHECK_EQ(replay.statusCode, 200);
    CHECK(contains(replay.body, "\"state\":\"already_stored\""));
    CHECK(contains(replay.body, "\"ingested\":0"));
    CHECK(!contains(replay.body, "\"state\":\"stored_new\""));

    // Chart route reports the persisted candle.
    HttpRequest candles;
    candles.host = "127.0.0.1";
    candles.port = port;
    candles.path = "/api/v1/candles?tf=M15&limit=10";
    const HttpResponse chart = httpGet(candles);
    CHECK_EQ(chart.statusCode, 200);
    CHECK(contains(chart.body, "\"count\":1"));
    CHECK(contains(chart.body, "2654.05"));

    // Sync endpoint exposes the watermark.
    HttpRequest sync;
    sync.host = "127.0.0.1";
    sync.port = port;
    sync.path = "/api/v1/sync/latest";
    sync.headers = "Authorization: Bearer s3cret-tok\r\n";
    const HttpResponse syncResp = httpGet(sync);
    CHECK_EQ(syncResp.statusCode, 200);
    CHECK(contains(syncResp.body, "\"timeframe\":\"M15\""));
    CHECK(contains(syncResp.body, "\"last_open_time_utc\":1792045800"));

    // Stop the process.
    std::string stopError; supervisor.stop(2000, stopError);

    // Restart against the same data dir: data must survive.
    {
        const std::uint16_t port2 = freePort();
        ProcessLaunchSpec spec2;
        spec2.executable = exe;
        spec2.args = {"--host", "127.0.0.1", "--port", std::to_string(port2),
                      "--data-dir", dataDir, "--sender-token", "s3cret-tok"};
        ProcessSupervisor supervisor2(spec2);
        std::string startError2;
        REQUIRE(supervisor2.start(startError2));

        bool ready2 = false;
        for (int i = 0; i < 60 && !ready2; ++i) {
            HttpRequest health2;
            health2.host = "127.0.0.1";
            health2.port = port2;
            health2.path = "/api/v1/health";
            health2.connectTimeoutMillis = 300;
            health2.readTimeoutMillis = 500;
            if (httpGet(health2).ok) ready2 = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(ready2);

        // The persisted candle is still there after restart.
        HttpRequest candles2;
        candles2.host = "127.0.0.1";
        candles2.port = port2;
        candles2.path = "/api/v1/candles?tf=M15&limit=10";
        const HttpResponse chart2 = httpGet(candles2);
        CHECK_EQ(chart2.statusCode, 200);
        CHECK(contains(chart2.body, "\"count\":1"));
        CHECK(contains(chart2.body, "2654.05"));

        // The same candle is still a duplicate (not stored again).
        const HttpResponse replay2 =
            postIngest(port2, "s3cret-tok", body);
        CHECK_EQ(replay2.statusCode, 200);
        CHECK(contains(replay2.body, "\"state\":\"already_stored\""));
        CHECK(contains(replay2.body, "\"ingested\":0"));

        std::string stopError2; supervisor2.stop(2000, stopError2);
    }
    fs::remove_all(dataDir);
}

TEST_CASE(astra_server_rejects_future_and_invalid_timestamps) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();

    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "s3cret-tok"};
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(ready);

    // A candle whose open_time is far in the future must be rejected, not
    // stored as fabricated market history.
    const int64_t future = std::time(nullptr) + 24 * 3600;
    std::string body =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\": " + std::to_string(future) +
        ",\"open\":2653.1,\"high\":2655.2,"
        "\"low\":2651.8,\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
        "\"real_volume\":0}]}";
    const HttpResponse futureResp = postIngest(port, "s3cret-tok", body);
    CHECK_EQ(futureResp.statusCode, 200);
    CHECK(contains(futureResp.body, "\"state\":\"rejected\""));
    CHECK(contains(futureResp.body, "in the future"));
    CHECK(contains(futureResp.body, "\"ingested\":0"));

    // A non-positive timestamp must also be rejected.
    const std::string zeroBody =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":0,\"open\":2653.1,\"high\":2655.2,"
        "\"low\":2651.8,\"close\":2654.05}]}";
    const HttpResponse zeroResp = postIngest(port, "s3cret-tok", zeroBody);
    CHECK_EQ(zeroResp.statusCode, 200);
    CHECK(contains(zeroResp.body, "\"state\":\"rejected\""));
    CHECK(contains(zeroResp.body, "positive epoch"));
    CHECK(contains(zeroResp.body, "\"ingested\":0"));

    // Nothing malformed may have entered the store: the chart route honestly
    // reports that no candle data is available for the timeframe.
    HttpRequest candles;
    candles.host = "127.0.0.1";
    candles.port = port;
    candles.path = "/api/v1/candles?tf=M15&limit=10";
    const HttpResponse chart = httpGet(candles);
    CHECK_EQ(chart.statusCode, 503);
    CHECK(contains(chart.body, "\"code\":\"dependency_unavailable\""));

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

TEST_CASE(astra_server_enforces_cors_allowlist) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();

    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "s3cret-tok",
                 "--allowed-origin", "https://astra.example.com"};
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(ready);

    // Disallowed origin on a read route -> 403, honest (never permissive).
    HttpRequest disallowed;
    disallowed.host = "127.0.0.1";
    disallowed.port = port;
    disallowed.path = "/api/v1/timeframes";
    disallowed.headers = "Origin: https://evil.example.com\r\n";
    const HttpResponse badOrigin = httpGet(disallowed);
    CHECK_EQ(badOrigin.statusCode, 403);
    CHECK(contains(badOrigin.body, "\"code\":\"origin_not_allowed\""));

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}