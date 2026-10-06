// TST-0006 - Restart continuity and idempotency.

#include "TestHarness.h"

#include "persistence/FilePersistenceStore.h"
#include "persistence/PersistenceEngine.h"
#include "reconciliation/ReconciliationEngine.h"
#include "recovery/CheckpointManager.h"
#include "recovery/CrashRecoveryManager.h"
#include "shadow/PositionSimulator.h"

#include <atomic>
#include <filesystem>
#include <string>

using namespace aura;

namespace {

std::string freshRoot(const std::string& label) {
    static std::atomic<unsigned long> counter{0};
    const auto base = std::filesystem::temp_directory_path() /
                      ("aura-test-" + label + "-" +
                       std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base.string();
}

PredictionRecord samplePrediction(const std::string& id) {
    PredictionRecord record;
    record.decisionId = EntityId(id);
    record.timeframe = Timeframe::M15;
    record.asOfBarOpenSec = 1735689600;
    record.direction = SignalDirection::LONG;
    record.score = 72.5;
    record.confidence = 0.61;
    record.referencePrice = 2000.0;
    record.stopPrice = 1990.0;
    record.targetPrice = 2020.0;
    record.recordedAt = Timestamp::fromEpochMillis(1735689600000);
    return record;
}

}  // namespace

TEST_CASE(prediction_survives_restart) {
    const std::string root = freshRoot("restart");
    {
        FilePersistenceStore store(root);
        PersistenceEngine engine(&store);
        CHECK(engine.persistPrediction(samplePrediction("dec-1")));
    }
    {
        // Fresh store instance over the same directory simulates a restart.
        FilePersistenceStore store(root);
        std::string payload;
        PersistenceRecordMetadata metadata;
        const PersistenceStatus status =
            store.get("predictions", "dec-1", payload, metadata);
        CHECK_EQ(static_cast<int>(status), static_cast<int>(PersistenceStatus::OK));
        CHECK(payload.find("dec-1") != std::string::npos);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE(prediction_write_is_idempotent) {
    const std::string root = freshRoot("idem");
    FilePersistenceStore store(root);
    PersistenceEngine engine(&store);

    CHECK(engine.persistPrediction(samplePrediction("dec-2")));
    CHECK(engine.persistPrediction(samplePrediction("dec-2")));

    // A repeated logical write collapses instead of duplicating.
    CHECK_EQ(store.keys("predictions").size(), static_cast<std::size_t>(1));
    std::filesystem::remove_all(root);
}

TEST_CASE(missing_record_is_not_found_not_empty) {
    const std::string root = freshRoot("missing");
    FilePersistenceStore store(root);
    std::string payload;
    PersistenceRecordMetadata metadata;
    const PersistenceStatus status =
        store.get("predictions", "does-not-exist", payload, metadata);
    CHECK_EQ(static_cast<int>(status),
             static_cast<int>(PersistenceStatus::NOT_FOUND));
    CHECK(payload.empty());
    std::filesystem::remove_all(root);
}

TEST_CASE(append_only_stream_preserves_order) {
    const std::string root = freshRoot("stream");
    FilePersistenceStore store(root);
    PersistenceRecordMetadata metadata;
    metadata.recordType = "outcomes";
    std::uint64_t seq1 = 0;
    std::uint64_t seq2 = 0;
    CHECK(isPersistenceSuccess(store.append("outcomes", "first", metadata, seq1)));
    CHECK(isPersistenceSuccess(store.append("outcomes", "second", metadata, seq2)));
    CHECK(seq2 > seq1);

    const auto stream = store.readStream("outcomes");
    CHECK_EQ(stream.size(), static_cast<std::size_t>(2));
    CHECK_EQ(stream[0], std::string("first"));
    CHECK_EQ(stream[1], std::string("second"));
    std::filesystem::remove_all(root);
}

TEST_CASE(checkpoint_and_crash_recovery) {
    const std::string root = freshRoot("checkpoint");
    const Timestamp now = Timestamp::fromEpochMillis(1735689600000);
    {
        FilePersistenceStore store(root);
        CheckpointManager checkpoints(&store);
        std::uint64_t sequence = 0;
        CHECK(checkpoints.checkpoint("runtime", "state-v1", now, sequence));
        CHECK(sequence > 0);
        CrashRecoveryManager recovery(&checkpoints, nullptr);
        CHECK(recovery.recordCleanShutdown(now));
    }
    {
        FilePersistenceStore store(root);
        CheckpointManager checkpoints(&store);
        Checkpoint latest;
        CHECK(checkpoints.latest("runtime", latest));
        CHECK_EQ(latest.payload, std::string("state-v1"));

        CrashRecoveryManager recovery(&checkpoints, nullptr);
        const RecoveryReport report = recovery.recover("runtime", now);
        CHECK(report.cleanShutdownLastRun);
        CHECK_EQ(static_cast<int>(report.outcome),
                 static_cast<int>(RecoveryOutcome::RESUMED));
        CHECK_EQ(report.restoredSequence, latest.sequence);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE(persisted_positions_reconcile_cleanly) {
    const std::string root = freshRoot("reconcile");
    FilePersistenceStore store(root);
    PersistenceEngine engine(&store);

    SimulatedPosition position;
    position.positionId = EntityId("pos-1");
    position.commandId = EntityId("cmd-1");
    position.decisionId = EntityId("dec-1");
    position.direction = SignalDirection::LONG;
    // Deliberately awkward doubles: must round-trip exactly.
    position.entryPrice = 2000.1234567890123;
    position.stopPrice = 1995.9876543210987;
    position.targetPrice = 2012.5;
    position.lots = 0.1337;
    position.contractSize = 100.0;
    position.timeframe = Timeframe::M15;
    position.state = PositionState::OPEN;
    REQUIRE(engine.persistPosition(position));

    ReconciliationEngine reconciler(&store);
    const ReconciliationReport report =
        reconciler.reconcile({position}, Timestamp::fromEpochMillis(1735689600000));
    CHECK(report.clean);
    CHECK_EQ(report.matched, static_cast<std::size_t>(1));
    CHECK_EQ(report.mismatched, static_cast<std::size_t>(0));
    std::filesystem::remove_all(root);
}

TEST_CASE(reconciliation_reports_missing_ledger_entry) {
    const std::string root = freshRoot("recon-missing");
    FilePersistenceStore store(root);

    SimulatedPosition position;
    position.positionId = EntityId("pos-unpersisted");
    position.commandId = EntityId("cmd-2");
    position.decisionId = EntityId("dec-2");
    position.direction = SignalDirection::LONG;
    position.entryPrice = 2000.0;
    position.stopPrice = 1995.0;
    position.targetPrice = 2012.0;
    position.lots = 0.1;
    position.state = PositionState::OPEN;

    ReconciliationEngine reconciler(&store);
    const ReconciliationReport report =
        reconciler.reconcile({position}, Timestamp::fromEpochMillis(1735689600000));
    CHECK(!report.clean);
    CHECK_EQ(report.mismatched, static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(report.items[0].finding),
             static_cast<int>(ReconciliationFinding::MISSING_IN_LEDGER));
    std::filesystem::remove_all(root);
}

TEST_CASE(cold_start_is_reported_as_such) {
    const std::string root = freshRoot("cold");
    FilePersistenceStore store(root);
    CheckpointManager checkpoints(&store);
    CrashRecoveryManager recovery(&checkpoints, nullptr);
    const RecoveryReport report =
        recovery.recover("runtime", Timestamp::fromEpochMillis(1735689600000));
    CHECK_EQ(static_cast<int>(report.outcome),
             static_cast<int>(RecoveryOutcome::COLD_START));
    std::filesystem::remove_all(root);
}
