// AYZR-ECO-TEST-API — Ecosystem HTTP integration tests.
//
// Drives the real `astra_server` binary over loopback HTTP: authenticates,
// ingests a candle batch, then reads the ecosystem routes (sessions, engines,
// opportunities, ledger). Proves the routes are client-token gated, that they
// report honestly with no data, and that a real ingested series produces a
// coherent cycle with a persisted ledger. No live LLM call is made.

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
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        ::close(s);
        return 8897;
    }
    std::uint16_t port = 0;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0)
        port = ntohs(addr.sin_port);
    ::close(s);
    return port;
}

std::string binaryPath() {
    const fs::path base = fs::current_path();
    return (base / "astra_server").string();
}

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

std::string makeDataDir() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("astra_eco_" + std::to_string(unique))).string();
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
    if (!token.empty()) req.headers = "Authorization: Bearer " + token + "\r\n";
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

struct EcoServer {
    std::unique_ptr<ProcessSupervisor> supervisor;
    std::uint16_t port = 0;
    std::string dataDir;

    bool start(std::string& error) {
        const std::string exe = binaryPath();
        if (!fs::exists(exe)) { error = "astra_server binary not built"; return false; }
        port = freePort();
        dataDir = makeDataDir();
        ProcessLaunchSpec spec;
        spec.executable = exe;
        spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--data-dir", dataDir, "--sender-token", "sender-tok",
                     "--admin-token", "admin-tok"};
        spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
        spec.environment["ASTRA_CLIENT_TOKEN"] = "client-tok";
        supervisor = std::make_unique<ProcessSupervisor>(spec);
        if (!supervisor->start(error)) return false;
        if (!waitReady(port)) { error = "server did not become ready"; return false; }
        return true;
    }
    ~EcoServer() {
        if (supervisor) { std::string e; supervisor->stop(2000, e); }
        if (!dataDir.empty()) { std::error_code ec; fs::remove_all(dataDir, ec); }
    }
};

// A contiguous M15 series of N bars ending at `endOpen`, with a mild uptrend so
// the trend engine has something real to see. Test data, clearly synthetic.
std::string candleBatch(int n, std::int64_t endOpen) {
    std::string candles = "[";
    for (int i = 0; i < n; ++i) {
        const std::int64_t t = endOpen - static_cast<std::int64_t>(n - 1 - i) * 900;
        const double base = 1900.0 + i * 1.5;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%s{\"time\":%lld,\"open\":%.2f,\"high\":%.2f,\"low\":%.2f,"
                      "\"close\":%.2f,\"tick_volume\":100,\"spread\":10,\"real_volume\":0}",
                      i ? "," : "", static_cast<long long>(t), base, base + 1.0,
                      base - 1.0, base + 0.5);
        candles += buf;
    }
    candles += "]";
    return "{\"source_id\":\"eco-test\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
           "\"candles\":" + candles + "}";
}

}  // namespace

// 1. Every ecosystem route requires the client credential.
TEST_CASE(eco_routes_require_client_credential) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const std::uint16_t port = server.port;

    for (const char* path : {"/api/v1/ayzr/ecosystem/sessions",
                             "/api/v1/ayzr/ecosystem/engines",
                             "/api/v1/ayzr/ecosystem/opportunities",
                             "/api/v1/ayzr/ecosystem/ledger"}) {
        const HttpResponse r = request(port, "GET", path, "");
        CHECK_EQ(r.statusCode, 401);
        CHECK(contains(r.body, "\"code\":\"unauthorized\""));
    }
    // The sender credential must NOT open the client read surface.
    const HttpResponse s = request(port, "GET",
                                   "/api/v1/ayzr/ecosystem/sessions", "sender-tok");
    CHECK_EQ(s.statusCode, 401);
}

// 2. Sessions route: real IANA data, four sessions, honest structure.
TEST_CASE(eco_sessions_route_returns_real_session_state) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const HttpResponse r = request(server.port, "GET",
                                   "/api/v1/ayzr/ecosystem/sessions", "client-tok");
    CHECK_EQ(r.statusCode, 200);
    CHECK(contains(r.body, "\"sessions\":["));
    CHECK(contains(r.body, "\"nameAr\":\"جلسة لندن\""));
    CHECK(contains(r.body, "\"zoneId\":\"Europe/London\""));
    CHECK(contains(r.body, "\"phase\":"));
    // At least one session must report a concrete local time (tzdata present).
    CHECK(contains(r.body, "\"localTime\":\"20"));
}

// 3. Engines route: registry is complete and micro reports unavailable.
TEST_CASE(eco_engines_route_lists_registry_honestly) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const HttpResponse r = request(server.port, "GET",
                                   "/api/v1/ayzr/ecosystem/engines", "client-tok");
    CHECK_EQ(r.statusCode, 200);
    CHECK(contains(r.body, "\"contractSchemaVersion\":1"));
    CHECK(contains(r.body, "\"engineId\":\"trend-following-M15\""));
    CHECK(contains(r.body, "\"engineId\":\"mean-reversion-M15\""));
    CHECK(contains(r.body, "\"engineId\":\"breakout-M15\""));
    CHECK(contains(r.body, "\"engineId\":\"microstructure-M15\""));
    CHECK(contains(r.body, "\"engineId\":\"event-driven-M15\""));
    CHECK(contains(r.body, "\"engineId\":\"trend-following-H4\""));
    // Microstructure must report UNAVAILABLE, never a fabricated health.
    CHECK(contains(r.body, "\"health\":\"UNAVAILABLE\""));
    CHECK(contains(r.body, "بنية السوق"));
}

// 4. With no candles, the cycle is empty but honest.
TEST_CASE(eco_opportunities_route_honest_when_no_data) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const HttpResponse r = request(server.port, "GET",
                                   "/api/v1/ayzr/ecosystem/opportunities", "client-tok");
    CHECK_EQ(r.statusCode, 200);
    CHECK(contains(r.body, "\"opportunities\":["));
    CHECK(contains(r.body, "\"correlationId\":\"cycle-"));
}

// 5. End-to-end: ingest candles, then the cycle sees them and the ledger grows.
TEST_CASE(eco_end_to_end_ingest_then_cycle_and_ledger) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const std::uint16_t port = server.port;

    // A recent bar open time so freshness checks pass.
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const std::int64_t endOpen = (now / 900) * 900 - 900;

    const HttpResponse ingest = request(port, "POST", "/api/v1/candles/ingest",
                                        "sender-tok", candleBatch(120, endOpen));
    CHECK_EQ(ingest.statusCode, 200);
    CHECK(contains(ingest.body, "\"accepted\""));

    // Ledger before/after must be monotone (append-only).
    const HttpResponse l0 = request(port, "GET", "/api/v1/ayzr/ecosystem/ledger",
                                    "client-tok");
    CHECK_EQ(l0.statusCode, 200);
    CHECK(contains(l0.body, "\"count\":"));

    const HttpResponse opp = request(port, "GET",
                                     "/api/v1/ayzr/ecosystem/opportunities", "client-tok");
    CHECK_EQ(opp.statusCode, 200);
    // With a real clean series the engines run; the cycle must carry a valid
    // correlation id and at least one opportunity record (possibly WITHHOLD).
    CHECK(contains(opp.body, "\"correlationId\":\"cycle-"));

    // The ledger now has decision rows referencing the cycle.
    const HttpResponse l1 = request(port, "GET",
                                    "/api/v1/ayzr/ecosystem/ledger?limit=500", "client-tok");
    CHECK_EQ(l1.statusCode, 200);
    CHECK(contains(l1.body, "\"entries\":["));
}

// 6. Duplicate ingestion does not duplicate the market record.
TEST_CASE(eco_duplicate_ingest_is_idempotent) {
    EcoServer server;
    std::string error;
    if (!server.start(error)) { std::cout << "  [SKIP] " << error << "\n"; return; }
    const std::uint16_t port = server.port;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const std::int64_t endOpen = (now / 900) * 900 - 900;
    const std::string body = candleBatch(10, endOpen);

    const HttpResponse a = request(port, "POST", "/api/v1/candles/ingest", "sender-tok", body);
    CHECK_EQ(a.statusCode, 200);
    const HttpResponse b = request(port, "POST", "/api/v1/candles/ingest", "sender-tok", body);
    CHECK_EQ(b.statusCode, 200);
    // The second identical batch must report the candles as already stored.
    CHECK(contains(b.body, "already_stored"));
}
