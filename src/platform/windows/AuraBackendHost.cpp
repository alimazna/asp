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
#include "platform/windows/SingleInstance.h"
#include "runtime/AuraRuntime.h"
#include "telegram/TelegramGateway.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

void handleSignal(int) { g_stopRequested = 1; }

// Durable host log. The backend is normally spawned detached by the frontend,
// so its stdout is not visible anywhere; without this a startup or bridge-launch
// failure on an installed machine is unexplainable. Opened once paths resolve.
std::ofstream g_hostLog;

// A startup failure (e.g. the single-instance lock cannot be created because the
// install directory is not writable) kills the host before it can bind 8790 for
// the frontend to query. Stage the reason next to the executable, which the
// installer (running elevated) can write; the frontend reads and surfaces it.
// Cleared on a successful start so it never reports a stale failure.
std::string failureMarkerPath(const std::string& appRootDir) {
    return appRootDir.empty() ? std::string() : appRootDir + "/backend-startup.failed";
}

void stageStartupFailure(const std::string& appRootDir, const std::string& reason) {
    const std::string marker = failureMarkerPath(appRootDir);
    if (marker.empty()) return;
    std::ofstream f(marker, std::ios::trunc);
    if (f.is_open()) f << reason << "\n";
}

void clearStartupFailure(const std::string& appRootDir) {
    const std::string marker = failureMarkerPath(appRootDir);
    if (marker.empty()) return;
    std::error_code ec;
    std::filesystem::remove(marker, ec);
}

// Serializes writes to the host log. The startup thread logs the handshake
// outcome while the main thread logs the first cycle, so an unsynchronized
// ofstream would interleave/corrupt lines.
std::mutex g_logMutex;

void logLine(const std::string& text, bool isError = false) {
    (isError ? std::cerr : std::cout) << text << "\n";
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_hostLog.is_open()) {
        g_hostLog << text << "\n";
        g_hostLog.flush();
    }
}

// Wall-clock millisecond timeline. The reported failure was "port 8790 silent
// for 173s"; without per-step timings the slow step is invisible. Every startup
// step logs the milliseconds elapsed since process start so the exact blocking
// step is provable from the log alone.
std::chrono::steady_clock::time_point g_startClock;
std::int64_t timelineMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - g_startClock)
        .count();
}
void logTimeline(const std::string& step) {
    logLine("[t+" + std::to_string(timelineMillis()) + "ms] " + step);
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
    g_startClock = std::chrono::steady_clock::now();
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
    // Create the runtime log dir BEFORE opening the log: on a fresh install
    // <appRoot>/logs does not exist, and ofstream::open does not create parents,
    // so the host would run with no log file at all — exactly the "logs folder
    // is empty" symptom. Then mirror the log there so a detached launch (no
    // console) is still diagnosable after the fact.
    std::error_code fsError;
    std::filesystem::create_directories(paths.logDir, fsError);
    g_hostLog.open(paths.logDir + "/backend-host.log", std::ios::app);
    logLine("  app root: " + paths.appRootDir);

    // Single-instance guard (Bug 2): the frontend port probe alone is not
    // enough because the API only binds after the bridge handshake, so two
    // launches in that window each spawn a host. The lock closes that race.
    // Probe writability of the runtime dir first. The installer targets Program
    // Files, which a non-elevated launch cannot write; without this the only
    // symptom is a backend that silently never binds 8790. Record a precise
    // reason next to the executable (installer-writable) for the frontend.
    bool runtimeWritable = false;
    if (!fsError) {
        const std::string probePath = paths.logDir + "/.write-probe";
        std::ofstream probe(probePath, std::ios::app);
        runtimeWritable = probe.is_open();
        probe.close();
        if (runtimeWritable) {
            std::error_code rmError;
            std::filesystem::remove(probePath, rmError);
        }
    }
    aura::InstanceLock backendLock;
    std::string lockError;
    const std::string lockPath = paths.logDir + "/backend-host.lock";
    if (!backendLock.acquire(lockPath, lockError)) {
        if (!runtimeWritable || fsError) {
            stageStartupFailure(
                paths.appRootDir,
                "cannot write runtime state under " + paths.logDir
                    + " (the install directory is read-only for a non-elevated "
                      "launch): " + lockError);
        }
        logLine("another backend host instance is already running: " + lockError);
        std::cout << "AURA backend host already running; exiting\n";
        return 0;
    }
    logLine("  single-instance lock acquired: " + lockPath);
    // The lock succeeded: this is the live host. Remove any failure staged by a
    // previous (failed) launch so the frontend does not report a stale error.
    clearStartupFailure(paths.appRootDir);

    aura::StartupOptions options;
    options.allowSystemPythonFallback = wantsDevFallback(argc, argv);
    aura::AuraRuntime runtime(options);

    // Phase 0: resolve paths/bundle and build the runtime components. No spawn
    // and no network, so this is milliseconds. The loopback API binds right
    // after this and BEFORE the bridge spawn (see below), which is the whole
    // point: the API must answer the frontend within 10s even when spawning the
    // bridge blocks for a long time.
    std::string startupError;
    const bool startupResolved = runtime.resolveAsync(startupError);
    logTimeline(std::string("resolveAsync done: stage=")
                + aura::toString(runtime.startupReport().stage));
    if (!startupResolved) {
        logLine("  startup not ready: " + startupError, true);
        // Continue in DEGRADED mode: the backend stays diagnosable.
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
        logTimeline("frontend api bound: http://" + apiServer.bindAddress() + ":"
                    + std::to_string(apiServer.port()) + "/api/v1");
    } else {
        logLine("  frontend api unavailable: " + apiError, true);
    }

    // Development/ops surface: expose the state summary now that 8790 is bound.
    // This runs before the bridge is even spawned, so it reports STARTING —
    // proof the API answers before the bridge exists at all.
    {
        const aura::ApiResponse systemState =
            facade.handle("GET", "/api/v1/system/state");
        logLine("  api system/state (pre-spawn): " + systemState.body);
    }

    // Phase 1: spawn the bridge. This is the step that can block for a long time
    // on a fresh install (Windows Defender scanning bridge.exe), which is why it
    // runs *after* the API is bound. The API threads serve STARTING from the
    // phase-0 report throughout.
    std::string spawnError;
    const bool startupBegun = runtime.startAsync(spawnError);
    logTimeline(std::string("startAsync (bridge spawn) done: stage=")
                + aura::toString(runtime.startupReport().stage));
    if (!startupBegun) {
        logLine("  startup not ready: " + spawnError, true);
    }

    // Phase 2: wait for the bridge handshake. The API server serves on its own
    // per-connection threads, so the frontend gets STARTING responses throughout
    // this wait; only the runtime's own data wiring blocks here.
    if (startupBegun) {
        std::string finishError;
        const bool ready = runtime.finishStartup(finishError);
        logTimeline(std::string("finishStartup (handshake) ") +
                    (ready ? "ok" : "failed") + ": stage="
                    + aura::toString(runtime.startupReport().stage));
        if (!ready) logLine("  startup not ready: " + finishError, true);
    }

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
    return runtime.isReady() ? 0 : 1;
}
