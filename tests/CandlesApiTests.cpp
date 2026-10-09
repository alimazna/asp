// TST-0022 - /api/v1/candles contract.
//
// The route is validated at three layers:
//  * deterministic validation (unknown/missing tf, out-of-range limit) which
//    needs no bridge;
//  * a real integration check that boots the actual bundled Python bridge with
//    a stub MetaTrader5 module on PYTHONPATH, points the facade at it via
//    ASTRA_BRIDGE_PORT, and verifies the enveloped series end to end. When the
//    environment has no Python interpreter the case reports SKIP;
//  * a dependency-outage check: an unreachable bridge yields 503
//    dependency_unavailable, never fabricated bars.

#include "TestHarness.h"

#include "api/BackendFacade.h"
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

using namespace aura;

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

namespace {

std::string findPython() {
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) return "";
    std::string path(pathEnv);
    const std::vector<std::string> names = {"python3", "python"};
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
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

// Bind an ephemeral loopback port and release it, so the bridge can use it.
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
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::uint16_t port = 8797;
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void setEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    ::setenv(key, value.c_str(), 1);
#endif
}

}  // namespace

// Validation happens before any bridge contact: these cases need no bridge.
TEST_CASE(candles_route_rejects_unknown_timeframe) {
    BackendFacade facade(FacadeDependencies{});
    const ApiResponse response = facade.handle("GET", "/api/v1/candles", "tf=XYZ");
    CHECK_EQ(response.status, 400);
    CHECK(contains(response.body, "\"code\":\"unknown_timeframe\""));
    CHECK(!contains(response.body, "\"bars\""));
}

TEST_CASE(candles_route_requires_timeframe) {
    BackendFacade facade(FacadeDependencies{});
    const ApiResponse response = facade.handle("GET", "/api/v1/candles", "limit=10");
    CHECK_EQ(response.status, 400);
    CHECK(contains(response.body, "\"code\":\"missing_timeframe\""));
}

TEST_CASE(candles_route_rejects_out_of_range_limit) {
    BackendFacade facade(FacadeDependencies{});
    const ApiResponse low = facade.handle("GET", "/api/v1/candles", "tf=M15&limit=0");
    CHECK_EQ(low.status, 400);
    CHECK(contains(low.body, "\"code\":\"invalid_limit\""));
    const ApiResponse high = facade.handle("GET", "/api/v1/candles", "tf=M15&limit=1001");
    CHECK_EQ(high.status, 400);
    const ApiResponse bad = facade.handle("GET", "/api/v1/candles", "tf=M15&limit=abc");
    CHECK_EQ(bad.status, 400);
}

TEST_CASE(candles_route_reports_unreachable_bridge) {
    // Point at a closed port: the route must report the dependency outage
    // explicitly rather than returning an empty-but-OK series.
    const char* previous = std::getenv("ASTRA_BRIDGE_PORT");
    const std::string saved = previous != nullptr ? previous : "";
    setEnv("ASTRA_BRIDGE_PORT", "9");   // discard port, effectively closed
    BackendFacade facade(FacadeDependencies{});
    const ApiResponse response = facade.handle("GET", "/api/v1/candles", "tf=M15&limit=10");
    if (saved.empty()) {
#ifdef _WIN32
        _putenv_s("ASTRA_BRIDGE_PORT", "");
#else
        ::unsetenv("ASTRA_BRIDGE_PORT");
#endif
    } else {
        setEnv("ASTRA_BRIDGE_PORT", saved);
    }
    CHECK_EQ(response.status, 503);
    CHECK(contains(response.body, "\"code\":\"dependency_unavailable\""));
    CHECK(contains(response.body, "python bridge not reachable"));
}

TEST_CASE(real_bridge_serves_enveloped_candle_series) {
    const std::string python = findPython();
    if (python.empty()) {
        std::cout << "  [SKIP] no python interpreter on PATH; "
                     "candles integration not exercised\n";
        return;
    }

    const std::string bridgeDir = std::string(AURA_SOURCE_DIR) + "/bridge/mt5_python";
    const std::string fakeMt5 = std::string(AURA_SOURCE_DIR) + "/tests/integration/fake_mt5";
    const std::uint16_t port = freePort();

    ProcessLaunchSpec spec;
    spec.executable = python;
    spec.args = {bridgeDir + "/bridge_service.py", "--host", "127.0.0.1",
                 "--port", std::to_string(port), "--symbol", "XAUUSD"};
    spec.workingDir = bridgeDir;
    spec.environment["PYTHONUNBUFFERED"] = "1";
    spec.environment["FAKE_MT5_AVAILABLE"] = "1";
    spec.environment["FAKE_MT5_TOTAL_BARS"] = "300";
#ifdef _WIN32
    spec.environment["PYTHONPATH"] = fakeMt5;
#else
    const char* existing = std::getenv("PYTHONPATH");
    spec.environment["PYTHONPATH"] =
        fakeMt5 + (existing != nullptr ? std::string(":") + existing : "");
#endif

    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    // Wait for the bridge to bind.
    bool ready = false;
    for (int i = 0; i < 40 && !ready; ++i) {
        HttpRequest ping;
        ping.host = "127.0.0.1";
        ping.port = port;
        ping.path = "/v1/handshake";
        ping.connectTimeoutMillis = 300;
        ping.readTimeoutMillis = 500;
        if (httpGet(ping).ok) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(ready);

    setEnv("ASTRA_BRIDGE_PORT", std::to_string(port));
    BackendFacade facade(FacadeDependencies{});
    const ApiResponse response =
        facade.handle("GET", "/api/v1/candles", "tf=M15&limit=10");

    setEnv("ASTRA_BRIDGE_PORT", "8791");   // restore production default
    std::string stopError;
    supervisor.stop(3000, stopError);

    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"api\":\"v1\""));
    CHECK(contains(response.body, "\"schema\":\"1.0\""));
    CHECK(contains(response.body, "\"bars\":["));
    CHECK(contains(response.body, "\"timeframe\":\"M15\""));
    CHECK(contains(response.body, "\"symbol\":\"XAUUSD\""));
    CHECK(contains(response.body, "\"count\":10"));
    CHECK(contains(response.body, "\"closed_only\":true"));
    // The first bar carries the full OHLC/volume shape.
    CHECK(contains(response.body, "\"open\":"));
    CHECK(contains(response.body, "\"high\":"));
    CHECK(contains(response.body, "\"low\":"));
    CHECK(contains(response.body, "\"close\":"));
    CHECK(contains(response.body, "\"tick_volume\":"));
    // The bridge's own envelope is not leaked into the v1 contract.
    CHECK(!contains(response.body, "\"protocol_version\""));
}
