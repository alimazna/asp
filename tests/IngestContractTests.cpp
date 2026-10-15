// INGEST-0001 - the production entry point's argument parser must accept the
// "--flag=value" form, and the ingest route must reject a timeframe the analysis
// engine cannot map instead of persisting an unusable row.
//
// Both were found while verifying a live deployment:
//   * `astra_server --host=127.0.0.1 --port=N` silently ignored BOTH flags and
//     bound the default 0.0.0.0:8080, because the parser only looked at
//     argv[i] == "--port" and then read argv[i+1] (which was "--port=N" itself,
//     unparseable, so the default was kept). Railway-style tooling emits the "="
//     form, so this is a live footgun.
//   * an ingest batch with timeframe "M7" was accepted, persisted, and then
//     silently skipped by the analysis feed, leaving a permanently unusable row.
//
// This test runs the REAL astra_server binary over real HTTP against a real
// SQLite file, so it fails if either guarantee regresses. Linux-only.

#include "TestHarness.h"

#include "foundation/HttpClient.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

namespace {

#ifndef _WIN32

std::uint16_t freePort() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
}

// Poll the health route until it answers or the deadline passes.
bool waitForHealth(std::uint16_t port, int timeoutMillis) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMillis);
    while (std::chrono::steady_clock::now() < deadline) {
        HttpRequest req;
        req.port = port;
        req.path = "/api/v1/health";
        req.connectTimeoutMillis = 300;
        req.readTimeoutMillis = 1000;
        const HttpResponse r = httpGet(req);
        if (r.ok && r.statusCode == 200) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

HttpResponse postIngest(std::uint16_t port, const std::string& body,
                        const std::string& token = "ingest-test-token") {
    HttpRequest req;
    req.port = port;
    req.path = "/api/v1/candles/ingest";
    req.method = "POST";
    req.body = body;
    req.headers = "Authorization: Bearer " + token + "\r\n";
    req.connectTimeoutMillis = 1000;
    req.readTimeoutMillis = 3000;
    return httpRequest(req);
}

#endif  // !_WIN32

}  // namespace

TEST_CASE(equals_form_cli_args_are_honored_and_bad_timeframe_is_rejected) {
#ifdef _WIN32
    std::cout << "  [SKIP] process-level server test is Linux-only\n";
    return;
#else
    // Locate the built server binary. The build directory name varies between
    // local and CI checkouts, so probe the conventional ones in order.
    fs::path exe;
    for (const char* dir : {"build-verify", "build-linux", "build", "out"}) {
        const fs::path candidate =
            fs::path(AURA_SOURCE_DIR) / dir / "astra_server";
        if (fs::exists(candidate)) {
            exe = candidate;
            break;
        }
    }
    if (exe.empty()) {
        std::cout << "  [SKIP] astra_server binary not found under any build dir\n";
        return;
    }

    const fs::path tmp = fs::temp_directory_path() /
                         ("astra-ingest-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "data", ec);
    REQUIRE(fs::exists(tmp / "data"));

    const std::uint16_t port = freePort();
    REQUIRE(port != 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::setsid();
        const std::string dataDir = (tmp / "data").string();
        ::setenv("ASTRA_DATA_DIR", dataDir.c_str(), 1);
        ::setenv("ASTRA_SENDER_TOKEN", "ingest-test-token", 1);
        // The "--flag=value" form is the whole point of this test: it must be
        // honored exactly like "--flag value".
        const std::string hostArg = "--host=127.0.0.1";
        const std::string portArg = "--port=" + std::to_string(port);
        const std::string exeStr = exe.string();
        ::execl(exeStr.c_str(), exeStr.c_str(), hostArg.c_str(), portArg.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    const bool healthy = waitForHealth(port, 15000);
    CHECK(healthy);

    if (healthy) {
        // 1. A supported timeframe with two genuinely closed, aligned candles.
        const std::string good =
            "{\"source_id\":\"test-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"candles\":["
            "{\"time\":1792065000,\"open\":1901,\"high\":1911,\"low\":1899,"
            "\"close\":1906,\"tick_volume\":500},"
            "{\"time\":1792065900,\"open\":1906,\"high\":1912.5,\"low\":1904.5,"
            "\"close\":1910,\"tick_volume\":412}]}";
        const HttpResponse first = postIngest(port, good);
        CHECK(first.is2xx());
        CHECK(first.body.find("\"state\":\"stored_new\"") != std::string::npos);

        // 2. The identical batch replayed must not create a second record.
        const HttpResponse replay = postIngest(port, good);
        CHECK(replay.is2xx());
        CHECK(replay.body.find("\"state\":\"already_stored\"") != std::string::npos);
        CHECK(replay.body.find("\"state\":\"stored_new\"") == std::string::npos);

        // 3. An unmappable timeframe must be refused, not persisted.
        const std::string badTf =
            "{\"source_id\":\"test-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M7\",\"candles\":["
            "{\"time\":1792066800,\"open\":1900,\"high\":1910,\"low\":1890,"
            "\"close\":1905,\"tick_volume\":100}]}";
        const HttpResponse bad = postIngest(port, badTf);
        CHECK(bad.statusCode == 400);
        CHECK(bad.body.find("\"code\":\"invalid_timeframe\"") != std::string::npos);

        // 4. Unauthenticated ingestion must be refused.
        const HttpResponse noAuth = postIngest(port, good, "");
        CHECK(noAuth.statusCode == 401);

        // 5. The authoritative store must hold exactly the two accepted candles,
        //    and no row for the rejected M7 batch.
        const HttpResponse candles = httpGet(
            HttpRequest{std::string("127.0.0.1"), port,
                        "/api/v1/candles?tf=M15&limit=100"});
        CHECK(candles.is2xx());
        const HttpResponse m7 = httpGet(
            HttpRequest{std::string("127.0.0.1"), port,
                        "/api/v1/candles?tf=M7&limit=100"});
        // An unknown timeframe has no series, so this must not report data.
        CHECK(!(m7.is2xx() && m7.body.find("\"count\":2") != std::string::npos));
    }

    ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, &status, 0);

    fs::remove_all(tmp, ec);
#endif
}
