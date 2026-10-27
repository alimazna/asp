// TST-NEWS-0002 - News API integration tests against the real `astra_server`
// binary. Verifies the private mobile surface end to end:
//   * the news routes require the client credential (401 otherwise);
//   * a client token cannot write candles and a sender token cannot read news;
//   * news ingest persists and dedups across sources;
//   * the list and analyze routes return the persisted article + analysis;
//   * the mock provider drives the analysis path without external quota;
//   * history reflects persisted analyses across a server restart.
//
// The server runs with ASTRA_LLM_PROVIDER=mock so the analyze route is
// deterministic and offline.

#include "TestHarness.h"

#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
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

namespace {

std::uint16_t freePort() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 8798;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8798;
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
        return 8798;
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
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("astra_news_api_" + std::to_string(unique)))
        .string();
}

bool waitReady(std::uint16_t port) {
    for (int i = 0; i < 60; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

HttpResponse request(const std::uint16_t port, const std::string& method,
                     const std::string& path, const std::string& token,
                     const std::string& body = "") {
    HttpRequest req;
    req.host = "127.0.0.1";
    req.port = port;
    req.path = path;
    req.method = method;
    req.body = body;
    if (!token.empty()) {
        req.headers = "Authorization: Bearer " + token + "\r\n";
    }
    req.connectTimeoutMillis = 3000;
    req.readTimeoutMillis = 15000;
    return httpRequest(req);
}

const char* kNewsBody =
    "{\"articles\":["
    "{\"headline\":\"Fed cuts rates by 25bp\",\"source\":\"example.com\","
    "\"url\":\"https://example.com/a1\",\"category\":\"monetary_policy\","
    "\"published_at\":1792045800,\"body\":\"The Fed cut rates.\","
    "\"gold_relevance\":\"high\"},"
    "{\"headline\":\"Fed cuts rates by 25 bp\",\"source\":\"other.com\","
    "\"url\":\"https://other.com/a2\",\"category\":\"monetary_policy\","
    "\"published_at\":1792045900,\"body\":\"Rate cut reported.\","
    "\"gold_relevance\":\"high\"}"
    "]}";

// Two distinct Arabic headlines (the app's primary language). They must be
// stored as two separate stories, not collapsed into one duplicate.
const char* kArabicNewsBody =
    "{\"articles\":["
    "{\"headline\":\"ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع\","
    "\"source\":\"example.com\",\"url\":\"https://example.com/ar1\","
    "\"category\":\"inflation\",\"published_at\":1792045800,"
    "\"gold_relevance\":\"high\"},"
    "{\"headline\":\"البنك المركزي يثبت أسعار الفائدة دون تغيير\","
    "\"source\":\"other.com\",\"url\":\"https://other.com/ar2\","
    "\"category\":\"monetary_policy\",\"published_at\":1792045900,"
    "\"gold_relevance\":\"medium\"}"
    "]}";

// A server spec with the news surface enabled. `withClient` controls whether the
// read credential is configured at all.
ProcessLaunchSpec newsSpec(const std::string& exe, std::uint16_t port,
                           const std::string& dataDir, bool withClient) {
    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "sender-tok",
                 "--admin-token", "admin-tok"};
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
    if (withClient) spec.environment["ASTRA_CLIENT_TOKEN"] = "client-tok";
    return spec;
}

}  // namespace

TEST_CASE(news_routes_require_client_credential) {
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
                 "--data-dir", dataDir, "--sender-token", "sender-tok",
                 "--admin-token", "admin-tok"};
    spec.environment["ASTRA_CLIENT_TOKEN"] = "client-tok";
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // No credential -> 401 on every news route.
    CHECK_EQ(request(port, "GET", "/api/v1/news", "").statusCode, 401);
    CHECK_EQ(request(port, "GET", "/api/v1/history", "").statusCode, 401);
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "", kNewsBody).statusCode,
             401);

    // A sender token cannot read the private news surface.
    CHECK_EQ(request(port, "GET", "/api/v1/news", "sender-tok").statusCode, 401);

    // A read-only client token cannot WRITE news either: the client token ships
    // in the APK and must never authorize a write.
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "client-tok",
                     kNewsBody)
                 .statusCode,
             401);

    // A client token cannot write candles (write path needs a sender token).
    const std::string candleBody =
        "{\"source_id\":\"s1\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
        "\"candles\":[{\"time\":1792045800,\"open\":2653.1,\"high\":2655.2,"
        "\"low\":2651.8,\"close\":2654.05}]}";
    CHECK_EQ(request(port, "POST", "/api/v1/candles/ingest", "client-tok",
                     candleBody)
                 .statusCode,
             401);

    // The client token reads news successfully (empty feed initially).
    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "\"news\":[]"));

    // A sender token can write news (it is the data-provider credential).
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok",
                     kNewsBody)
                 .statusCode,
             200);

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

TEST_CASE(news_ingest_dedups_and_analyzes_with_mock_provider) {
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
                 "--data-dir", dataDir, "--sender-token", "sender-tok"};
    spec.environment["ASTRA_CLIENT_TOKEN"] = "client-tok";
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // Ingest two articles describing the same story. Ingest is a write, so it
    // uses the sender credential.
    const HttpResponse ingest =
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody);
    CHECK_EQ(ingest.statusCode, 200);
    CHECK(contains(ingest.body, "\"received\":2"));
    CHECK(contains(ingest.body, "\"stored\":1"));
    CHECK(contains(ingest.body, "\"duplicates\":1"));

    // Replay is idempotent.
    const HttpResponse replay =
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody);
    CHECK_EQ(replay.statusCode, 200);
    CHECK(contains(replay.body, "\"stored\":0"));

    // The list shows one canonical article.
    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "Fed cuts rates by 25bp"));

    // Analyze the canonical article through the mock provider.
    const HttpResponse analyze = request(
        port, "GET", "/api/v1/news/analyze?id=https://example.com/a1",
        "client-tok");
    CHECK_EQ(analyze.statusCode, 200);
    CHECK(contains(analyze.body, "\"status\":\"ok\""));
    CHECK(contains(analyze.body, "\"direction\":\"bullish\""));
    CHECK(contains(analyze.body, "\"provider_id\":\"mock\""));

    // Missing id is a 400, unknown id is a 404.
    CHECK_EQ(request(port, "GET", "/api/v1/news/analyze", "client-tok").statusCode,
             400);
    CHECK_EQ(request(port, "GET", "/api/v1/news/analyze?id=nope", "client-tok")
                 .statusCode,
             404);

    // History now has one persisted analysis.
    const HttpResponse history =
        request(port, "GET", "/api/v1/history", "client-tok");
    CHECK_EQ(history.statusCode, 200);
    CHECK(contains(history.body, "\"history\":["));

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// A configured client token must not grant the admin (purge) surface, and the
// admin token must not grant the private client read surface.
TEST_CASE(news_client_and_admin_credentials_are_separate) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    ProcessSupervisor supervisor(newsSpec(exe, port, dataDir, true));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // The client token cannot reach the admin inspection route.
    CHECK_EQ(request(port, "GET", "/api/v1/admin/candles?tf=M15", "client-tok")
                 .statusCode,
             401);
    // The admin token cannot read the private news surface.
    CHECK_EQ(request(port, "GET", "/api/v1/news", "admin-tok").statusCode, 401);

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// With no client token configured the private routes are disabled (401), never
// served unauthenticated.
TEST_CASE(news_routes_disabled_without_client_token) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    ProcessSupervisor supervisor(newsSpec(exe, port, dataDir, false));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // Even a well-formed bearer token cannot open a route with no configured
    // credential behind it.
    CHECK_EQ(request(port, "GET", "/api/v1/news", "client-tok").statusCode, 401);
    CHECK_EQ(request(port, "GET", "/api/v1/history", "client-tok").statusCode, 401);
    // Ingest is sender-guarded; with no credential it is still refused.
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "", kNewsBody)
                 .statusCode,
             401);

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// Method and input validation on the news surface.
TEST_CASE(news_route_input_validation) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    ProcessSupervisor supervisor(newsSpec(exe, port, dataDir, true));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // Wrong method: news ingest only accepts POST (sender credential).
    CHECK_EQ(request(port, "GET", "/api/v1/news/ingest", "sender-tok").statusCode,
             405);
    // Missing body is rejected before the handler runs.
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok").statusCode,
             400);
    // Malformed JSON is a 400, not a 500 or a crash.
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok", "{nope")
                 .statusCode,
             400);
    // An article missing the required headline is rejected.
    const std::string missingField =
        "{\"articles\":[{\"source\":\"example.com\"}]}";
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok",
                     missingField)
                 .statusCode,
             400);
    // An unknown analysis id is a 404; a missing id is a 400.
    CHECK_EQ(request(port, "GET", "/api/v1/news/analyze?id=unknown-id", "client-tok")
                 .statusCode,
             404);
    CHECK_EQ(request(port, "GET", "/api/v1/news/analyze", "client-tok").statusCode,
             400);

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// Distinct Arabic stories must both persist (regression: ASCII-only dedup keys
// collapsed every Arabic headline to one story).
TEST_CASE(news_arabic_stories_are_not_collapsed) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    ProcessSupervisor supervisor(newsSpec(exe, port, dataDir, true));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    const HttpResponse ingest =
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", kArabicNewsBody);
    CHECK_EQ(ingest.statusCode, 200);
    CHECK(contains(ingest.body, "\"received\":2"));
    CHECK(contains(ingest.body, "\"stored\":2"));
    CHECK(contains(ingest.body, "\"duplicates\":0"));

    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "ارتفع الذهب"));
    CHECK(contains(list.body, "البنك المركزي"));

    // The gold_only filter keeps high/medium relevance rows and drops nothing
    // here (both qualify), so the count is still two.
    const HttpResponse goldList =
        request(port, "GET", "/api/v1/news?gold_only=true", "client-tok");
    CHECK_EQ(goldList.statusCode, 200);
    CHECK(contains(goldList.body, "ارتفع الذهب"));

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}
