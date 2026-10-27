// TST-AYZR-A-0001 - AYZR/ASTRA v1 API contract pin (Agent A).
//
// Pins the status codes, the flat error-envelope shape and the top-level
// response keys of the routes the Android client and the separate MT5 sender
// depend on, so a future change cannot silently break either integration.
//
// Every case drives the real `astra_server` binary over loopback HTTP against a
// real SQLite file. The LLM provider is the deterministic mock
// (ASTRA_LLM_PROVIDER=mock): no live LLM7 call is made and no quota is spent.

#include "TestHarness.h"

#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
    if (s == INVALID_SOCKET) return 8796;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8796;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
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
        return 8796;
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
    const fs::path base = fs::current_path();
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
            ("astra_api_contract_" + std::to_string(unique)))
        .string();
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

bool waitReady(std::uint16_t port) {
    for (int i = 0; i < 80; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 800;
        if (httpGet(health).ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// A raw HTTP exchange, so a case can assert response HEADERS (e.g. Retry-After)
// that the loopback HttpClient does not surface. POSIX-only; Windows returns an
// empty string and the caller skips.
std::string rawExchange(const std::uint16_t port, const std::string& requestText) {
#ifdef _WIN32
    (void)port;
    (void)requestText;
    return std::string();
#else
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return std::string();
    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return std::string();
    }
    std::size_t sent = 0;
    while (sent < requestText.size()) {
        const ssize_t n =
            ::send(sock, requestText.data() + sent, requestText.size() - sent, 0);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    std::string raw;
    char buffer[4096];
    while (true) {
        const ssize_t n = ::recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(sock);
    return raw;
#endif
}

// The three-credential configuration used across the contract cases.
struct ContractServer {
    std::unique_ptr<ProcessSupervisor> supervisor;
    std::uint16_t port = 0;
    std::string dataDir;

    bool start(std::string& error,
               const std::vector<std::string>& extraArgs = {},
               const std::map<std::string, std::string>& extraEnv = {}) {
        const std::string exe = binaryPath();
        if (!fs::exists(exe)) {
            error = "astra_server binary not built";
            return false;
        }
        port = freePort();
        dataDir = makeDataDir();
        ProcessLaunchSpec spec;
        spec.executable = exe;
        spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--data-dir", dataDir, "--sender-token", "sender-tok",
                     "--admin-token", "admin-tok"};
        for (const auto& a : extraArgs) spec.args.push_back(a);
        spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
        spec.environment["ASTRA_CLIENT_TOKEN"] = "client-tok";
        for (const auto& kv : extraEnv) spec.environment[kv.first] = kv.second;
        supervisor = std::make_unique<ProcessSupervisor>(spec);
        if (!supervisor->start(error)) return false;
        if (!waitReady(port)) {
            error = "server did not become ready";
            return false;
        }
        return true;
    }

    ~ContractServer() {
        if (supervisor) {
            std::string stopError;
            supervisor->stop(2000, stopError);
        }
        if (!dataDir.empty()) {
            std::error_code ec;
            fs::remove_all(dataDir, ec);
        }
    }
};

// Assert a body is the flat error object with the expected code and that it is
// NOT an enveloped success body.
void checkErrorEnvelope(const HttpResponse& response, int expectedStatus,
                        const std::string& expectedCode) {
    CHECK_EQ(response.statusCode, expectedStatus);
    CHECK(contains(response.body, "\"error\":\"true\""));
    CHECK(contains(response.body, "\"code\":\"" + expectedCode + "\""));
    CHECK(contains(response.body, "\"message\":"));
    // An error must never be wrapped in the success envelope.
    CHECK(!contains(response.body, "\"api\":\"v1\""));
}

const char* kCandleBody =
    "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
    "\"candles\":[{\"time\":1792068300,\"open\":2653.1,\"high\":2655.2,"
    "\"low\":2651.8,\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
    "\"real_volume\":0}]}";

const char* kNewsBody =
    "{\"articles\":[{\"headline\":\"Gold rises on a weaker dollar\","
    "\"source\":\"example.com\",\"url\":\"https://example.com/a1\","
    "\"category\":\"markets\",\"published_at\":1792045800,"
    "\"body\":\"Gold rose.\",\"gold_relevance\":\"high\"}]}";

// An article the sender supplied without a publish time. Its timestamp is
// unknown, not epoch 0.
const char* kNewsBodyNoTimestamp =
    "{\"articles\":[{\"headline\":\"A story with no publish time\","
    "\"source\":\"example.com\",\"url\":\"https://example.com/nots\","
    "\"category\":\"other\",\"gold_relevance\":\"high\"}]}";

}  // namespace

// ---------------------------------------------------------------------------
// 1. Authentication boundaries: sender vs admin vs client.
// ---------------------------------------------------------------------------

// The private mobile read surface is client-token gated, and neither the
// sender (write) nor the admin (purge) credential can read it.
TEST_CASE(contract_news_read_routes_require_client_credential) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    const std::vector<std::string> readPaths = {
        "/api/v1/news", "/api/v1/news/analyze?id=x", "/api/v1/history"};
    for (const auto& path : readPaths) {
        // No credential -> 401 unauthorized.
        checkErrorEnvelope(request(port, "GET", path, ""), 401, "unauthorized");
        // Sender credential cannot read the private surface.
        checkErrorEnvelope(request(port, "GET", path, "sender-tok"), 401,
                           "unauthorized");
        // Admin credential cannot read the private surface.
        checkErrorEnvelope(request(port, "GET", path, "admin-tok"), 401,
                           "unauthorized");
        // An arbitrary token is rejected the same way.
        checkErrorEnvelope(request(port, "GET", path, "nonsense"), 401,
                           "unauthorized");
    }

    // The client credential is accepted (empty feed is still a 200).
    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "\"api\":\"v1\""));
    CHECK(contains(list.body, "\"schema\":\"1.0\""));
}

// A read-only client credential must never authorize a write, and the sender
// credential is the only one accepted on the candle write surface.
TEST_CASE(contract_write_routes_reject_client_and_admin_credentials) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    // Candle ingest: client and admin tokens are refused; sender is accepted.
    checkErrorEnvelope(request(port, "POST", "/api/v1/candles/ingest",
                               "client-tok", kCandleBody),
                       401, "unauthorized");
    checkErrorEnvelope(request(port, "POST", "/api/v1/candles/ingest",
                               "admin-tok", kCandleBody),
                       401, "unauthorized");
    CHECK_EQ(request(port, "POST", "/api/v1/candles/ingest", "sender-tok",
                     kCandleBody)
                 .statusCode,
             200);

    // News ingest is a WRITE: the client credential (shipped in the APK) is
    // refused, the sender credential is accepted.
    checkErrorEnvelope(request(port, "POST", "/api/v1/news/ingest", "client-tok",
                               kNewsBody),
                       401, "unauthorized");
    checkErrorEnvelope(request(port, "POST", "/api/v1/news/ingest", "admin-tok",
                               kNewsBody),
                       401, "unauthorized");
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody)
                 .statusCode,
             200);

    // Sync requires the sender credential.
    checkErrorEnvelope(request(port, "GET", "/api/v1/sync/latest", "client-tok"),
                       401, "unauthorized");
    CHECK_EQ(request(port, "GET", "/api/v1/sync/latest", "sender-tok").statusCode,
             200);
}

// The destructive admin surface requires the admin credential specifically; a
// leaked sender or client credential must not reach it.
TEST_CASE(contract_admin_surface_requires_admin_credential) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    checkErrorEnvelope(request(port, "GET", "/api/v1/admin/candles?tf=M15", ""),
                       401, "unauthorized");
    checkErrorEnvelope(
        request(port, "GET", "/api/v1/admin/candles?tf=M15", "sender-tok"), 401,
        "unauthorized");
    checkErrorEnvelope(
        request(port, "GET", "/api/v1/admin/candles?tf=M15", "client-tok"), 401,
        "unauthorized");
    // With the admin credential the inspection route is served.
    CHECK_EQ(request(port, "GET", "/api/v1/admin/candles?tf=M15", "admin-tok")
                 .statusCode,
             200);
}

// With no client credential configured the private routes are disabled (401)
// rather than served unauthenticated.
TEST_CASE(contract_news_routes_fail_closed_without_client_token) {
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
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
    // ASTRA_CLIENT_TOKEN deliberately absent.
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    // Even a well-formed bearer token cannot open a route with no configured
    // credential behind it.
    checkErrorEnvelope(request(port, "GET", "/api/v1/news", "client-tok"), 401,
                       "unauthorized");
    checkErrorEnvelope(request(port, "GET", "/api/v1/history", "client-tok"), 401,
                       "unauthorized");

    std::string stopError;
    supervisor.stop(2000, stopError);
    fs::remove_all(dataDir);
}

// ---------------------------------------------------------------------------
// 2. Error envelope shape and status codes.
// ---------------------------------------------------------------------------

// The flat error object {error,code,message} is the contract for every non-2xx
// on the routes the clients depend on.
TEST_CASE(contract_error_envelope_shape_is_flat_and_stable) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    // Unknown route -> 404 not_found.
    checkErrorEnvelope(request(port, "GET", "/api/v1/does-not-exist", "sender-tok"),
                       404, "not_found");

    // Wrong method on a known route -> 405 method_not_allowed.
    checkErrorEnvelope(request(port, "GET", "/api/v1/candles/ingest", "sender-tok"),
                       405, "method_not_allowed");
    checkErrorEnvelope(request(port, "POST", "/api/v1/sync/latest", "sender-tok"),
                       405, "method_not_allowed");
    checkErrorEnvelope(request(port, "POST", "/api/v1/candles", "sender-tok"),
                       405, "method_not_allowed");

    // Empty body where one is required -> 400 malformed_request.
    checkErrorEnvelope(request(port, "POST", "/api/v1/candles/ingest", "sender-tok"),
                       400, "malformed_request");
    checkErrorEnvelope(request(port, "POST", "/api/v1/news/ingest", "sender-tok"),
                       400, "malformed_request");

    // Malformed JSON -> 400 malformed_json (not 500, not a crash).
    checkErrorEnvelope(
        request(port, "POST", "/api/v1/candles/ingest", "sender-tok", "{nope"),
        400, "malformed_json");
    checkErrorEnvelope(
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", "{nope"), 400,
        "malformed_json");
}

// The client's typed failure mapping depends on these exact codes.
TEST_CASE(contract_news_route_validation_status_codes) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    // Missing ?id= -> 400 missing_id.
    checkErrorEnvelope(request(port, "GET", "/api/v1/news/analyze", "client-tok"),
                       400, "missing_id");
    // Unknown id -> 404 not_found.
    checkErrorEnvelope(
        request(port, "GET", "/api/v1/news/analyze?id=unknown-id", "client-tok"),
        404, "not_found");
    // Missing articles array -> 400 missing_articles.
    checkErrorEnvelope(
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", "{}"), 400,
        "missing_articles");
    // An article missing headline/source -> 400 missing_fields.
    checkErrorEnvelope(
        request(port, "POST", "/api/v1/news/ingest", "sender-tok",
                "{\"articles\":[{\"url\":\"https://x/y\"}]}"),
        400, "missing_fields");
    // Wrong method on the news read routes -> 405 (client credential first).
    checkErrorEnvelope(request(port, "POST", "/api/v1/news", "client-tok"), 405,
                       "method_not_allowed");
    checkErrorEnvelope(request(port, "GET", "/api/v1/news/ingest", "sender-tok"),
                       405, "method_not_allowed");
}

// A rate-limited response is a 429 with a Retry-After hint (asserted on the raw
// response, since the loopback client does not surface headers).
TEST_CASE(contract_rate_limit_returns_429_with_retry_after) {
    ContractServer server;
    std::string error;
    if (!server.start(error, {},
                      {{"ASTRA_SYNC_RATE_PER_MIN", "1"},
                       {"ASTRA_INGEST_RATE_PER_MIN", "1"}})) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    CHECK_EQ(request(port, "GET", "/api/v1/news", "client-tok").statusCode, 200);
    checkErrorEnvelope(request(port, "GET", "/api/v1/news", "client-tok"), 429,
                       "rate_limited");

    const std::string raw = rawExchange(
        port,
        "GET /api/v1/news HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Authorization: Bearer client-tok\r\nConnection: close\r\n\r\n");
    if (!raw.empty()) {
        CHECK(contains(raw, "429"));
        CHECK(contains(raw, "Retry-After:"));
    } else {
        std::cout << "  [SKIP] raw header probe unavailable\n";
    }
}

// ---------------------------------------------------------------------------
// 3. Success-envelope shape and backward compatibility.
// ---------------------------------------------------------------------------

// Every 2xx carries {api:"v1", schema:"1.0", data:...}; the Android client
// rejects anything else, so the keys are pinned here.
TEST_CASE(contract_success_envelope_keys_are_stable) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    CHECK_EQ(request(port, "POST", "/api/v1/candles/ingest", "sender-tok",
                     kCandleBody)
                 .statusCode,
             200);
    CHECK_EQ(request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody)
                 .statusCode,
             200);

    const std::vector<std::pair<std::string, std::string>> probes = {
        {"GET", "/api/v1/health"},
        {"GET", "/api/v1/livez"},
        {"GET", "/api/v1/candles?tf=M15&limit=5"},
        {"GET", "/api/v1/news"},
        {"GET", "/api/v1/history"},
        {"GET", "/api/v1/news/analyze?id=https://example.com/a1"},
    };
    for (const auto& probe : probes) {
        const HttpResponse response =
            request(port, probe.first, probe.second, "client-tok");
        CHECK_EQ(response.statusCode, 200);
        CHECK(contains(response.body, "\"api\":\"v1\""));
        CHECK(contains(response.body, "\"schema\":\"1.0\""));
        CHECK(contains(response.body, "\"data\":"));
    }
}

// The MT5 sender contract: the ingest ack carries the counters and the
// per-candle state vocabulary it reconciles on, and replay is idempotent.
TEST_CASE(contract_candle_ingest_ack_shape_is_stable) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    const HttpResponse first =
        request(port, "POST", "/api/v1/candles/ingest", "sender-tok", kCandleBody);
    CHECK_EQ(first.statusCode, 200);
    for (const char* key : {"\"source_id\"", "\"symbol\"", "\"timeframe\"",
                            "\"received\"", "\"accepted\"", "\"duplicates\"",
                            "\"rejected\"", "\"ingested\""}) {
        CHECK(contains(first.body, key));
    }
    CHECK(contains(first.body, "\"state\":\"stored_new\""));
    CHECK(contains(first.body, "\"ingested\":1"));

    // Replay yields already_stored, never a second row.
    const HttpResponse replay =
        request(port, "POST", "/api/v1/candles/ingest", "sender-tok", kCandleBody);
    CHECK_EQ(replay.statusCode, 200);
    CHECK(contains(replay.body, "\"state\":\"already_stored\""));
    CHECK(contains(replay.body, "\"ingested\":0"));

    // The sync watermark is scoped to the caller's own source identity.
    const HttpResponse sync =
        request(port, "GET", "/api/v1/sync/latest?source_id=sender-1", "sender-tok");
    CHECK_EQ(sync.statusCode, 200);
    CHECK(contains(sync.body, "\"last_open_time_utc\":1792068300"));
    CHECK(contains(sync.body, "\"count\":1"));
}

// The news ingest ack counters and per-article state vocabulary.
TEST_CASE(contract_news_ingest_ack_shape_is_stable) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    const HttpResponse ack =
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody);
    CHECK_EQ(ack.statusCode, 200);
    for (const char* key : {"\"received\"", "\"stored\"", "\"duplicates\"",
                            "\"rejected\"", "\"results\"", "\"article_id\"",
                            "\"state\"", "\"canonical_article_id\""}) {
        CHECK(contains(ack.body, key));
    }
    CHECK(contains(ack.body, "\"received\":1"));
    CHECK(contains(ack.body, "\"stored\":1"));

    // Replay is idempotent.
    const HttpResponse replay =
        request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody);
    CHECK_EQ(replay.statusCode, 200);
    CHECK(contains(replay.body, "\"stored\":0"));
}

// The news list payload keeps the keys the Android models read.
TEST_CASE(contract_news_list_and_detail_shape_is_stable) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    REQUIRE(request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody)
                .statusCode == 200);

    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    for (const char* key : {"\"news\"", "\"article\"", "\"analysis_status\"",
                            "\"article_id\"", "\"headline\"", "\"source\"",
                            "\"url\"", "\"category\"", "\"published_at\"",
                            "\"published_at_iso\"", "\"collected_at\"",
                            "\"gold_relevance\"", "\"body_available\""}) {
        CHECK(contains(list.body, key));
    }

    const HttpResponse detail = request(
        port, "GET", "/api/v1/news/analyze?id=https://example.com/a1", "client-tok");
    CHECK_EQ(detail.statusCode, 200);
    for (const char* key : {"\"article\"", "\"has_analysis\"", "\"analysis\"",
                            "\"status\"", "\"summary_ar\"", "\"facts\"",
                            "\"market_implications_ar\"",
                            "\"supporting_evidence_ar\"",
                            "\"opposing_evidence_ar\"", "\"gold_impact_ar\"",
                            "\"direction\"", "\"levels\"", "\"entry\"",
                            "\"stop_loss\"", "\"take_profit\"",
                            "\"astra_agreement\"", "\"limitations_ar\"",
                            "\"provider_id\"", "\"model_id\"", "\"analyzed_at\"",
                            "\"timeframe_context\"", "\"candle_as_of\""}) {
        CHECK(contains(detail.body, key));
    }
    // The mock provider is deterministic and offline.
    CHECK(contains(detail.body, "\"provider_id\":\"mock\""));
    CHECK(contains(detail.body, "\"status\":\"ok\""));

    const HttpResponse history = request(port, "GET", "/api/v1/history", "client-tok");
    CHECK_EQ(history.statusCode, 200);
    for (const char* key : {"\"history\"", "\"headline\"", "\"source\"",
                            "\"analysis\""}) {
        CHECK(contains(history.body, key));
    }
}

// ---------------------------------------------------------------------------
// 4. Data-honesty guard: an unknown timestamp is null, never epoch 0.
// ---------------------------------------------------------------------------

// A sender may legitimately omit published_at. The API must not present that
// absence as 1970-01-01, which the client would render as a real date.
TEST_CASE(contract_missing_publish_time_is_null_not_epoch_zero) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    REQUIRE(request(port, "POST", "/api/v1/news/ingest", "sender-tok",
                    kNewsBodyNoTimestamp)
                .statusCode == 200);

    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-tok");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "\"published_at\":null"));
    CHECK(contains(list.body, "\"published_at_iso\":null"));
    CHECK(!contains(list.body, "\"published_at\":0"));
    CHECK(!contains(list.body, "1970-01-01T00:00:00Z"));
}

// An analysis that could not observe a candle must report an unknown data
// version (null), not epoch 0.
TEST_CASE(contract_analysis_without_candle_evidence_reports_null_version) {
    ContractServer server;
    std::string error;
    if (!server.start(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    // No candle has been ingested, so the analysis has no candle evidence.
    REQUIRE(request(port, "POST", "/api/v1/news/ingest", "sender-tok", kNewsBody)
                .statusCode == 200);
    const HttpResponse detail = request(
        port, "GET", "/api/v1/news/analyze?id=https://example.com/a1", "client-tok");
    CHECK_EQ(detail.statusCode, 200);
    CHECK(contains(detail.body, "\"candle_as_of\":null"));
    CHECK(!contains(detail.body, "\"candle_as_of\":0"));
}

// ---------------------------------------------------------------------------
// 5. CORS preflight and origin handling.
// ---------------------------------------------------------------------------

// OPTIONS on a client route is answered 204 for a non-browser (no-Origin)
// request; a disallowed browser origin is refused 403 before any handler runs.
TEST_CASE(contract_cors_preflight_and_origin_allowlist) {
    ContractServer server;
    std::string error;
    if (!server.start(error, {"--allowed-origin", "https://app.example.com"})) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }
    const std::uint16_t port = server.port;

    // Preflight without an Origin header is a 204.
    CHECK_EQ(request(port, "OPTIONS", "/api/v1/news", "").statusCode, 204);

    // A disallowed browser origin is refused 403 origin_not_allowed.
    HttpRequest bad;
    bad.host = "127.0.0.1";
    bad.port = port;
    bad.path = "/api/v1/news";
    bad.headers = "Origin: https://evil.example.com\r\n";
    const HttpResponse refused = httpGet(bad);
    checkErrorEnvelope(refused, 403, "origin_not_allowed");

    // The allow-listed origin is reflected on the response.
    HttpRequest good;
    good.host = "127.0.0.1";
    good.port = port;
    good.path = "/api/v1/news";
    good.headers = "Origin: https://app.example.com\r\n"
                   "Authorization: Bearer client-tok\r\n";
    const HttpResponse allowed = httpGet(good);
    CHECK_EQ(allowed.statusCode, 200);
}
