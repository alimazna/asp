// HOST-0011 - AURA backend host entry point.
//
// This is the double-clickable backend process. It resolves its own paths,
// starts the bundled Python MT5 bridge as a supervised child, and runs the
// runtime loop. The user is never asked to open a terminal or run Python.

#include "api/BackendFacade.h"
#include "governance/ApprovalGate.h"
#include "governance/IncidentManager.h"
#include "platform/windows/PathResolver.h"
#include "runtime/AuraRuntime.h"
#include "telegram/TelegramGateway.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

void handleSignal(int) { g_stopRequested = 1; }

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

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cout << "AURA backend host starting\n";

    aura::AppPaths paths;
    std::string pathError;
    aura::PathResolver resolver;
    if (!resolver.resolve(paths, pathError)) {
        std::cerr << "FATAL: " << pathError << "\n";
        return 2;
    }
    std::cout << "  app root: " << paths.appRootDir << "\n";

    aura::StartupOptions options;
    options.allowSystemPythonFallback = wantsDevFallback(argc, argv);
    aura::AuraRuntime runtime(options);

    std::string startupError;
    const bool started = runtime.start(startupError);
    const aura::StartupReport& startup = runtime.startupReport();
    std::cout << "  startup stage: " << aura::toString(startup.stage) << "\n";
    if (!started) {
        std::cerr << "  startup not ready: " << startupError << "\n";
        // Continue in DEGRADED mode: the backend stays diagnosable.
    } else {
        std::cout << "  resolved symbol: " << startup.resolvedSymbol << "\n";
    }

    const int intervalMillis = parseIntervalMillis(argc, argv);
    const bool oneShot = wantsOneShot(argc, argv);

    // The in-process facade is the backend API surface the frontend talks to.
    // The transport (local API, IPC) is a separate concern; the contract is
    // identical either way.
    aura::IncidentManager incidents;
    aura::ApprovalGate approvals(runtime.guardian());
    aura::TelegramGateway telegram;
    aura::BackendFacade facade(aura::FacadeDependencies{
        &runtime, &runtime.health(), &runtime.ledger(), &runtime.positions(),
        &incidents, &approvals, &telegram});

    // Development/ops surface: expose the current state summary once at start.
    const aura::ApiResponse systemState = facade.handle("GET", "/api/v1/system/state");
    std::cout << "  api system/state: " << systemState.body << "\n";

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
    runtime.stop();
    return started ? 0 : 1;
}
