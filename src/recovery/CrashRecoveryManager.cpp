// REC-0006 - Crash recovery manager implementation.

#include "recovery/CrashRecoveryManager.h"

namespace aura {

bool CrashRecoveryManager::recordCleanShutdown(Timestamp now) {
    if (checkpoints_ == nullptr) return false;
    std::uint64_t sequence = 0;
    return checkpoints_->checkpoint("__clean_shutdown__", "clean", now, sequence);
}

RecoveryReport CrashRecoveryManager::recover(const std::string& checkpointName,
                                             Timestamp now) {
    RecoveryReport report;
    report.checkpointName = checkpointName;
    report.completedAt = now.isUnknown() ? Timestamp::now() : now;

    if (checkpoints_ == nullptr || !checkpoints_->isAvailable()) {
        report.outcome = RecoveryOutcome::FAILED;
        report.error = "persistence store unavailable; cannot recover";
        return report;
    }

    Checkpoint shutdownMarker;
    report.cleanShutdownLastRun =
        checkpoints_->latest("__clean_shutdown__", shutdownMarker);

    Checkpoint checkpoint;
    if (!checkpoints_->latest(checkpointName, checkpoint)) {
        report.outcome = RecoveryOutcome::COLD_START;
        report.notes.push_back("no checkpoint found for '" + checkpointName + "'");
        if (!report.cleanShutdownLastRun) {
            report.notes.push_back(
                "previous run did not record a clean shutdown");
        }
        return report;
    }

    report.restoredSequence = checkpoint.sequence;

    if (startup_ != nullptr) {
        const StartupReport& startupReport = startup_->report();
        if (startupReport.stage == StartupStage::READY) {
            report.outcome = RecoveryOutcome::RESUMED;
            report.notes.push_back("checkpoint restored; bridge ready");
        } else if (startupReport.stage == StartupStage::DEGRADED) {
            report.outcome = RecoveryOutcome::PARTIAL;
            report.notes.push_back(
                "checkpoint restored; bridge degraded, dependent capabilities "
                "remain disabled");
        } else {
            report.outcome = RecoveryOutcome::PARTIAL;
            report.notes.push_back(
                std::string("checkpoint restored; startup stage=") +
                toString(startupReport.stage));
        }
    } else {
        report.outcome = RecoveryOutcome::RESUMED;
        report.notes.push_back("checkpoint restored");
    }
    return report;
}

}  // namespace aura
