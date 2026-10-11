// Bug 2 - the loopback server must not serialize a slow /candles behind
// /health.
//
// The field log showed "ui health ok: aggregate=STARTING" flip-flopping while a
// candle fetch was in flight. Root cause: the accept loop served one connection
// to completion, and /candles *proxies* to the Python bridge (which reads MT5).
// A slow-but-healthy cold read therefore blocked the fast /health behind it for
// the whole proxy budget, and the frontend (5s health budget) counted a healthy
// backend as OFFLINE.
//
// This test drives the real LoopbackApiServer over a real slow bridge fixture:
// it fires a /candles request that holds the bridge for several seconds, then a
// /health 0.5s later, and requires /health to answer promptly regardless.

#include "TestHarness.h"

#include "api/BackendFacade.h"
#include "api/LoopbackApiServer.h"
#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
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

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

using namespace aura;

namespace {

std::string findPython() {
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) return "";
    std::string path(pathEnv);
    // PATH is ';'-separated on Windows, ':' elsewhere; splitting on the wrong
    // character made this case SKIP on the Windows CI runner.
#ifdef _WIN32
    const char kPathSep = ';';
    const std::vector<std::string> names = {"python.exe", "python3.exe",
                                            "python", "python3"};
#else
    const char kPathSep = ':';
    const std::vector<std::string> names = {"python3", "python"};
#endif
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(kPathSep, start);
        const std::string dir =
            path.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (!dir.empty()) {
            for (const auto& name : names) {
                const std::string candidate = dir + "/" + name;
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) return candidate;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

std::uint16_t freePort() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 8799;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8799;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::uint16_t port = 8799;
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        sockaddr_in bound{};
#ifdef _WIN32
        int len = sizeof(bound);
#else
        socklen_t len = sizeof(bound);
#endif
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    return port;
}

void setEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    ::setenv(key, value.c_str(), 1);
#endif
}

}  // namespace

TEST_CASE(health_is_not_blocked_by_a_slow_candles_proxy) {
    const std::string python = findPython();
    if (python.empty()) {
        std::cout << "  [SKIP] no python interpreter on PATH; "
                     "concurrency regression not exercised\n";
        return;
    }

    const std::string fixture =
        std::string(AURA_SOURCE_DIR) + "/tests/integration/slow_bridge_fixture.py";
    const std::uint16_t bridgePort = freePort();
    const std::uint16_t apiPort = freePort();

    // Slow bridge: /candles holds for 3s, /health answers at once.
    ProcessLaunchSpec spec;
    spec.executable = python;
    spec.args = {fixture, "--host", "127.0.0.1",
                 "--port", std::to_string(bridgePort), "--delay", "3"};
    spec.workingDir = std::string(AURA_SOURCE_DIR) + "/tests/integration";
    spec.environment["PYTHONUNBUFFERED"] = "1";
    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    bool bridgeReady = false;
    for (int i = 0; i < 40 && !bridgeReady; ++i) {
        HttpRequest ping;
        ping.host = "127.0.0.1";
        ping.port = bridgePort;
        ping.path = "/v1/health";
        ping.connectTimeoutMillis = 300;
        ping.readTimeoutMillis = 500;
        if (httpGet(ping).ok) bridgeReady = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(bridgeReady);

    setEnv("ASTRA_BRIDGE_PORT", std::to_string(bridgePort));
    BackendFacade facade(FacadeDependencies{});

    LoopbackApiServerConfig config;
    config.port = apiPort;
    LoopbackApiServer server(&facade, config);
    REQUIRE(server.start(error));

    // Fire the slow candle fetch, then a health probe 0.5s later while the
    // candle proxy is still waiting on the bridge.
    HttpRequest candles;
    candles.host = "127.0.0.1";
    candles.port = apiPort;
    candles.path = "/api/v1/candles?tf=M15&limit=10";
    candles.readTimeoutMillis = 15000;
    HttpResponse candlesResponse;
    std::thread candlesThread([&]() { candlesResponse = httpGet(candles); });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    HttpRequest health;
    health.host = "127.0.0.1";
    health.port = apiPort;
    health.path = "/api/v1/health";
    health.readTimeoutMillis = 4000;
    const auto start = std::chrono::steady_clock::now();
    const HttpResponse healthResponse = httpGet(health);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();

    candlesThread.join();

    // With a serialized server the health probe would wait ~2.5s (the rest of
    // the candle hold). Concurrent handling answers it in well under 1s. The
    // bare facade has no health monitor wired, so the route answers 503 by
    // design; what this test pins is that it answers *promptly*, not behind the
    // slow candle proxy.
    CHECK(healthResponse.ok);
    CHECK(healthResponse.statusCode > 0);
    CHECK(elapsedMs < 1000);
    CHECK(candlesResponse.ok);
    CHECK(candlesResponse.is2xx());

    server.stop();
    setEnv("ASTRA_BRIDGE_PORT", "8791");
    std::string stopError;
    supervisor.stop(3000, stopError);
}
