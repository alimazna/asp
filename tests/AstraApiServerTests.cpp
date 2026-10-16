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

    // A second source sends the SAME symbol/timeframe at a DIFFERENT (earlier)
    // open time. The aggregate sync view legitimately reports the newest row
    // across sources, but a source-scoped query must never let source A read
    // source B's watermark as proof that A's own candle was persisted.
    const std::string bodyB =
        "{\"source_id\":\"sender-2\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":1792042200,\"open\":2654.1,\"high\":2656.2,"
        "\"low\":2652.8,\"close\":2655.05,\"tick_volume\":180,\"spread\":11,"
        "\"real_volume\":0}]}";
    const HttpResponse secondSource = postIngest(port, "s3cret-tok", bodyB);
    CHECK_EQ(secondSource.statusCode, 200);
    CHECK(contains(secondSource.body, "\"state\":\"stored_new\""));

    // Aggregate: newest across sources (sender-1's later candle).
    const HttpResponse aggregate = httpGet(sync);
    CHECK(contains(aggregate.body, "\"last_open_time_utc\":1792045800"));

    // Scoped to sender-2: only sender-2's own watermark, never sender-1's.
    HttpRequest scoped;
    scoped.host = "127.0.0.1";
    scoped.port = port;
    scoped.path = "/api/v1/sync/latest?source_id=sender-2";
    scoped.headers = "Authorization: Bearer s3cret-tok\r\n";
    const HttpResponse scopedResp = httpGet(scoped);
    CHECK_EQ(scopedResp.statusCode, 200);
    CHECK(contains(scopedResp.body, "\"last_open_time_utc\":1792042200"));
    CHECK(!contains(scopedResp.body, "1792045800"));

    // Scoped to an unknown source: empty, not another source's data.
    HttpRequest unknown;
    unknown.host = "127.0.0.1";
    unknown.port = port;
    unknown.path = "/api/v1/sync/latest?source_id=ghost";
    unknown.headers = "Authorization: Bearer s3cret-tok\r\n";
    const HttpResponse unknownResp = httpGet(unknown);
    CHECK_EQ(unknownResp.statusCode, 200);
    CHECK(!contains(unknownResp.body, "1792045800"));
    CHECK(!contains(unknownResp.body, "1792042200"));

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

        // Both persisted candles are still there after restart (sender-1's and
        // sender-2's), proving the store is durable and not source-collapsed.
        HttpRequest candles2;
        candles2.host = "127.0.0.1";
        candles2.port = port2;
        candles2.path = "/api/v1/candles?tf=M15&limit=10";
        const HttpResponse chart2 = httpGet(candles2);
        CHECK_EQ(chart2.statusCode, 200);
        CHECK(contains(chart2.body, "\"count\":2"));
        CHECK(contains(chart2.body, "2654.05"));
        CHECK(contains(chart2.body, "2655.05"));

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

TEST_CASE(astra_server_rejects_misaligned_open_times) {
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

    // A candle whose open_time is not a multiple of the M15 duration (900s)
    // is not real M15 data. The production store previously accepted such
    // rows; the ingest path must now reject them so state stays honest.
    const std::string misalignedBody =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":1792068271,\"open\":1900,\"high\":1910.5,"
        "\"low\":1898.2,\"close\":1905.7,\"tick_volume\":1234,\"spread\":0,"
        "\"real_volume\":0}]}";
    const HttpResponse misaligned = postIngest(port, "s3cret-tok", misalignedBody);
    CHECK_EQ(misaligned.statusCode, 200);
    CHECK(contains(misaligned.body, "\"state\":\"rejected\""));
    CHECK(contains(misaligned.body, "not aligned"));
    CHECK(contains(misaligned.body, "\"ingested\":0"));

    // A genuinely aligned M15 candle (exact multiple of 900, and old enough to
    // be closed) is accepted.
    const std::string alignedBody =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":1792068300,\"open\":2653.1,\"high\":2655.2,"
        "\"low\":2651.8,\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
        "\"real_volume\":0}]}";
    const HttpResponse aligned = postIngest(port, "s3cret-tok", alignedBody);
    CHECK_EQ(aligned.statusCode, 200);
    CHECK(contains(aligned.body, "\"state\":\"stored_new\""));
    CHECK(contains(aligned.body, "\"ingested\":1"));

    // The store holds exactly the one aligned candle, and the timeframe now
    // reports itself as observed because of real, grid-aligned data.
    HttpRequest candles;
    candles.host = "127.0.0.1";
    candles.port = port;
    candles.path = "/api/v1/candles?tf=M15&limit=10";
    const HttpResponse chart = httpGet(candles);
    CHECK_EQ(chart.statusCode, 200);
    CHECK(contains(chart.body, "\"count\":1"));
    CHECK(contains(chart.body, "2654.05"));

    HttpRequest tfReq;
    tfReq.host = "127.0.0.1";
    tfReq.port = port;
    tfReq.path = "/api/v1/timeframes";
    const HttpResponse tfResp = httpGet(tfReq);
    CHECK_EQ(tfResp.statusCode, 200);
    CHECK(contains(tfResp.body, "\"last_closed_bar_open\":1792068300"));

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

// T33 item 2: liveness and readiness are distinct, and /health carries the
// build identity. livez must answer even while the engine is not READY (it
// never consults the engine); readyz must gate on the runtime and expose the
// version.
TEST_CASE(astra_server_exposes_livez_readyz_and_health_version) {
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

    bool alive = false;
    for (int i = 0; i < 60 && !alive; ++i) {
        HttpRequest probe;
        probe.host = "127.0.0.1";
        probe.port = port;
        probe.path = "/api/v1/livez";
        probe.connectTimeoutMillis = 300;
        probe.readTimeoutMillis = 500;
        if (httpGet(probe).ok) alive = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(alive);

    HttpRequest live;
    live.host = "127.0.0.1";
    live.port = port;
    live.path = "/api/v1/livez";
    const HttpResponse liveResp = httpGet(live);
    CHECK_EQ(liveResp.statusCode, 200);
    CHECK(contains(liveResp.body, "\"live\":true"));
    CHECK(contains(liveResp.body, "\"version\":\"v1\""));

    // /health now advertises the build identity.
    HttpRequest health;
    health.host = "127.0.0.1";
    health.port = port;
    health.path = "/api/v1/health";
    const HttpResponse healthResp = httpGet(health);
    CHECK_EQ(healthResp.statusCode, 200);
    CHECK(contains(healthResp.body, "\"version\":\"v1\""));
    CHECK(contains(healthResp.body, "\"schema\":\"1.0\""));

    // /readyz must be reachable and carry a boolean ready flag. In this
    // server-only deployment there is no local MT5 bridge, so the runtime is
    // expected to be not-ready (503); the test accepts either but requires the
    // flag to be truthful and the version present.
    HttpRequest ready;
    ready.host = "127.0.0.1";
    ready.port = port;
    ready.path = "/api/v1/readyz";
    const HttpResponse readyResp = httpGet(ready);
    CHECK(readyResp.statusCode == 200 || readyResp.statusCode == 503);
    CHECK(contains(readyResp.body, "\"ready\":"));
    CHECK(contains(readyResp.body, "\"version\":\"v1\""));

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// T33 item 3: the ingestion surface is rate-limited and bounds the batch size.
// A hostile or buggy sender must not be able to loop the parser or the store.
TEST_CASE(astra_server_rate_limits_ingest_and_caps_batch) {
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
    // Tiny per-minute budget and a batch cap of 2 so the test is fast. The
    // rejected oversized batch still consumes one token (rate limiting happens
    // before parsing), so the limit is set to 4 to leave room for 3 valid ones.
    spec.environment["ASTRA_INGEST_RATE_PER_MIN"] = "4";
    spec.environment["ASTRA_MAX_CANDLES_PER_BATCH"] = "2";
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

    const auto minuteBody = [](int baseSec) {
        return std::string(
                   "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
                   "\"timeframe\":\"M15\",\"candles\":[{\"time\":") +
               std::to_string(baseSec) +
               ",\"open\":2653.1,\"high\":2655.2,\"low\":2651.8,"
               "\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
               "\"real_volume\":0}]}";
    };

    // A batch of 3 exceeds the cap of 2 and must be refused before parsing.
    const std::string threeBody =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
        "\"timeframe\":\"M15\",\"candles\":["
        "{\"time\":1,\"open\":1,\"high\":1,\"low\":1,\"close\":1},"
        "{\"time\":2,\"open\":1,\"high\":1,\"low\":1,\"close\":1},"
        "{\"time\":3,\"open\":1,\"high\":1,\"low\":1,\"close\":1}]}";
    const HttpResponse tooBig = postIngest(port, "s3cret-tok", threeBody);
    CHECK_EQ(tooBig.statusCode, 413);
    CHECK(contains(tooBig.body, "\"code\":\"batch_too_large\""));

    // The oversized batch consumed one token, then three valid ones consume the
    // rest of the budget (limit 4); the next is 429.
    const int64_t base = (std::time(nullptr) / 900) * 900 - 3600;
    for (int i = 0; i < 3; ++i) {
        const HttpResponse r =
            postIngest(port, "s3cret-tok",
                       minuteBody(static_cast<int>(base + i * 900)));
        CHECK_EQ(r.statusCode, 200);
    }
    const HttpResponse limited =
        postIngest(port, "s3cret-tok",
                   minuteBody(static_cast<int>(base + 3 * 900)));
    CHECK_EQ(limited.statusCode, 429);
    CHECK(contains(limited.body, "\"code\":\"rate_limited\""));

    // A different (wrong) credential never reaches the limiter: 401 first.
    const HttpResponse unauth = postIngest(port, "another-tok",
                                           minuteBody(static_cast<int>(base)));
    CHECK_EQ(unauth.statusCode, 401);

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// T33 item 7: an authenticated admin route can purge exactly one reviewed
// quarantined row. The sender credential must NOT be able to reach it.
TEST_CASE(astra_server_admin_purge_requires_admin_token_and_deletes_one_row) {
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
                 "--admin-token", "adm1n-tok"};
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

    // Seed one aligned M15 candle through the normal ingestion path.
    const int64_t openTime = (std::time(nullptr) / 900) * 900 - 3600;
    const std::string body =
        std::string("{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
                    "\"timeframe\":\"M15\",\"candles\":[{\"time\":") +
        std::to_string(openTime) +
        ",\"open\":2653.1,\"high\":2655.2,\"low\":2651.8,\"close\":2654.05,"
        "\"tick_volume\":210,\"spread\":12,\"real_volume\":0}]}";
    const HttpResponse seeded = postIngest(port, "s3cret-tok", body);
    CHECK_EQ(seeded.statusCode, 200);
    CHECK(contains(seeded.body, "\"state\":\"stored_new\""));

    const std::string deleteBody =
        std::string("{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\","
                    "\"timeframe\":\"M15\",\"open_time\":") +
        std::to_string(openTime) + "}";

    const auto deleteReq = [&](const std::string& token) {
        HttpRequest request;
        request.host = "127.0.0.1";
        request.port = port;
        request.path = "/api/v1/admin/candles";
        request.method = "DELETE";
        request.body = deleteBody;
        request.headers = "Authorization: Bearer " + token + "\r\n";
        request.connectTimeoutMillis = 3000;
        request.readTimeoutMillis = 8000;
        return httpRequest(request);
    };

    // The SENDER credential must not be accepted on the admin surface.
    const HttpResponse senderTry = deleteReq("s3cret-tok");
    CHECK_EQ(senderTry.statusCode, 401);
    // No token -> 401.
    const HttpResponse noToken = deleteReq("");
    CHECK_EQ(noToken.statusCode, 401);
    // A GET on the admin route is a method error.
    {
        HttpRequest getReq;
        getReq.host = "127.0.0.1";
        getReq.port = port;
        getReq.path = "/api/v1/admin/candles";
        getReq.headers = "Authorization: Bearer adm1n-tok\r\n";
        const HttpResponse r = httpGet(getReq);
        CHECK_EQ(r.statusCode, 405);
    }

    // The admin credential purges exactly one row.
    const HttpResponse purged = deleteReq("adm1n-tok");
    CHECK_EQ(purged.statusCode, 200);
    CHECK(contains(purged.body, "\"deleted\":1"));
    CHECK(contains(purged.body, "\"state\":\"purged\""));

    // A repeat is a no-op that reports not_found (never a second deletion).
    const HttpResponse again = deleteReq("adm1n-tok");
    CHECK_EQ(again.statusCode, 200);
    CHECK(contains(again.body, "\"deleted\":0"));
    CHECK(contains(again.body, "\"state\":\"not_found\""));

    std::string stopError; supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}
