// TST-0005 - Bounded bridge recovery and degradation.

#include "TestHarness.h"

#include "health/Watchdog.h"
#include "mt5/PythonBridgeClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <memory>
#include <string>

using namespace aura;

namespace {

// Deterministic test double for the bridge client. It never touches the
// network; it reports whatever health the test asks for.
class FakeBridgeClient : public IPythonBridgeClient {
public:
    explicit FakeBridgeClient(bool healthy) : healthy_(healthy) {}

    const BridgeClientConfig& config() const noexcept override { return config_; }

    BridgeResult<HandshakeInfo> handshake() override {
        if (!healthy_) {
            return BridgeResult<HandshakeInfo>::failure(
                BridgeError{"BRIDGE_UNAVAILABLE", "UNKNOWN", "bridge down", "", ""});
        }
        return BridgeResult<HandshakeInfo>::success(HandshakeInfo{});
    }

    BridgeResult<BridgeHealth> health() override {
        if (!healthy_) {
            return BridgeResult<BridgeHealth>::failure(
                BridgeError{"BRIDGE_UNAVAILABLE", "UNKNOWN", "bridge down", "", ""});
        }
        BridgeHealth health;
        health.initialized = true;
        health.mt5Ready = true;
        return BridgeResult<BridgeHealth>::success(health);
    }

    BridgeResult<std::vector<BridgeCandle>> candles(const std::string&,
                                                    Timeframe, int, bool) override {
        return BridgeResult<std::vector<BridgeCandle>>::failure(
            BridgeError{"BRIDGE_UNAVAILABLE", "UNKNOWN", "bridge down", "", ""});
    }

    BridgeResult<BridgeTick> tick(const std::string&) override {
        return BridgeResult<BridgeTick>::failure(
            BridgeError{"BRIDGE_UNAVAILABLE", "UNKNOWN", "bridge down", "", ""});
    }

    BridgeResult<SymbolSpecification> symbolSpecification(const std::string&) override {
        return BridgeResult<SymbolSpecification>::failure(
            BridgeError{"BRIDGE_UNAVAILABLE", "UNKNOWN", "bridge down", "", ""});
    }

    bool isLoopbackOnly() const noexcept override { return true; }

    void setHealthy(bool healthy) { healthy_ = healthy; }

private:
    BridgeClientConfig config_;
    bool healthy_;
};

ProcessLaunchSpec badSpec() {
    ProcessLaunchSpec spec;
    spec.executable = "/nonexistent/interpreter/that/cannot/run";
    spec.args = {"bridge_service.py"};
    return spec;
}

}  // namespace

TEST_CASE(watchdog_degrades_then_requests_bounded_recovery) {
    auto client = std::make_shared<FakeBridgeClient>(false);
    ProcessSupervisor supervisor(badSpec());
    WatchdogPolicy policy;
    policy.failureThreshold = 3;
    Watchdog watchdog(client, &supervisor, policy);

    const Timestamp now = Timestamp::fromEpochMillis(1000);

    // Below the threshold: observe, do not thrash.
    CHECK_EQ(static_cast<int>(watchdog.probe(now)),
             static_cast<int>(WatchdogAction::NONE));
    CHECK_EQ(static_cast<int>(watchdog.probe(now)),
             static_cast<int>(WatchdogAction::NONE));

    // At the threshold: attempt recovery, which cannot succeed here.
    const WatchdogAction action = watchdog.probe(now);
    CHECK(action == WatchdogAction::RECOVERY_EXHAUSTED ||
          action == WatchdogAction::RECOVERY_REQUESTED);
    // The supervisor is not ONLINE after a failed recovery attempt.
    CHECK(supervisor.state() != ServiceState::ONLINE);
}

TEST_CASE(watchdog_reports_recovery_when_bridge_returns) {
    auto client = std::make_shared<FakeBridgeClient>(false);
    WatchdogPolicy policy;
    policy.failureThreshold = 2;
    Watchdog watchdog(client, nullptr, policy);   // no supervisor: isolate logic

    const Timestamp now = Timestamp::fromEpochMillis(1000);
    watchdog.probe(now);
    CHECK(watchdog.consecutiveFailures() > 0);

    client->setHealthy(true);
    const WatchdogAction action = watchdog.probe(now);
    CHECK_EQ(static_cast<int>(action), static_cast<int>(WatchdogAction::RECOVERED));
    CHECK_EQ(watchdog.consecutiveFailures(), 0);
}

TEST_CASE(restart_budget_is_bounded) {
    ProcessSupervisor supervisor(badSpec());
    RestartPolicy policy;
    policy.maxRestarts = 2;
    policy.windowMillis = 600000;
    ProcessSupervisor bounded(badSpec(), policy);

    std::string error;
    // Each restart attempt fails (bad executable); the budget must not grow
    // without bound and the final state must be BLOCKED, not ONLINE.
    int failures = 0;
    for (int i = 0; i < 5; ++i) {
        if (!bounded.restart(error)) ++failures;
    }
    CHECK(failures >= 1);
    CHECK(bounded.restartCount() <= policy.maxRestarts + 1);
    CHECK(bounded.state() != ServiceState::ONLINE);
    (void)supervisor;
}
