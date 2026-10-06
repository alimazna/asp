#pragma once
// HOST-0005 - Supervises the bundled Python bridge child process.
//
// Owns launch, bounded restart, and graceful shutdown. The user never starts
// Python manually: this supervisor is the only component that does.

#include "foundation/ServiceState.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aura {

struct ProcessLaunchSpec {
    std::string executable;                    // python interpreter
    std::vector<std::string> args;             // script + flags
    std::string workingDir;                    // bridge directory
    std::map<std::string, std::string> environment;
    // How long spawn() waits for the child to reach exec before concluding the
    // launch succeeded. A child that fails before exec is detected sooner.
    int execProbeMillis = 1500;
};

struct ProcessInfo {
    long pid = 0;
    bool running = false;
    int exitCode = 0;
};

struct RestartPolicy {
    int maxRestarts = 3;
    std::int64_t windowMillis = 300000;   // 5 minutes
};

class ProcessSupervisor {
public:
    ProcessSupervisor(ProcessLaunchSpec spec, RestartPolicy policy = {});

    // Launch the child. Fails if already running or the executable is missing.
    bool start(std::string& error);

    // Terminate gracefully, then forcibly after `graceMillis`.
    bool stop(int graceMillis, std::string& error);

    // Bounded restart. Returns false when the restart budget is exhausted.
    bool restart(std::string& error);

    bool isRunning() const;
    ServiceState state() const noexcept { return state_; }
    long pid() const noexcept { return info_.pid; }
    int restartCount() const noexcept { return restartCount_; }
    const ProcessLaunchSpec& spec() const noexcept { return spec_; }

    // Record that a health probe succeeded/failed; drives state transitions.
    void markHealthy();
    void markUnhealthy(const std::string& reason);

    std::string lastError() const;

private:
    bool spawn(std::string& error);
    void reapIfExited();

    ProcessLaunchSpec spec_;
    RestartPolicy policy_;
    ServiceState state_ = ServiceState::OFFLINE;
    ProcessInfo info_;
    int restartCount_ = 0;
    std::string lastError_;
    std::int64_t windowStartMillis_ = 0;
};

}  // namespace aura
