#pragma once
// HOST-0007 - Coordinates deterministic application startup.
//
// Order: resolve paths -> locate bundle -> launch bridge -> wait for
// handshake -> expose readiness. Any failure is surfaced with a specific
// reason; startup never silently degrades into "just run python manually".

#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"
#include "mt5/PythonBridgeClient.h"
#include "platform/windows/BundleLocator.h"
#include "platform/windows/PathResolver.h"
#include "platform/windows/ProcessSupervisor.h"
#include "platform/windows/SingleInstance.h"

#include <memory>
#include <string>

namespace aura {

enum class StartupStage {
    NOT_STARTED,
    RESOLVING_PATHS,
    LOCATING_BUNDLE,
    LAUNCHING_BRIDGE,
    WAITING_FOR_HANDSHAKE,
    READY,
    DEGRADED,       // bridge not ready but backend can still run safely
    FAILED,
};

inline const char* toString(StartupStage stage) noexcept {
    switch (stage) {
        case StartupStage::NOT_STARTED:          return "NOT_STARTED";
        case StartupStage::RESOLVING_PATHS:      return "RESOLVING_PATHS";
        case StartupStage::LOCATING_BUNDLE:      return "LOCATING_BUNDLE";
        case StartupStage::LAUNCHING_BRIDGE:     return "LAUNCHING_BRIDGE";
        case StartupStage::WAITING_FOR_HANDSHAKE:return "WAITING_FOR_HANDSHAKE";
        case StartupStage::READY:                return "READY";
        case StartupStage::DEGRADED:             return "DEGRADED";
        case StartupStage::FAILED:               return "FAILED";
    }
    return "FAILED";
}

struct StartupOptions {
    bool allowSystemPythonFallback = false;  // development only
    int handshakeTimeoutMillis = 15000;
    int handshakePollMillis = 250;
    std::string preferredSymbol = "XAUUSD";
    bool launchBridgeProcess = true;
};

struct StartupReport {
    StartupStage stage = StartupStage::NOT_STARTED;
    bool ready = false;
    bool bridgeProcessRunning = false;
    bool handshakeOk = false;
    bool mt5Ready = false;
    AppPaths paths;
    BundleLayout bundle;
    std::string resolvedSymbol;
    std::string error;
    std::string detail;
    Timestamp completedAt;
};

class StartupCoordinator {
public:
    StartupCoordinator(StartupOptions options = {}) : options_(std::move(options)) {}

    // Runs the full startup sequence synchronously (begin + handshake). Returns
    // true when READY. Kept for callers that want the whole sequence at once
    // (tests, recovery); the backend host uses the split form below so it can
    // bind its loopback API before the possibly-slow bridge handshake.
    bool run(StartupReport& report);

    // Phase 0: resolve paths and locate the bridge bundle. No process spawn and
    // no network — cheap. Lets the caller build its runtime components and bind
    // the frontend API *before* anything slow (the bridge spawn can block for
    // tens of seconds under AV scanning on a fresh install). Returns false with
    // a specific stage (FAILED/DEGRADED) when there is nothing to launch.
    bool resolve(StartupReport& report);

    // Phase 1: spawn the bridge process and create its client. Returns quickly
    // (never waits for the handshake). `report.stage` is WAITING_FOR_HANDSHAKE
    // when the bridge process was launched. Returns true when the bridge is on
    // its way up. Requires a prior successful resolve().
    bool launch(StartupReport& report);

    // Phase 0 + 1 in one call. Returns quickly (never waits for the handshake),
    // so the caller can start serving the frontend API immediately.
    bool begin(StartupReport& report);

    // Phase 2: block until the bridge answers (or the handshake budget expires)
    // and update `report` to READY/DEGRADED. Safe to call on its own thread while
    // the API serves STARTING from the phase-1 report.
    bool completeHandshake(StartupReport& report);

    // Attempt bridge recovery after a failure; bounded by the supervisor.
    bool recover(StartupReport& report);

    void shutdown();

    const StartupReport& report() const noexcept { return report_; }
    std::shared_ptr<IPythonBridgeClient> bridgeClient() const noexcept {
        return bridgeClient_;
    }
    ProcessSupervisor* supervisor() noexcept { return supervisor_.get(); }

private:
    bool bridgeSpawnAllowed(StartupReport& report);
    bool waitForHandshake(StartupReport& report);

    StartupOptions options_;
    StartupReport report_;
    PathResolver resolver_;
    std::unique_ptr<ProcessSupervisor> supervisor_;
    std::shared_ptr<IPythonBridgeClient> bridgeClient_;
    // Prevents two backend hosts from both spawning a bridge. Held for the
    // whole backend lifetime and released in shutdown().
    InstanceLock bridgeLock_;
};

}  // namespace aura
