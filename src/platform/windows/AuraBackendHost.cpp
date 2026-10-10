// HOST-0011 - AURA backend host entry point.
//
// This is the double-clickable backend process. It resolves its own paths,
// starts the bundled Python MT5 bridge as a supervised child, and runs the
// runtime loop. The user is never asked to open a terminal or run Python.

#include "api/BackendFacade.h"
#include "api/LoopbackApiServer.h"
#include "api/ProbabilityApi.h"
#include "governance/ApprovalGate.h"
#include "governance/IncidentManager.h"
#include "platform/windows/PathResolver.h"
#include "runtime/AuraRuntime.h"
#include "telegram/TelegramGateway.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

void handleSignal(int) { g_stopRequested = 1; }

// Durable host log. The backend is normally spawned detached by the frontend,
// so its stdout is not visible anywhere; without this a startup or bridge-launch
// failure on an installed machine is unexplainable. Opened once paths resolve.
std::ofstream g_hostLog;

void logLine(const std::string& text, bool isError = false) {
    (isError ? std::cerr : std::cout) << text << "\n";
    if (g_hostLog.is_open()) {
        g_hostLog << text << "\n";
        g_hostLog.flush();
    }
}

int parseApiPort(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--api-port") {
            try {
                const int port = std::stoi(argv[i + 1]);
                if (port > 0 && port < 65536) return port;
            } catch (...) {
            }
        }
    }
    return 8790;   // frontend-facing loopback API (bridge uses 8791)
}

int parseIntervalMillis(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--interval-ms") {
            try {
                return std::max(1000, std::stoi(argv[i + 1]));
            } catch (...) {
            }
        }
    }
    return 15000;   // M15 cadence by default
}

bool wantsDevFallback(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--dev-system-python") return true;
    }
    return false;
}

bool wantsOneShot(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--once") return true;
    }
    return false;
}

// Path to the durable calibration audit that opens the RULE C probability gate.
// Defaults to <appRoot>/AUDIT_REPORTS/AUDIT-T11-calibration.md (relocatable
// alongside the executable); overridable for tests and deployments.
std::string parseCalibrationAuditPath(int argc, char** argv,
                                      const std::string& appRootDir) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--calibration-audit") {
            return std::string(argv[i + 1]);
        }
    }
    return appRootDir + "/AUDIT_REPORTS/AUDIT-T11-calibration.md";
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    logLine("AURA backend host starting");

    aura::AppPaths paths;
    std::string pathError;
    aura::PathResolver resolver;
    if (!resolver.resolve(paths, pathError)) {
        std::cerr << "FATAL: " << pathError << "\n";
        return 2;
    }
    // Now that the app root is known, mirror the log to <appRoot>/logs so a
    // detached launch (no console) is still diagnosable after the fact.
    g_hostLog.open(paths.logDir + "/backend-host.log", std::ios::app);
    logLine("  app root: " + paths.appRootDir);

    aura::StartupOptions options;
    options.allowSystemPythonFallback = wantsDevFallback(argc, argv);
    aura::AuraRuntime runtime(options);

    std::string startupError;
    const bool started = runtime.start(startupError);
    const aura::StartupReport& startup = runtime.startupReport();
    logLine(std::string("  startup stage: ") + aura::toString(startup.stage));
    if (!started) {
        logLine("  startup not ready: " + startupError, true);
        // Continue in DEGRADED mode: the backend stays diagnosable.
    } else {
        logLine("  resolved symbol: " + startup.resolvedSymbol);
    }

    const int intervalMillis = parseIntervalMillis(argc, argv);
    const bool oneShot = wantsOneShot(argc, argv);

    // The in-process facade is the backend API surface the frontend talks to.
    // It is published to the ASTRA frontend through the loopback HTTP transport
    // started below (see PKG-0007).
    aura::IncidentManager incidents;
    aura::ApprovalGate approvals(runtime.guardian());
    aura::TelegramGateway telegram;

    // RULE C gate (audit C-1): "audited" is bound to the durable calibration
    // audit artifact, not an in-process toggle. In this build the T11 report is
    // PASS-on-methodology but explicitly NOT publication-authorised (synthetic
    // data, blocker E05), so the gate stays closed and the surface reports a
    // score — never a market probability.
    aura::ProbabilityApi probability;
    const aura::ProbabilityApi::Audit audit = probability.applyCalibrationAudit(
        parseCalibrationAuditPath(argc, argv, paths.appRootDir));
    logLine(std::string("  calibration audit: ")
            + (audit.present ? "present" : "absent")
            + ", passed=" + (audit.passed ? "yes" : "no")
            + ", publication_authorised="
            + (audit.publicationAuthorised ? "yes" : "no") + " ("
            + audit.reason + ")");

    aura::AnalysisApi analysis;
    aura::BackendFacade facade(aura::FacadeDependencies{
        &runtime, &runtime.health(), &runtime.ledger(), &runtime.positions(),
        &incidents, &approvals, &telegram, &probability, &analysis});

    // The frontend-facing transport: a loopback-only HTTP/JSON server over the
    // facade. It is started by the host so the ASTRA frontend connects to a
    // running backend without the user starting anything manually. A bind
    // failure is reported but never fatal: the runtime keeps running.
    aura::LoopbackApiServerConfig apiConfig;
    apiConfig.host = "127.0.0.1";
    apiConfig.port = static_cast<std::uint16_t>(parseApiPort(argc, argv));
    aura::LoopbackApiServer apiServer(&facade, apiConfig);
    std::string apiError;
    if (apiServer.start(apiError)) {
        logLine("  frontend api: http://" + apiServer.bindAddress() + ":"
                + std::to_string(apiServer.port()) + "/api/v1");
    } else {
        logLine("  frontend api unavailable: " + apiError, true);
    }

    // Development/ops surface: expose the current state summary once at start.
    const aura::ApiResponse systemState = facade.handle("GET", "/api/v1/system/state");
    logLine("  api system/state: " + systemState.body);

    do {
        aura::RuntimeCycleReport cycle = runtime.tick(aura::Timestamp::now());
        std::cout << "cycle: ingested=" << cycle.timeframesIngested
                  << " failed=" << cycle.timeframesFailed
                  << " shadow=" << cycle.shadowCommandsIssued
                  << " mode=" << aura::toString(runtime.mode()) << "\n";
        for (const auto& issue : cycle.issues) {
            std::cerr << "  issue: " << issue << "\n";
        }
        if (oneShot) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMillis));
    } while (!g_stopRequested);

    std::cout << "AURA backend host shutting down\n";
    apiServer.stop();
    runtime.stop();
    return started ? 0 : 1;
}
