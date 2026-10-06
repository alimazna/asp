#pragma once
// REC-0005 - Crash recovery manager.
//
// On restart, restores runtime state from the latest checkpoint and reports
// whether the previous run terminated cleanly. Recovery never invents state:
// if no checkpoint exists, recovery reports a cold start rather than a resume.

#include "platform/windows/StartupCoordinator.h"
#include "recovery/CheckpointManager.h"

#include <string>
#include <vector>

namespace aura {

enum class RecoveryOutcome {
    COLD_START,        // no checkpoint; nothing to restore
    RESUMED,           // checkpoint restored
    PARTIAL,           // checkpoint restored but some subsystems unavailable
    FAILED,            // recovery attempted and failed
};

inline const char* toString(RecoveryOutcome o) noexcept {
    switch (o) {
        case RecoveryOutcome::COLD_START: return "COLD_START";
        case RecoveryOutcome::RESUMED:    return "RESUMED";
        case RecoveryOutcome::PARTIAL:    return "PARTIAL";
        case RecoveryOutcome::FAILED:     return "FAILED";
    }
    return "FAILED";
}

struct RecoveryReport {
    RecoveryOutcome outcome = RecoveryOutcome::COLD_START;
    std::string checkpointName;
    std::uint64_t restoredSequence = 0;
    bool cleanShutdownLastRun = false;
    std::vector<std::string> notes;
    std::string error;
    Timestamp completedAt;
};

class CrashRecoveryManager {
public:
    CrashRecoveryManager(CheckpointManager* checkpoints,
                         StartupCoordinator* startup)
        : checkpoints_(checkpoints), startup_(startup) {}

    // Record a clean shutdown marker so the next start can tell the difference
    // between a clean stop and a crash.
    bool recordCleanShutdown(Timestamp now);

    RecoveryReport recover(const std::string& checkpointName, Timestamp now);

private:
    CheckpointManager* checkpoints_;
    StartupCoordinator* startup_;
};

}  // namespace aura
