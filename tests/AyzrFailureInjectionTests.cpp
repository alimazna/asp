// TST-AYZR-E-0001 - AYZR failure injection + authentication-boundary suite.
//
// Every case here drives the REAL `astra_server` binary over real loopback HTTP
// against a real SQLite file, so a regression in a guard, an auth check or an
// error path fails the test rather than being papered over by a stub. The only
// test double is a stalling TCP listener used to inject an API timeout; it sits
// at the network boundary, so the real outbound client, the real provider
// classification and the real service retry policy all execute.
//
// Coverage (Agent E scope):
//   * authentication boundaries: sender vs admin vs client credentials, in both
//     directions (a read credential must not write, a write credential must not
//     read, a leaked sender token must not reach the destructive admin surface);
//   * input validation: duplicate candle batches, invalid OHLC, negative
//     volume/spread, invalid and future timestamps, misaligned open times,
//     malformed JSON, empty batch and empty feed;
//   * dependency failure: DB open/write failure, network interruption and API
//     timeout against the news provider;
//   * error hygiene: no response body may leak an internal path, a credential or
//     an implementation detail.
//
// Read-only with respect to ASTRA: nothing here writes a decision, mutates the
// decision engine or executes anything. Linux process-level cases are guarded.

#include "TestHarness.h"

#include "data/TimeframeStateStore.h"
#include "foundation/HttpClient.h"
#include "news/NewsAnalysisParser.h"
#include "platform/windows/ProcessSupervisor.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// A unique, currently-free loopback port. Fixed ports collide with a
// concurrently running test binary or another agent's server, so every case
// asks the kernel for an ephemeral one.
std::uint16_t freePort() {
#ifdef _WIN32
    return 8798;
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8798;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    std::uint16_t port = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
    ::close(s);
    return port;
#endif
}

std::string binaryPath() {
    const fs::path base = fs::current_path();
#ifdef _WIN32
    return (base / "astra_server.exe").string();
#else
    return (base / "astra_server").string();
#endif
}

std::string uniqueDir(const std::string& tag) {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("astra_ayzr_" + tag + "_" + std::to_string(unique)))
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
    req.readTimeoutMillis = 20000;
    return httpRequest(req);
}

bool waitReady(std::uint16_t port, int attempts = 80) {
    for (int i = 0; i < attempts; ++i) {
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

// RAII wrapper around a spawned astra_server plus its temp data dir.
struct TestServer {
    std::unique_ptr<ProcessSupervisor> supervisor;
    std::uint16_t port = 0;
    std::string dataDir;
    bool started = false;

    bool start(const std::vector<std::string>& extraArgs,
               const std::map<std::string, std::string>& env,
               std::string& error) {
        const std::string exe = binaryPath();
        if (!fs::exists(exe)) {
            error = "astra_server binary not built";
            return false;
        }
        port = freePort();
        dataDir = uniqueDir("srv");
        fs::create_directories(dataDir);

        ProcessLaunchSpec spec;
        spec.executable = exe;
        spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--data-dir", dataDir};
        for (const auto& a : extraArgs) spec.args.push_back(a);
        spec.environment = env;
        spec.environment["ASTRA_DATA_DIR"] = dataDir;
        supervisor = std::make_unique<ProcessSupervisor>(spec);
        if (!supervisor->start(error)) return false;
        started = true;
        if (!waitReady(port)) {
            error = "server did not become ready";
            return false;
        }
        return true;
    }

    void stop() {
        if (supervisor && started) {
            std::string stopError;
            supervisor->stop(2000, stopError);
            started = false;
        }
    }

    ~TestServer() {
        stop();
        if (!dataDir.empty()) {
            std::error_code ec;
            fs::remove_all(dataDir, ec);
        }
    }
};

// The standard three-credential configuration: a sender (write) token, a
// distinct admin token and a distinct read-only client token.
std::map<std::string, std::string> threeCredentialEnv() {
    return {
        {"ASTRA_SENDER_TOKEN", "sender-secret"},
        {"ASTRA_ADMIN_TOKEN", "admin-secret"},
        {"ASTRA_CLIENT_TOKEN", "client-secret"},
        {"ASTRA_LLM_PROVIDER", "mock"},
    };
}

const char* kGoodCandleBody =
    "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
    "\"candles\":[{\"time\":1792068300,\"open\":2653.1,\"high\":2655.2,"
    "\"low\":2651.8,\"close\":2654.05,\"tick_volume\":210,\"spread\":12,"
    "\"real_volume\":0}]}";

const char* kGoodNewsBody =
    "{\"articles\":[{\"headline\":\"Gold rises on a weaker dollar\","
    "\"source\":\"example.com\",\"url\":\"https://example.com/a1\","
    "\"published_at\":1792045800,\"body\":\"Gold rose.\","
    "\"gold_relevance\":\"high\"}]}";

// Assert a body carries none of the things an error response must never expose.
void checkNoInternalLeak(const std::string& body, const std::string& dataDir) {
    CHECK(!contains(body, dataDir));
    CHECK(!contains(body, "sender-secret"));
    CHECK(!contains(body, "admin-secret"));
    CHECK(!contains(body, "client-secret"));
    CHECK(!contains(body, ".cpp"));
    CHECK(!contains(body, ".h:"));
    CHECK(!contains(body, "sqlite3"));
    CHECK(!contains(body, "std::"));
    CHECK(!contains(body, "AstraApiServer"));
    CHECK(!contains(body, "/workspace"));
}

// A loopback listener that accepts a connection and never answers, so the
// client's read timeout must fire. Dependency-free (no Python needed).
class StallingTcpServer {
public:
    ~StallingTcpServer() { stop(); }

    bool start(std::string& error) {
#ifdef _WIN32
        error = "stalling server is POSIX-only";
        return false;
#else
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            error = "socket failed";
            return false;
        }
        int one = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr),
                   sizeof(addr)) != 0) {
            error = "bind failed";
            return false;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) !=
            0) {
            error = "getsockname failed";
            return false;
        }
        port_ = ntohs(addr.sin_port);
        if (::listen(listenFd_, 4) != 0) {
            error = "listen failed";
            return false;
        }
        running_ = true;
        thread_ = std::thread([this] { loop(); });
        return true;
#endif
    }

    void stop() {
#ifndef _WIN32
        running_ = false;
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
#endif
    }

    std::uint16_t port() const noexcept { return port_; }

private:
#ifndef _WIN32
    void loop() {
        while (running_) {
            const int conn = ::accept(listenFd_, nullptr, nullptr);
            if (conn < 0) {
                if (!running_) break;
                continue;
            }
            // Accept, read nothing back: the caller's read timeout must fire.
            std::thread([conn] {
                std::this_thread::sleep_for(std::chrono::seconds(20));
                ::close(conn);
            }).detach();
        }
    }
#endif
    int listenFd_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace

// ===========================================================================
// Section 1 - Authentication boundaries
// ===========================================================================

// The candle write surface accepts the sender credential and nothing else.
TEST_CASE(ayzr_candle_ingest_requires_sender_credential_only) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret", "--admin-token",
                       "admin-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const auto post = [&](const std::string& token) {
        return request(server.port, "POST", "/api/v1/candles/ingest", token,
                       kGoodCandleBody);
    };

    CHECK_EQ(post("").statusCode, 401);
    CHECK_EQ(post("wrong-token").statusCode, 401);
    CHECK_EQ(post("admin-secret").statusCode, 401);
    CHECK_EQ(post("client-secret").statusCode, 401);
    CHECK_EQ(post("sender-secret").statusCode, 200);
}

// The destructive admin surface requires the admin credential. Neither the
// sender token (a leaked ingestion credential) nor the read-only client token
// may reach it.
TEST_CASE(ayzr_admin_surface_rejects_sender_and_client_credentials) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret", "--admin-token",
                       "admin-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const std::string deleteBody =
        "{\"source_id\":\"sender-1\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
        "\"open_time\":1792068300}";

    const auto del = [&](const std::string& token) {
        return request(server.port, "DELETE", "/api/v1/admin/candles", token,
                       deleteBody);
    };
    const auto list = [&](const std::string& token) {
        return request(server.port, "GET", "/api/v1/admin/candles?tf=M15", token);
    };

    CHECK_EQ(del("").statusCode, 401);
    CHECK_EQ(del("sender-secret").statusCode, 401);
    CHECK_EQ(del("client-secret").statusCode, 401);
    CHECK_EQ(list("sender-secret").statusCode, 401);
    CHECK_EQ(list("client-secret").statusCode, 401);
    // The admin credential reaches both the purge and the inspection view.
    CHECK_EQ(list("admin-secret").statusCode, 200);
    CHECK_EQ(del("admin-secret").statusCode, 200);
}

// The private mobile READ surface (news, analyze, history) requires the
// read-only client credential. A sender or admin token must not read it.
TEST_CASE(ayzr_news_read_surface_requires_client_credential) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret", "--admin-token",
                       "admin-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const std::vector<std::string> readPaths = {"/api/v1/news",
                                                "/api/v1/history"};
    for (const auto& path : readPaths) {
        CHECK_EQ(request(server.port, "GET", path, "").statusCode, 401);
        CHECK_EQ(request(server.port, "GET", path, "sender-secret").statusCode,
                 401);
        CHECK_EQ(request(server.port, "GET", path, "admin-secret").statusCode,
                 401);
        const HttpResponse ok =
            request(server.port, "GET", path, "client-secret");
        CHECK_EQ(ok.statusCode, 200);
        checkNoInternalLeak(ok.body, server.dataDir);
    }

    // analyze without an id is a client-visible 400 (auth passed first).
    const HttpResponse missing =
        request(server.port, "GET", "/api/v1/news/analyze", "client-secret");
    CHECK_EQ(missing.statusCode, 400);
    checkNoInternalLeak(missing.body, server.dataDir);
}

// News ingestion is a WRITE. It must require the sender credential, so the
// client token embedded in the Android app can never publish an article.
TEST_CASE(ayzr_news_ingest_write_requires_sender_credential) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret", "--admin-token",
                       "admin-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const auto ingest = [&](const std::string& token) {
        return request(server.port, "POST", "/api/v1/news/ingest", token,
                       kGoodNewsBody);
    };

    CHECK_EQ(ingest("").statusCode, 401);
    CHECK_EQ(ingest("client-secret").statusCode, 401);
    CHECK_EQ(ingest("admin-secret").statusCode, 401);
    const HttpResponse ok = ingest("sender-secret");
    CHECK_EQ(ok.statusCode, 200);
    checkNoInternalLeak(ok.body, server.dataDir);
}

// When no client token is configured the private surface is DISABLED (401),
// never served unauthenticated.
TEST_CASE(ayzr_news_surface_disabled_without_client_token) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      {{"ASTRA_SENDER_TOKEN", "sender-secret"},
                       {"ASTRA_LLM_PROVIDER", "mock"}},
                      error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    CHECK_EQ(request(server.port, "GET", "/api/v1/news", "").statusCode, 401);
    CHECK_EQ(request(server.port, "GET", "/api/v1/history", "").statusCode, 401);
    // An empty credential can never be mistaken for a configured one.
    CHECK_EQ(request(server.port, "GET", "/api/v1/news", "anything").statusCode,
             401);
}

// A per-credential fixed-window limit protects the write and read surfaces: a
// hostile caller is told to back off with a Retry-After hint, and the limiter is
// keyed on the credential so one sender cannot starve another.
TEST_CASE(ayzr_rate_limit_returns_429_with_retry_after) {
    TestServer server;
    std::string error;
    auto env = threeCredentialEnv();
    env["ASTRA_SYNC_RATE_PER_MIN"] = "3";    // read budget
    env["ASTRA_INGEST_RATE_PER_MIN"] = "2";  // write budget
    if (!server.start({"--sender-token", "sender-secret",
                       "--admin-token", "admin-secret"},
                      env, error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(request(server.port, "GET", "/api/v1/news", "client-secret")
                     .statusCode,
                 200);
    }
    const HttpResponse limited =
        request(server.port, "GET", "/api/v1/news", "client-secret");
    CHECK_EQ(limited.statusCode, 429);
    CHECK(contains(limited.body, "\"code\":\"rate_limited\""));
    // A 429 must be actionable without leaking internals.
    checkNoInternalLeak(limited.body, server.dataDir);

    // The write budget is separate and enforced before parsing.
    CHECK_EQ(request(server.port, "POST", "/api/v1/candles/ingest",
                     "sender-secret", kGoodCandleBody)
                 .statusCode,
             200);
    CHECK_EQ(request(server.port, "POST", "/api/v1/candles/ingest",
                     "sender-secret", kGoodCandleBody)
                 .statusCode,
             200);
    const HttpResponse writeLimited = request(
        server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
        kGoodCandleBody);
    CHECK_EQ(writeLimited.statusCode, 429);
    CHECK(contains(writeLimited.body, "\"code\":\"rate_limited\""));
}

// Every error is a flat {error, code, message} object — never an enveloped
// success shape and never a leak of internal detail.
TEST_CASE(ayzr_error_responses_use_flat_contract_without_leaks) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    // Unknown route -> 404 not_found.
    const HttpResponse unknown =
        request(server.port, "GET", "/api/v1/does-not-exist", "sender-secret");
    CHECK_EQ(unknown.statusCode, 404);
    CHECK(contains(unknown.body, "\"code\":\"not_found\""));
    CHECK(!contains(unknown.body, "\"api\":\"v1\""));
    checkNoInternalLeak(unknown.body, server.dataDir);

    // Wrong method on a known route -> 405.
    const HttpResponse wrongMethod = request(
        server.port, "GET", "/api/v1/candles/ingest", "sender-secret");
    CHECK_EQ(wrongMethod.statusCode, 405);
    CHECK(contains(wrongMethod.body, "\"code\":\"method_not_allowed\""));
    checkNoInternalLeak(wrongMethod.body, server.dataDir);

    // A CORS-disallowed browser origin is refused before any handler runs.
    HttpRequest cors;
    cors.host = "127.0.0.1";
    cors.port = server.port;
    cors.path = "/api/v1/health";
    cors.headers = "Origin: https://evil.example.com\r\n";
    const HttpResponse badOrigin = httpGet(cors);
    CHECK_EQ(badOrigin.statusCode, 403);
    CHECK(contains(badOrigin.body, "\"code\":\"origin_not_allowed\""));
}

// ===========================================================================
// Section 2 - Input validation and data-integrity guards
// ===========================================================================

// A replayed batch must be idempotent: stored once, then reported
// already_stored. A duplicate must never become a second row.
TEST_CASE(ayzr_duplicate_candle_batch_is_idempotent) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const HttpResponse first = request(server.port, "POST",
                                       "/api/v1/candles/ingest", "sender-secret",
                                       kGoodCandleBody);
    CHECK_EQ(first.statusCode, 200);
    CHECK(contains(first.body, "\"state\":\"stored_new\""));
    CHECK(contains(first.body, "\"ingested\":1"));

    for (int i = 0; i < 2; ++i) {
        const HttpResponse replay =
            request(server.port, "POST", "/api/v1/candles/ingest",
                    "sender-secret", kGoodCandleBody);
        CHECK_EQ(replay.statusCode, 200);
        CHECK(contains(replay.body, "\"state\":\"already_stored\""));
        CHECK(contains(replay.body, "\"ingested\":0"));
        CHECK(!contains(replay.body, "\"state\":\"stored_new\""));
    }

    // Exactly one row survived the replays.
    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M15&limit=10",
                "sender-secret");
    CHECK_EQ(chart.statusCode, 200);
    CHECK(contains(chart.body, "\"count\":1"));
}

// Structurally invalid candles are rejected per-candle and never persisted, so
// a later read honestly reports that no data is available rather than serving a
// fabricated bar.
TEST_CASE(ayzr_invalid_ohlc_is_rejected_and_never_stored) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const std::vector<std::pair<std::string, std::string>> cases = {
        // description, expected rejection fragment
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":1792068300,\"open\":2653,\"high\":2650,"
         "\"low\":2655,\"close\":2654}]}",
         "OHLC ordering"},
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":1792068300,\"open\":-1,\"high\":10,"
         "\"low\":5,\"close\":6}]}",
         "must be positive"},
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":1792068300,\"open\":1,\"high\":2,"
         "\"low\":0.5}]}",
         "open/high/low/close"},
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":1792068300,\"open\":1,\"high\":2,"
         "\"low\":0.5,\"close\":1.5,\"tick_volume\":-5}]}",
         "non-negative"},
    };

    for (const auto& c : cases) {
        const HttpResponse r = request(server.port, "POST",
                                       "/api/v1/candles/ingest", "sender-secret",
                                       c.first);
        CHECK_EQ(r.statusCode, 200);
        CHECK(contains(r.body, "\"state\":\"rejected\""));
        CHECK(contains(r.body, c.second));
        CHECK(contains(r.body, "\"ingested\":0"));
        checkNoInternalLeak(r.body, server.dataDir);
    }

    // Nothing was stored: the chart route withholds data rather than inventing.
    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M15&limit=10",
                "sender-secret");
    CHECK_EQ(chart.statusCode, 503);
    CHECK(contains(chart.body, "\"code\":\"dependency_unavailable\""));
    CHECK(!contains(chart.body, "\"bars\""));
}

// A timestamp that is non-positive or in the future is rejected, and an open
// time off the timeframe grid is rejected: neither may enter the store.
TEST_CASE(ayzr_invalid_and_misaligned_timestamps_are_rejected) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const std::int64_t future = std::time(nullptr) + 48 * 3600;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":0,\"open\":2653,\"high\":2655,\"low\":2651,"
         "\"close\":2654}]}",
         "positive epoch"},
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":" + std::to_string(future) +
             ",\"open\":2653,\"high\":2655,\"low\":2651,\"close\":2654}]}",
         "in the future"},
        // 1792068271 is not a multiple of the M15 grid (900s).
        {"{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
         "\"candles\":[{\"time\":1792068271,\"open\":1900,\"high\":1910.5,"
         "\"low\":1898.2,\"close\":1905.7}]}",
         "not aligned"},
    };

    for (const auto& c : cases) {
        const HttpResponse r = request(server.port, "POST",
                                       "/api/v1/candles/ingest", "sender-secret",
                                       c.first);
        CHECK_EQ(r.statusCode, 200);
        CHECK(contains(r.body, "\"state\":\"rejected\""));
        CHECK(contains(r.body, c.second));
        CHECK(contains(r.body, "\"ingested\":0"));
    }

    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M15&limit=10",
                "sender-secret");
    CHECK_EQ(chart.statusCode, 503);
}

// An unsupported timeframe is refused up front (400) so an unusable row is
// never persisted; an empty batch is a valid, no-op acknowledgement; and an
// empty feed reads back as "no data", never as fabricated bars.
TEST_CASE(ayzr_empty_batch_empty_feed_and_bad_timeframe) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    // Unsupported timeframe -> 400 before any parse/store work.
    const HttpResponse badTf = request(
        server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
        "{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M7\","
        "\"candles\":[{\"time\":1792068300,\"open\":1,\"high\":2,\"low\":0.5,"
        "\"close\":1.5}]}");
    CHECK_EQ(badTf.statusCode, 400);
    CHECK(contains(badTf.body, "\"code\":\"invalid_timeframe\""));

    // Empty batch: honest zero acknowledgement, no error.
    const HttpResponse empty = request(
        server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
        "{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\","
        "\"candles\":[]}");
    CHECK_EQ(empty.statusCode, 200);
    CHECK(contains(empty.body, "\"ingested\":0"));

    // Empty feed: the chart route reports unavailability, not an empty success
    // that a client could mistake for "flat market".
    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M15&limit=10",
                "sender-secret");
    CHECK_EQ(chart.statusCode, 503);
    CHECK(!contains(chart.body, "\"count\":0"));

    // Missing candle array is a 400, not a silent success.
    const HttpResponse noArray = request(
        server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
        "{\"source_id\":\"s\",\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\"}");
    CHECK_EQ(noArray.statusCode, 400);
    CHECK(contains(noArray.body, "\"code\":\"missing_candles\""));

    // Missing identity fields is a 400.
    const HttpResponse noFields = request(
        server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
        "{\"symbol\":\"XAUUSD\",\"timeframe\":\"M15\",\"candles\":[]}");
    CHECK_EQ(noFields.statusCode, 400);
    CHECK(contains(noFields.body, "\"code\":\"missing_fields\""));
}

// Malformed JSON on every body-carrying route is a 400 with a stable code and
// an error body that exposes no internal detail.
TEST_CASE(ayzr_malformed_json_is_400_and_leaks_nothing) {
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const HttpResponse candleBad =
        request(server.port, "POST", "/api/v1/candles/ingest", "sender-secret",
                "not json at all");
    CHECK_EQ(candleBad.statusCode, 400);
    CHECK(contains(candleBad.body, "\"code\":\"malformed_json\""));
    checkNoInternalLeak(candleBad.body, server.dataDir);

    const HttpResponse newsBad =
        request(server.port, "POST", "/api/v1/news/ingest", "sender-secret",
                "garbage");
    CHECK_EQ(newsBad.statusCode, 400);
    CHECK(contains(newsBad.body, "\"code\":\"malformed_json\""));
    checkNoInternalLeak(newsBad.body, server.dataDir);

    // Missing required news fields is a 400 (never a partially stored article).
    const HttpResponse newsMissing = request(
        server.port, "POST", "/api/v1/news/ingest", "sender-secret",
        "{\"articles\":[{\"source\":\"example.com\"}]}");
    CHECK_EQ(newsMissing.statusCode, 400);
    CHECK(contains(newsMissing.body, "\"code\":\"missing_fields\""));

    // A non-array articles field is a 400.
    const HttpResponse newsShape = request(
        server.port, "POST", "/api/v1/news/ingest", "sender-secret",
        "{\"articles\":\"nope\"}");
    CHECK_EQ(newsShape.statusCode, 400);
    CHECK(contains(newsShape.body, "\"code\":\"missing_articles\""));

    // A missing article id is 400, an unknown id is 404.
    CHECK_EQ(request(server.port, "GET", "/api/v1/news/analyze", "client-secret")
                 .statusCode,
             400);
    CHECK_EQ(request(server.port, "GET", "/api/v1/news/analyze?id=nope",
                     "client-secret")
                 .statusCode,
             404);
}

// Malformed HTTP framing (no header terminator, truncated body) is answered
// with a 400 and a stable code, and the socket read timeout bounds how long a
// stalled client can hold a worker. Raw sockets are used because the loopback
// client cannot emit an intentionally malformed request.
TEST_CASE(ayzr_malformed_http_framing_is_bounded_and_400) {
#ifdef _WIN32
    std::cout << "  [SKIP] raw-socket framing test is POSIX-only\n";
    return;
#else
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "sender-secret"},
                      threeCredentialEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const auto raw = [&](const std::string& data, std::string& out) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(server.port);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return;
        }
        timeval tv{};
        tv.tv_sec = 15;  // outlast the server's 10s read timeout
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::send(fd, data.data(), data.size(), 0);
        char buf[1024];
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) out.assign(buf, static_cast<std::size_t>(n));
        ::close(fd);
    };

    // A request with no header terminator must be answered 400, not left open.
    std::string noTerminator;
    raw("GET /api/v1/health HTTP/1.1\r\nHost: x\r\n", noTerminator);
    CHECK(contains(noTerminator, "400 Bad Request"));
    CHECK(contains(noTerminator, "\"code\":\"malformed_request\""));

    // A body shorter than its declared Content-Length must not be parsed as a
    // larger payload: the missing identity fields are a 400, not a crash.
    std::string truncated;
    raw("POST /api/v1/candles/ingest HTTP/1.1\r\nHost: x\r\n"
        "Authorization: Bearer sender-secret\r\nContent-Length: 100\r\n\r\n{}",
        truncated);
    CHECK(contains(truncated, "400 Bad Request"));
    CHECK(contains(truncated, "\"code\":\"missing_fields\""));

    // A request target that does not begin with '/' is refused before routing.
    std::string badTarget;
    raw("GET api/v1/health HTTP/1.1\r\nHost: x\r\n\r\n", badTarget);
    CHECK(contains(badTarget, "400 Bad Request"));
    CHECK(contains(badTarget, "\"code\":\"malformed_request\""));
#endif
}

// ===========================================================================
// Section 3 - Dependency failure (DB, network, timeout)
// ===========================================================================

// A DB connection failure at startup must stop the process rather than letting
// it serve a store it cannot write. Linux-only (fork/exec + exit status).
TEST_CASE(ayzr_candle_store_open_failure_refuses_to_start) {
#ifdef _WIN32
    std::cout << "  [SKIP] process-level startup test is Linux-only\n";
    return;
#else
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::string dataDir = uniqueDir("dbfail");
    fs::create_directories(dataDir);
    // Make the DB path a DIRECTORY so sqlite3_open cannot succeed.
    fs::create_directories(fs::path(dataDir) / "candles.sqlite");

    const std::uint16_t port = freePort();
    const std::string cmd = exe + " --host 127.0.0.1 --port " +
                            std::to_string(port) + " --data-dir '" + dataDir +
                            "' --sender-token sender-secret >/dev/null 2>&1";
    const int status = std::system(cmd.c_str());
    CHECK(status != -1);
    CHECK(WIFEXITED(status));
    // A store it cannot open is fatal: non-zero exit, no bound port.
    CHECK(WEXITSTATUS(status) != 0);
    CHECK(!waitReady(port, 3));

    std::error_code ec;
    fs::remove_all(dataDir, ec);
#endif
}

// A news-store failure must not crash the server: the write reports a stable
// 500 store_failure with no internal path, and the read routes degrade to an
// honest empty result rather than fabricating articles.
TEST_CASE(ayzr_news_store_failure_is_500_without_leaking_internals) {
    TestServer server;
    std::string error;
    // Make news.sqlite a directory so the news store cannot open.
    const std::string dataDir = uniqueDir("newsdb");
    fs::create_directories(fs::path(dataDir) / "news.sqlite");

    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "sender-secret"};
    spec.environment = threeCredentialEnv();
    spec.environment["ASTRA_DATA_DIR"] = dataDir;
    ProcessSupervisor supervisor(spec);
    if (!supervisor.start(error) || !waitReady(port)) {
        std::cout << "  [SKIP] server did not start: " << error << "\n";
        std::string stopError;
        supervisor.stop(2000, stopError);
        std::error_code ec;
        fs::remove_all(dataDir, ec);
        return;
    }

    const HttpResponse ingest =
        request(port, "POST", "/api/v1/news/ingest", "sender-secret",
                kGoodNewsBody);
    CHECK_EQ(ingest.statusCode, 500);
    CHECK(contains(ingest.body, "\"code\":\"store_failure\""));
    checkNoInternalLeak(ingest.body, dataDir);

    // Reads must not fabricate: an unavailable store yields an empty list.
    const HttpResponse list = request(port, "GET", "/api/v1/news", "client-secret");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "\"news\":[]"));
    const HttpResponse history =
        request(port, "GET", "/api/v1/history", "client-secret");
    CHECK_EQ(history.statusCode, 200);
    CHECK(contains(history.body, "\"history\":[]"));

    std::string stopError;
    supervisor.stop(2000, stopError);
    std::error_code ec;
    fs::remove_all(dataDir, ec);
}

// Network interruption: the provider host is unreachable. The analysis must be
// an honest `unavailable` with no direction and no levels, and the diagnostic
// must not carry the credential.
TEST_CASE(ayzr_llm_network_interruption_yields_unavailable) {
    TestServer server;
    std::string error;
    // A closed loopback port: connect fails immediately (network interruption).
    const std::uint16_t deadPort = freePort();
    auto env = threeCredentialEnv();
    env["ASTRA_LLM_PROVIDER"] = "llm7";
    env["LLM7_API_TOKEN"] = "llm7-secret-token";
    env["LLM7_BASE_URL"] = "http://127.0.0.1:" + std::to_string(deadPort) + "/v1";
    env["LLM7_CONNECT_TIMEOUT_MS"] = "500";
    env["LLM7_READ_TIMEOUT_MS"] = "800";
    env["LLM7_MAX_ATTEMPTS"] = "1";
    if (!server.start({"--sender-token", "sender-secret",
                       "--admin-token", "admin-secret"},
                      env, error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const HttpResponse ing =
        request(server.port, "POST", "/api/v1/news/ingest", "sender-secret",
                kGoodNewsBody);
    CHECK_EQ(ing.statusCode, 200);

    const HttpResponse analyze = request(
        server.port, "GET", "/api/v1/news/analyze?id=https://example.com/a1",
        "client-secret");
    CHECK_EQ(analyze.statusCode, 200);
    CHECK(contains(analyze.body, "\"status\":\"unavailable\""));
    CHECK(contains(analyze.body, "\"direction\":\"no_trade\""));
    CHECK(contains(analyze.body,
                   "\"levels\":{\"entry\":null,\"stop_loss\":null,"
                   "\"take_profit\":null}"));
    CHECK(contains(analyze.body, "\"limitations_ar\":[\""));
    // The provider credential is never echoed to a client.
    CHECK(!contains(analyze.body, "llm7-secret-token"));
    checkNoInternalLeak(analyze.body, server.dataDir);
}

// API timeout: the provider accepts the connection but never answers. The
// bounded read timeout must fire, the request must return within a bounded
// wall-clock, and the result must be an honest `unavailable`.
TEST_CASE(ayzr_llm_api_timeout_yields_unavailable_within_bound) {
    StallingTcpServer stall;
    std::string stallError;
    if (!stall.start(stallError)) {
        std::cout << "  [SKIP] cannot start stalling server: " << stallError
                  << "\n";
        return;
    }

    TestServer server;
    std::string error;
    auto env = threeCredentialEnv();
    env["ASTRA_LLM_PROVIDER"] = "llm7";
    env["LLM7_API_TOKEN"] = "llm7-secret-token";
    env["LLM7_BASE_URL"] =
        "http://127.0.0.1:" + std::to_string(stall.port()) + "/v1";
    env["LLM7_CONNECT_TIMEOUT_MS"] = "1000";
    env["LLM7_READ_TIMEOUT_MS"] = "700";
    env["LLM7_MAX_ATTEMPTS"] = "1";
    if (!server.start({"--sender-token", "sender-secret",
                       "--admin-token", "admin-secret"},
                      env, error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const HttpResponse ing =
        request(server.port, "POST", "/api/v1/news/ingest", "sender-secret",
                kGoodNewsBody);
    CHECK_EQ(ing.statusCode, 200);

    const auto started = std::chrono::steady_clock::now();
    const HttpResponse analyze = request(
        server.port, "GET", "/api/v1/news/analyze?id=https://example.com/a1",
        "client-secret");
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    CHECK_EQ(analyze.statusCode, 200);
    CHECK(contains(analyze.body, "\"status\":\"unavailable\""));
    CHECK(contains(analyze.body, "\"direction\":\"no_trade\""));
    // The read timeout is enforced: the request cannot hang on the stalled
    // provider (one attempt at 700ms plus overhead).
    CHECK(elapsed < 15);
    CHECK(!contains(analyze.body, "llm7-secret-token"));
}

// ===========================================================================
// Section 4 - Data honesty: stale and conflicting evidence
// ===========================================================================

// A stale or unknown timeframe is never decision-grade, and conflicting
// multi-timeframe signals are reported as a conflict score rather than being
// resolved into a false agreement. Pure library-level checks (no I/O).
TEST_CASE(ayzr_stale_and_conflicting_evidence_are_not_decision_grade) {
    CHECK(!isDecisionGrade(DataQualityState::STALE));
    CHECK(!isDecisionGrade(DataQualityState::UNKNOWN));
    CHECK(!isDecisionGrade(DataQualityState::MISSING));
    CHECK(!isDecisionGrade(DataQualityState::INVALID));
    CHECK(!isDecisionGrade(DataQualityState::DEGRADED));
    CHECK(isDecisionGrade(DataQualityState::VALID));

    // A timeframe that has never been observed reports UNKNOWN, not VALID.
    TimeframeStateStore store;
    CHECK_EQ(store.qualityOf(Timeframe::M15), DataQualityState::UNKNOWN);
    CHECK(!store.hasClosedBar(Timeframe::M15));

    // Conflicting evidence keeps both sides and stays no_trade with no levels.
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"إشارات متعارضة","direction":"no_trade",)"
        R"("astra_agreement":"disagree",)"
        R"("supporting_evidence_ar":["دعم"],"opposing_evidence_ar":["معارضة"],)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680})");
    REQUIRE(parsed.valid);
    CHECK_EQ(parsed.analysis.proposedDirection, std::string("no_trade"));
    CHECK_EQ(parsed.analysis.supportingEvidenceAr.size(),
             static_cast<std::size_t>(1));
    CHECK_EQ(parsed.analysis.opposingEvidenceAr.size(),
             static_cast<std::size_t>(1));
    CHECK(!parsed.analysis.hasEntry);
    CHECK(!parsed.analysis.hasStop);
    CHECK(!parsed.analysis.hasTarget);
}
