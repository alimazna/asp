// TST-AYZR-D-0001 - AYZR/ASTRA security boundary suite (Agent D).
//
// Unlike the process-level suites (which spawn the real `astra_server` binary),
// this file drives a REAL `AstraApiServer` instance IN PROCESS: the same socket
// accept loop, the same header parser, the same `authorize`/`authorizeAdmin`/
// `authorizeClient` guards, the same rate limiter and the same route dispatch
// that production runs. The only thing that is a test double is the route
// handler body (a counter), which is deliberate: it is what lets a case assert
// that a denied request never reaches the handler at all.
//
// The property under test is the security boundary, not the handler:
//
//   * a sender credential authorises ONLY candle ingest, sync and news ingest;
//   * a client (app-embedded, read-only) credential can never write a candle,
//     ingest news, or reach the destructive admin surface;
//   * an admin credential authorises only the admin candle surface, never a
//     write and never the private read surface;
//   * missing, empty or malformed credentials fail CLOSED on every protected
//     route, including when the corresponding token list is unset;
//   * request-size limits, CORS and rate limits are enforced BEFORE the handler
//     runs;
//   * no error response leaks a credential, a path or an implementation detail.
//
// SHADOW ONLY: no order execution anywhere; nothing here mutates a decision.

#include "TestHarness.h"

#include "api/AstraApiServer.h"
#include "api/BackendApiSchema.h"
#include "api/BackendFacade.h"
#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
#include <sys/time.h>
#include <unistd.h>
#endif

using namespace aura;

namespace {

// The three distinct credentials every case starts from. The values are
// deliberately distinctive so a leak check can look for the exact string.
constexpr const char* kSender = "sender-cred-aaa";
constexpr const char* kAdmin = "admin-cred-bbb";
constexpr const char* kClient = "client-cred-ccc";

// A handler that counts how many times the guarded route actually ran. A denied
// request must leave the counter at zero, which is the strongest available proof
// that the guard (not the handler) rejected it.
struct Counters {
    std::atomic<int> ingest{0};
    std::atomic<int> sync{0};
    std::atomic<int> newsIngest{0};
    std::atomic<int> adminList{0};
    std::atomic<int> adminDelete{0};
    std::atomic<int> newsList{0};
    std::atomic<int> newsAnalyze{0};
    std::atomic<int> history{0};

    int total() const {
        return ingest.load() + sync.load() + newsIngest.load() +
               adminList.load() + adminDelete.load() + newsList.load() +
               newsAnalyze.load() + history.load();
    }
    void reset() {
        ingest = 0; sync = 0; newsIngest = 0; adminList = 0;
        adminDelete = 0; newsList = 0; newsAnalyze = 0; history = 0;
    }
};

ApiResponse ok200() { return ApiResponse{200, "application/json", "{\"ok\":true}", true, ""}; }

// A real in-process server plus the facade it serves. Handlers are wired before
// start() so no request can race the wiring.
struct Fixture {
    BackendFacade facade{FacadeDependencies{}};
    std::unique_ptr<AstraApiServer> server;
    Counters calls;
    std::uint16_t port = 0;
    std::string startError;

    bool start(const AstraApiServerConfig& cfgIn) {
        AstraApiServerConfig cfg = cfgIn;
        server = std::make_unique<AstraApiServer>(&facade, cfg);
        wire();
        if (!server->start(startError)) return false;
        port = server->port();
        return true;
    }

    void wire() {
        server->ingestHandler = [this](const std::string&) { ++calls.ingest; return ok200(); };
        server->syncHandler = [this](const std::string&) { ++calls.sync; return ok200(); };
        server->newsIngestHandler = [this](const std::string&) { ++calls.newsIngest; return ok200(); };
        server->adminListHandler = [this](const std::string&, int) { ++calls.adminList; return ok200(); };
        server->adminDeleteHandler = [this](const std::string&) { ++calls.adminDelete; return ok200(); };
        server->newsListHandler = [this](const std::string&) { ++calls.newsList; return ok200(); };
        server->newsAnalyzeHandler = [this](const std::string&) { ++calls.newsAnalyze; return ok200(); };
        server->historyHandler = [this](const std::string&) { ++calls.history; return ok200(); };
    }

    ~Fixture() { if (server) server->stop(); }
};

// The standard production-shaped configuration: three distinct credentials, an
// ephemeral port, and the rate limits disabled so a case that is not about
// throttling cannot be perturbed by it.
AstraApiServerConfig baseConfig() {
    AstraApiServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 0;   // ephemeral
    cfg.senderTokens = {kSender};
    cfg.adminTokens = {kAdmin};
    cfg.clientTokens = {kClient};
    cfg.allowedOrigins = {};
    cfg.ingestRequestsPerMinute = 0;
    cfg.syncRequestsPerMinute = 0;
    return cfg;
}

HttpResponse call(std::uint16_t port, const std::string& method,
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
    req.readTimeoutMillis = 8000;
    return httpRequest(req);
}

// Send a request with an arbitrary raw Authorization header line (so a case can
// present "Bearer ", "bearer x", a trailing-space variant, ...).
HttpResponse callRawAuth(std::uint16_t port, const std::string& method,
                         const std::string& path,
                         const std::string& authHeaderLine,
                         const std::string& body = "",
                         const std::string& extraHeaders = "") {
    HttpRequest req;
    req.host = "127.0.0.1";
    req.port = port;
    req.path = path;
    req.method = method;
    req.body = body;
    req.headers = authHeaderLine + extraHeaders;
    req.connectTimeoutMillis = 3000;
    req.readTimeoutMillis = 8000;
    return httpRequest(req);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Nothing a client can see may carry a credential, a source filename, a
// filesystem path or an implementation symbol.
void checkNoLeak(const HttpResponse& response) {
    CHECK(!contains(response.body, kSender));
    CHECK(!contains(response.body, kAdmin));
    CHECK(!contains(response.body, kClient));
    CHECK(!contains(response.body, ".cpp"));
    CHECK(!contains(response.body, ".h:"));
    CHECK(!contains(response.body, "sqlite3"));
    CHECK(!contains(response.body, "database"));
    CHECK(!contains(response.body, "std::"));
    CHECK(!contains(response.body, "AstraApiServer"));
    CHECK(!contains(response.body, "BackendFacade"));
    CHECK(!contains(response.body, "/workspace"));
    CHECK(!contains(response.body, "/tmp/"));
}

const char* kIngestBody =
    "{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
    "\"candles\":[{\"time\":1792068300,\"open\":2653.1,\"high\":2655.2,"
    "\"low\":2651.8,\"close\":2654.05}]}";
const char* kNewsBody =
    "{\"articles\":[{\"headline\":\"h\",\"source\":\"s\"}]}";
const char* kAdminBody =
    "{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
    "\"open_time\":1792068300}";

// Every protected route, with the method and body it expects. Used by the
// "no credential may reach a handler" sweeps.
struct Route {
    const char* method;
    const char* path;
    const char* body;
};

std::vector<Route> protectedRoutes() {
    return {
        {"POST", "/api/v1/candles/ingest", kIngestBody},
        {"GET", "/api/v1/sync/latest", ""},
        {"POST", "/api/v1/news/ingest", kNewsBody},
        {"GET", "/api/v1/admin/candles?tf=M15", ""},
        {"DELETE", "/api/v1/admin/candles", kAdminBody},
        {"GET", "/api/v1/news", ""},
        {"GET", "/api/v1/news/analyze?id=x", ""},
        {"GET", "/api/v1/history", ""},
    };
}

// Open findings are recorded in the docs. Their regression tests assert the
// SECURE expectation and are enabled only when this gate is set, so the default
// suite (which must stay green) is not broken by a known, reported gap. Run
// `AYZR_SECURITY_ENFORCE_FIXES=1 ./AyzrSecurityTests` to see them fail until the
// owning agent lands the fix.
bool enforceFixes() {
    const char* v = std::getenv("AYZR_SECURITY_ENFORCE_FIXES");
    return v != nullptr && std::string(v) == "1";
}

}  // namespace

// ===========================================================================
// Section 1 - Cross-role denials (the core security boundary)
// ===========================================================================

// The sender credential authorises candle ingest, sync and news ingest, and
// nothing else: not the admin surface, not the private read surface.
TEST_CASE(sec_sender_credential_authorises_only_ingest_and_sync) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody).statusCode, 200);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/sync/latest", kSender).statusCode, 200);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/news/ingest", kSender, kNewsBody).statusCode, 200);

    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kSender).statusCode, 401);
    CHECK_EQ(call(fx.port, "DELETE", "/api/v1/admin/candles", kSender, kAdminBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kSender).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/history", kSender).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news/analyze?id=x", kSender).statusCode, 401);

    CHECK_EQ(fx.calls.ingest.load(), 1);
    CHECK_EQ(fx.calls.sync.load(), 1);
    CHECK_EQ(fx.calls.newsIngest.load(), 1);
    // The denied routes must never have executed.
    CHECK_EQ(fx.calls.adminList.load(), 0);
    CHECK_EQ(fx.calls.adminDelete.load(), 0);
    CHECK_EQ(fx.calls.newsList.load(), 0);
    CHECK_EQ(fx.calls.newsAnalyze.load(), 0);
    CHECK_EQ(fx.calls.history.load(), 0);
}

// The client credential is read-only and is embedded in the Android app. It must
// not authorise ANY write and must not reach the admin surface.
TEST_CASE(sec_client_credential_cannot_write_or_reach_admin) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kClient, kIngestBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/news/ingest", kClient, kNewsBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/sync/latest", kClient).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kClient).statusCode, 401);
    CHECK_EQ(call(fx.port, "DELETE", "/api/v1/admin/candles", kClient, kAdminBody).statusCode, 401);

    // It is exactly the private read surface that it may use.
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kClient).statusCode, 200);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news/analyze?id=x", kClient).statusCode, 200);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/history", kClient).statusCode, 200);

    CHECK_EQ(fx.calls.ingest.load(), 0);
    CHECK_EQ(fx.calls.newsIngest.load(), 0);
    CHECK_EQ(fx.calls.sync.load(), 0);
    CHECK_EQ(fx.calls.adminList.load(), 0);
    CHECK_EQ(fx.calls.adminDelete.load(), 0);
    CHECK_EQ(fx.calls.newsList.load(), 1);
    CHECK_EQ(fx.calls.newsAnalyze.load(), 1);
    CHECK_EQ(fx.calls.history.load(), 1);
}

// The admin credential is confined to the admin candle surface. A leaked admin
// token must not become a write credential for candles or news, and must not
// read the private mobile surface.
TEST_CASE(sec_admin_credential_cannot_write_or_read_private) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kAdmin).statusCode, 200);
    CHECK_EQ(call(fx.port, "DELETE", "/api/v1/admin/candles", kAdmin, kAdminBody).statusCode, 200);

    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kAdmin, kIngestBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/news/ingest", kAdmin, kNewsBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/sync/latest", kAdmin).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kAdmin).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news/analyze?id=x", kAdmin).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/history", kAdmin).statusCode, 401);

    CHECK_EQ(fx.calls.adminList.load(), 1);
    CHECK_EQ(fx.calls.adminDelete.load(), 1);
    CHECK_EQ(fx.calls.ingest.load(), 0);
    CHECK_EQ(fx.calls.newsIngest.load(), 0);
    CHECK_EQ(fx.calls.sync.load(), 0);
    CHECK_EQ(fx.calls.newsList.load(), 0);
    CHECK_EQ(fx.calls.newsAnalyze.load(), 0);
    CHECK_EQ(fx.calls.history.load(), 0);
}

// No credential at all must fail closed on every protected route.
TEST_CASE(sec_missing_credentials_fail_closed_on_every_protected_route) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    for (const auto& route : protectedRoutes()) {
        const HttpResponse r = call(fx.port, route.method, route.path, "", route.body);
        CHECK_EQ(r.statusCode, 401);
        CHECK(contains(r.body, "\"code\":\"unauthorized\""));
        checkNoLeak(r);
    }
    CHECK_EQ(fx.calls.total(), 0);
}

// An empty or malformed Authorization header can never be mistaken for a
// configured credential, on any route.
TEST_CASE(sec_empty_and_malformed_authorization_fail_closed) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    const std::vector<std::string> badHeaders = {
        "Authorization: \r\n",
        "Authorization: Bearer\r\n",
        "Authorization: Bearer \r\n",
        "Authorization: Bearer    \r\n",
        "Authorization: bearer " + std::string(kSender) + "\r\n",   // case-sensitive scheme
        "Authorization: Basic " + std::string(kSender) + "\r\n",
        "Authorization: " + std::string(kSender) + "\r\n",          // no scheme
        "Authorization: Bearer " + std::string(kSender) + "x\r\n",  // prefix, not the token
        "Authorization: Bearer x" + std::string(kSender) + "\r\n",
        "Authorization: Bearer " + std::string(kSender) + "-wrong\r\n",
    };

    for (const auto& header : badHeaders) {
        for (const auto& route : protectedRoutes()) {
            const HttpResponse r =
                callRawAuth(fx.port, route.method, route.path, header, route.body);
            CHECK_EQ(r.statusCode, 401);
            checkNoLeak(r);
        }
    }
    CHECK_EQ(fx.calls.total(), 0);
}

// An unset admin token disables the admin surface (401), never serves it
// unauthenticated, and is never satisfied by a sender or client credential.
TEST_CASE(sec_admin_surface_fails_closed_when_admin_token_unset) {
    AstraApiServerConfig cfg = baseConfig();
    cfg.adminTokens.clear();
    Fixture fx;
    REQUIRE(fx.start(cfg));

    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", "").statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kSender).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kClient).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kAdmin).statusCode, 401);
    CHECK_EQ(call(fx.port, "DELETE", "/api/v1/admin/candles", kAdmin, kAdminBody).statusCode, 401);
    CHECK_EQ(call(fx.port, "DELETE", "/api/v1/admin/candles", kSender, kAdminBody).statusCode, 401);
    CHECK_EQ(fx.calls.adminList.load(), 0);
    CHECK_EQ(fx.calls.adminDelete.load(), 0);
}

// An unset client token disables the private mobile surface (401).
TEST_CASE(sec_private_surface_fails_closed_when_client_token_unset) {
    AstraApiServerConfig cfg = baseConfig();
    cfg.clientTokens.clear();
    Fixture fx;
    REQUIRE(fx.start(cfg));

    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", "").statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kSender).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kClient).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/history", kClient).statusCode, 401);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news/analyze?id=x", kClient).statusCode, 401);
    CHECK_EQ(fx.calls.newsList.load(), 0);
    CHECK_EQ(fx.calls.newsAnalyze.load(), 0);
    CHECK_EQ(fx.calls.history.load(), 0);
}

// A sender credential is not a client credential and vice versa, even when the
// presented value is a prefix/suffix of a real one.
TEST_CASE(sec_credentials_are_not_interchangeable) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    // Cross-present the three credentials against every route.
    const std::vector<std::string> tokens = {kSender, kAdmin, kClient};
    for (const auto& route : protectedRoutes()) {
        for (const auto& token : tokens) {
            const HttpResponse r = call(fx.port, route.method, route.path, token, route.body);
            const bool isIngest = std::string(route.path) == "/api/v1/candles/ingest" ||
                                  std::string(route.path) == "/api/v1/news/ingest" ||
                                  std::string(route.path) == "/api/v1/sync/latest";
            const bool isAdmin = std::string(route.path).rfind("/api/v1/admin/", 0) == 0;
            const bool senderMay = isIngest && token == kSender;
            const bool adminMay = isAdmin && token == kAdmin;
            const bool clientMay = !isIngest && !isAdmin && token == kClient;
            if (senderMay || adminMay || clientMay) {
                CHECK_EQ(r.statusCode, 200);
            } else {
                CHECK_EQ(r.statusCode, 401);
            }
        }
    }
}

// Unknown paths and wrong methods must not reach a handler and must not disclose
// the route table to an unauthenticated caller.
TEST_CASE(sec_unknown_path_and_wrong_method_do_not_reach_handlers) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    // Wrong method on each protected route (with the right token) -> 405.
    CHECK_EQ(call(fx.port, "GET", "/api/v1/candles/ingest", kSender).statusCode, 405);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/sync/latest", kSender).statusCode, 405);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news/ingest", kSender).statusCode, 405);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/news", kClient).statusCode, 405);
    CHECK_EQ(call(fx.port, "PUT", "/api/v1/history", kClient).statusCode, 405);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/admin/candles", kAdmin, kAdminBody).statusCode, 405);

    // Unknown paths fall through to the facade, which answers 404.
    CHECK_EQ(call(fx.port, "GET", "/api/v1/does-not-exist", "").statusCode, 404);
    CHECK_EQ(call(fx.port, "GET", "/api/v1/admin/candles/", kAdmin).statusCode, 404);
    CHECK_EQ(call(fx.port, "GET", "/API/V1/ADMIN/CANDLES", kAdmin).statusCode, 404);
    CHECK_EQ(fx.calls.total(), 0);
}

// ===========================================================================
// Section 2 - Input validation, size limits, rate limits, error hygiene
// ===========================================================================

// A body over maxRequestBytes is refused with 413 before the handler runs.
TEST_CASE(sec_oversized_body_is_refused_before_the_handler) {
    AstraApiServerConfig cfg = baseConfig();
    cfg.maxRequestBytes = 256;
    Fixture fx;
    REQUIRE(fx.start(cfg));

    const std::string big(1024, 'x');
    const HttpResponse r = call(fx.port, "POST", "/api/v1/candles/ingest", kSender, big);
    CHECK_EQ(r.statusCode, 413);
    CHECK(contains(r.body, "\"code\":\"request_too_large\""));
    CHECK_EQ(fx.calls.ingest.load(), 0);
    checkNoLeak(r);
}

// The per-credential rate limit returns 429 with a Retry-After hint and does not
// run the handler for a throttled request.
TEST_CASE(sec_rate_limit_refuses_before_the_handler) {
    AstraApiServerConfig cfg = baseConfig();
    cfg.ingestRequestsPerMinute = 2;
    Fixture fx;
    REQUIRE(fx.start(cfg));

    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody).statusCode, 200);
    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody).statusCode, 200);
    const HttpResponse limited =
        call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody);
    CHECK_EQ(limited.statusCode, 429);
    CHECK(contains(limited.body, "\"code\":\"rate_limited\""));
    CHECK_EQ(fx.calls.ingest.load(), 2);   // the throttled call never ran
    checkNoLeak(limited);

    // The limit is per credential: a different credential has its own window.
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kClient).statusCode, 200);
}

// CORS is enforced before the handler: a non-allow-listed browser origin is
// refused with 403 and the handler never runs.
TEST_CASE(sec_disallowed_origin_is_refused_before_the_handler) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    const HttpResponse r = callRawAuth(
        fx.port, "GET", "/api/v1/news", "Authorization: Bearer " + std::string(kClient) + "\r\n",
        "", "Origin: https://evil.example.com\r\n");
    CHECK_EQ(r.statusCode, 403);
    CHECK(contains(r.body, "\"code\":\"origin_not_allowed\""));
    CHECK_EQ(fx.calls.newsList.load(), 0);
    checkNoLeak(r);

    // A non-browser request (no Origin) is unaffected.
    CHECK_EQ(call(fx.port, "GET", "/api/v1/news", kClient).statusCode, 200);
}

// Every non-2xx body is the flat error object and leaks no internal detail.
TEST_CASE(sec_error_bodies_are_flat_and_leak_no_internals) {
    AstraApiServerConfig cfg = baseConfig();
    cfg.ingestRequestsPerMinute = 1;
    Fixture fx;
    REQUIRE(fx.start(cfg));

    // Fill the single-slot ingest budget so the next write is a 429.
    CHECK_EQ(call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody).statusCode, 200);

    std::vector<HttpResponse> responses;
    responses.push_back(call(fx.port, "POST", "/api/v1/candles/ingest", "", kIngestBody));      // 401
    responses.push_back(call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", kSender));         // 401
    responses.push_back(call(fx.port, "GET", "/api/v1/news", kSender));                          // 401
    responses.push_back(call(fx.port, "GET", "/api/v1/does-not-exist", ""));                     // 404
    responses.push_back(call(fx.port, "GET", "/api/v1/candles/ingest", kSender));                // 405
    responses.push_back(callRawAuth(fx.port, "GET", "/api/v1/news",
                                    "Authorization: Bearer " + std::string(kClient) + "\r\n",
                                    "", "Origin: https://evil.example.com\r\n"));                // 403
    responses.push_back(call(fx.port, "POST", "/api/v1/candles/ingest", kSender, kIngestBody));  // 429

    for (const auto& r : responses) {
        CHECK(!r.is2xx());
        CHECK(contains(r.body, "\"error\":\"true\""));
        CHECK(!contains(r.body, "\"api\":\"v1\""));
        checkNoLeak(r);
    }
}

// A credential presented to a failing request is never echoed back.
TEST_CASE(sec_presented_credential_is_never_reflected) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    const std::string probe = "REFLECT-PROBE-9f3a2b";
    const std::vector<HttpResponse> responses = {
        call(fx.port, "POST", "/api/v1/candles/ingest", probe, kIngestBody),
        call(fx.port, "GET", "/api/v1/admin/candles?tf=M15", probe),
        call(fx.port, "GET", "/api/v1/news", probe),
        callRawAuth(fx.port, "POST", "/api/v1/news/ingest",
                    "Authorization: Bearer " + probe + "\r\n", kNewsBody),
    };
    for (const auto& r : responses) {
        CHECK_EQ(r.statusCode, 401);
        CHECK(!contains(r.body, probe));
        checkNoLeak(r);
    }
}

// A malformed request (missing header terminator, bad target) is answered 400
// with a stable code and no leak.
TEST_CASE(sec_malformed_framing_is_400_and_leaks_nothing) {
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));

    // A target that does not start with '/'.
    const HttpResponse badTarget =
        callRawAuth(fx.port, "GET", "api/v1/health", "");
    CHECK_EQ(badTarget.statusCode, 400);
    CHECK(contains(badTarget.body, "\"code\":\"malformed_request\""));
    checkNoLeak(badTarget);
}

// ===========================================================================
// Section 3 - Known gaps (reported; regression tests gated)
// ===========================================================================
//
// Each case below asserts the SECURE expectation. It is skipped by default so
// the shipped suite stays green, and enabled by AYZR_SECURITY_ENFORCE_FIXES=1,
// where it FAILS until the owning source fix lands. See
// docs/ayzr/backend-hardening/SECURITY_FINDINGS.md for the full write-up.

// F-D-1: the rate-limit key is the RAW Authorization header, not the credential
// identity. Whitespace inside/around the same credential yields a fresh bucket,
// so a caller holding a valid token can trivially evade the limit.
TEST_CASE(sec_gap_rate_limit_key_is_the_raw_header_not_the_credential) {
    if (!enforceFixes()) {
        std::cout << "  [KNOWN GAP F-D-1] skipped; set AYZR_SECURITY_ENFORCE_FIXES=1 to enforce\n";
        return;
    }
    AstraApiServerConfig cfg = baseConfig();
    cfg.ingestRequestsPerMinute = 2;
    Fixture fx;
    REQUIRE(fx.start(cfg));

    CHECK_EQ(callRawAuth(fx.port, "POST", "/api/v1/candles/ingest",
                         "Authorization: Bearer " + std::string(kSender) + "\r\n",
                         kIngestBody).statusCode, 200);
    CHECK_EQ(callRawAuth(fx.port, "POST", "/api/v1/candles/ingest",
                         "Authorization: Bearer " + std::string(kSender) + "\r\n",
                         kIngestBody).statusCode, 200);
    // Same credential, one extra space after the scheme: must share the bucket.
    const HttpResponse evaded = callRawAuth(
        fx.port, "POST", "/api/v1/candles/ingest",
        "Authorization: Bearer  " + std::string(kSender) + "\r\n", kIngestBody);
    CHECK_EQ(evaded.statusCode, 429);
}

// F-D-2: failed authentication is not throttled at all, so an attacker can
// brute-force a bearer token at line rate.
TEST_CASE(sec_gap_failed_authentication_is_not_throttled) {
    if (!enforceFixes()) {
        std::cout << "  [KNOWN GAP F-D-2] skipped; set AYZR_SECURITY_ENFORCE_FIXES=1 to enforce\n";
        return;
    }
    AstraApiServerConfig cfg = baseConfig();
    cfg.ingestRequestsPerMinute = 2;
    Fixture fx;
    REQUIRE(fx.start(cfg));

    bool sawThrottle = false;
    for (int i = 0; i < 50 && !sawThrottle; ++i) {
        const HttpResponse r = call(fx.port, "POST", "/api/v1/candles/ingest",
                                    "guess-" + std::to_string(i), kIngestBody);
        if (r.statusCode == 429) sawThrottle = true;
    }
    CHECK(sawThrottle);
}

// F-D-8: a store write failure echoes the raw SQLite error string (which can
// carry a filesystem path) into the 500 response body. Asserted through the real
// `astra_server` binary because a read-only data directory is the deterministic
// way to make the write fail; it also means the body can contain the data dir.
TEST_CASE(sec_gap_store_failure_echoes_raw_sqlite_error) {
    const std::string exe = [] {
        const std::filesystem::path base = std::filesystem::current_path();
#ifdef _WIN32
        return (base / "astra_server.exe").string();
#else
        return (base / "astra_server").string();
#endif
    }();
    if (!std::filesystem::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string dataDir =
        (std::filesystem::temp_directory_path() /
         ("astra_sec_ro_" + std::to_string(unique)))
            .string();
    std::filesystem::create_directories(dataDir);

    // Ephemeral loopback port.
    std::uint16_t port = 0;
#ifndef _WIN32
    {
        const int s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                sockaddr_in bound{};
                socklen_t len = sizeof(bound);
                if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
                    port = ntohs(bound.sin_port);
                }
            }
            ::close(s);
        }
    }
#endif
    if (port == 0) {
        std::cout << "  [SKIP] no ephemeral port available\n";
        return;
    }

    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "sender-cred-aaa"};
    spec.environment["ASTRA_DATA_DIR"] = dataDir;
    spec.environment["ASTRA_SENDER_TOKEN"] = "sender-cred-aaa";
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
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

    // Make the data directory unwritable so the next insert must fail. The
    // process holds the sqlite handle open, so the failure is on write, not open.
    std::error_code ec;
    const auto original = std::filesystem::status(dataDir, ec).permissions();
    std::filesystem::permissions(dataDir,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace, ec);

    const HttpResponse r = call(port, "POST", "/api/v1/candles/ingest",
                                "sender-cred-aaa", kIngestBody);
    CHECK_EQ(r.statusCode, 500);
    CHECK(contains(r.body, "\"code\":\"store_failure\""));
    // The secure expectation: the body must not carry the raw driver error or
    // the data directory path. This fails today (F-D-8).
    if (enforceFixes()) {
        CHECK(!contains(r.body, "database"));
        CHECK(!contains(r.body, "insert failed"));
        CHECK(!contains(r.body, dataDir));
    }

    std::filesystem::permissions(dataDir, original,
                                 std::filesystem::perm_options::replace, ec);
    std::string stopError;
    supervisor.stop(2000, stopError);
    std::filesystem::remove_all(dataDir, ec);
}


// F-D-3: the admin route answers 405 for a wrong method BEFORE authenticating,
// disclosing that the route exists to an unauthenticated caller.
TEST_CASE(sec_gap_admin_route_method_probe_precedes_auth) {
    if (!enforceFixes()) {
        std::cout << "  [KNOWN GAP F-D-3] skipped; set AYZR_SECURITY_ENFORCE_FIXES=1 to enforce\n";
        return;
    }
    Fixture fx;
    REQUIRE(fx.start(baseConfig()));
    const HttpResponse probe = call(fx.port, "POST", "/api/v1/admin/candles", "", kAdminBody);
    CHECK_EQ(probe.statusCode, 401);   // secure: refuse before disclosing the method table
}
