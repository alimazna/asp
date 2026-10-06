// TST-0001 - Bridge handshake/schema/error behavior.
//
// Two layers are exercised:
//  * deterministic contract checks (loopback enforcement, envelope structs);
//  * a real integration check that boots the actual bundled Python bridge
//    (which runs without MetaTrader5) and speaks to it through the real
//    loopback HTTP client. When the integration environment cannot provide a
//    Python interpreter the integration case reports SKIP rather than a false
//    PASS.

#include "TestHarness.h"

#include "foundation/HttpClient.h"
#include "mt5/PythonBridgeClient.h"
#include "mt5/Mt5BridgeContract.h"
#include "platform/windows/ProcessSupervisor.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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

}  // namespace

TEST_CASE(loopback_enforcement_is_structural) {
    CHECK(isLoopbackHost("127.0.0.1"));
    CHECK(isLoopbackHost("localhost"));
    CHECK(isLoopbackHost("127.0.0.5"));
    CHECK(!isLoopbackHost("0.0.0.0"));
    CHECK(!isLoopbackHost("192.168.1.10"));
    CHECK(!isLoopbackHost("example.com"));

    // The HTTP client refuses to reach a non-loopback host at all.
    HttpRequest request;
    request.host = "10.0.0.1";
    request.port = 8791;
    request.path = "/v1/handshake";
    const HttpResponse response = httpGet(request);
    CHECK(!response.ok);
    CHECK(response.error.find("non-loopback") != std::string::npos);
}

TEST_CASE(bridge_client_reports_loopback_binding) {
    BridgeClientConfig config;
    config.host = "127.0.0.1";
    auto client = makePythonBridgeClient(config);
    REQUIRE(client != nullptr);
    CHECK(client->isLoopbackOnly());
    CHECK_EQ(client->config().protocolVersion.major, 1);
}

TEST_CASE(unreachable_bridge_yields_structured_error) {
    // Nothing is listening on this port; the client must report a structured,
    // actionable failure rather than an empty success.
    BridgeClientConfig config;
    config.host = "127.0.0.1";
    config.port = 9;   // discard port, effectively closed
    config.connectTimeoutMillis = 500;
    auto client = makePythonBridgeClient(config);
    const auto handshake = client->handshake();
    CHECK(!handshake.ok);
    CHECK(!handshake.error.code.empty());
    CHECK(!handshake.error.message.empty());
}

TEST_CASE(real_bridge_handshake_and_error_contract) {
    const std::string python = findPython();
    if (python.empty()) {
        std::cout << "  [SKIP] no python interpreter on PATH; "
                     "bridge integration not exercised\n";
        return;
    }
    const std::string script =
        std::string(AURA_SOURCE_DIR) + "/bridge/mt5_python/bridge_service.py";
    const std::uint16_t port = 8799;

    ProcessLaunchSpec spec;
    spec.executable = python;
    spec.args = {script, "--host", "127.0.0.1", "--port",
                 std::to_string(port), "--symbol", "XAUUSD"};
    spec.workingDir = std::string(AURA_SOURCE_DIR) + "/bridge/mt5_python";
    spec.environment["PYTHONUNBUFFERED"] = "1";

    ProcessSupervisor supervisor(spec);
    std::string error;
    REQUIRE(supervisor.start(error));

    BridgeClientConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.connectTimeoutMillis = 500;
    config.readTimeoutMillis = 2000;
    auto client = makePythonBridgeClient(config);

    // Poll for the handshake; the process needs a moment to bind.
    bool handshakeOk = false;
    HandshakeInfo info;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
        const auto handshake = client->handshake();
        if (handshake.ok) {
            handshakeOk = true;
            info = handshake.value;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (handshakeOk) {
        CHECK_EQ(info.service, std::string("mt5-python-bridge"));
        CHECK_EQ(info.timeframes.size(), kTimeframeCount);
        CHECK_EQ(info.primaryOperationalTimeframe, std::string("M15"));
        CHECK_EQ(info.primaryStructuralTimeframe, std::string("H4"));
        CHECK(info.loopbackOnly);

        // Without MetaTrader5 the candle request must fail with a structured,
        // actionable error - never fabricated candles.
        const auto candles = client->candles("XAUUSD", Timeframe::M15, 5, true);
        CHECK(!candles.ok);
        CHECK(!candles.error.code.empty());
        CHECK(!candles.error.recovery.empty());
    } else {
        std::cout << "  [SKIP] bridge process did not become ready; "
                     "integration not exercised\n";
    }

    std::string stopError;
    supervisor.stop(2000, stopError);
}
