// TST-0011 - Live decision pipeline integration (closed bar -> shadow).
//
// Exercises the real decision chain end to end through DecisionPipeline:
// features -> structure -> regime -> eligibility -> signal -> score ->
// confidence -> probability -> macro/market-quality -> risk -> portfolio ->
// shadow execution -> position -> persistence. No mocks: the real engines,
// ledger, simulator, guardian, and file store are used.

#include "TestHarness.h"

#include "guardian/IGuardian.h"
#include "ledger/PredictionLedger.h"
#include "outcomes/OutcomeEngine.h"
#include "persistence/FilePersistenceStore.h"
#include "persistence/PersistenceEngine.h"
#include "runtime/DecisionPipeline.h"
#include "shadow/PositionSimulator.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace aura;

namespace {

// A wavy uptrend: strong enough to be trending/bullish, oscillating enough to
// form swing pivots. Deterministic, no randomness.
std::vector<Bar> syntheticUptrend(Timeframe timeframe, std::int64_t startOpen,
                                  std::size_t count) {
    std::vector<Bar> bars;
    const double pi = 3.14159265358979323846;
    const std::int64_t step = 60;   // one minute spacing (timeframe-agnostic here)
    for (std::size_t i = 0; i < count; ++i) {
        const double base = 2000.0 + static_cast<double>(i) * 0.3;
        const double wave = 15.0 * std::sin(static_cast<double>(i) * 2.0 * pi / 12.0);
        const double close = base + wave;
        const std::int64_t openSec = startOpen + static_cast<std::int64_t>(i) * step;
        bars.push_back(test::makeBar(timeframe, openSec, close - 0.2, close + 1.0,
                                     close - 1.0, close));
    }
    return bars;
}

// A flat, low-volatility series: no trend, RSI near 50, structure RANGE.
// The signal engine votes 0 long / 0 short -> NONE. Eligibility stays
// ELIGIBLE (allowRanging defaults true), so the chain runs to completion and
// hits the no-decision-grade branch rather than bailing early.
std::vector<Bar> syntheticFlat(Timeframe timeframe, std::int64_t startOpen,
                               std::size_t count) {
    std::vector<Bar> bars;
    const double pi = 3.14159265358979323846;
    const std::int64_t step = 60;
    for (std::size_t i = 0; i < count; ++i) {
        const double wave = 15.0 * std::sin(static_cast<double>(i) * 2.0 * pi / 12.0);
        const double close = 2000.0 + wave;
        const std::int64_t openSec = startOpen + static_cast<std::int64_t>(i) * step;
        bars.push_back(test::makeBar(timeframe, openSec, close - 0.2, close + 1.0,
                                     close - 1.0, close));
    }
    return bars;
}

}  // namespace

TEST_CASE(closed_bar_produces_decision_and_shadow_command) {
    const std::string root = "/tmp/aura-pipeline-test";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    REQUIRE(store.isAvailable());
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars =
        syntheticUptrend(Timeframe::M15, 1700000000, 200);
    REQUIRE(bars.size() >= 60);

    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    const PipelineCycleReport report =
        pipeline.onClosedBar(bars.back(), bars, now);

    CHECK(report.decisionProduced);
    REQUIRE(report.decisions.size() == 1);
    const PipelineDecision& decision = report.decisions[0];
    CHECK_EQ(static_cast<int>(decision.direction),
             static_cast<int>(SignalDirection::LONG));
    CHECK(decision.score > 0.0);
    CHECK(decision.confidence > 0.0);
    CHECK(decision.recorded);
    CHECK(decision.shadowIssued);
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(1));
    CHECK_EQ(positions.openCount(), static_cast<std::size_t>(1));

    // Probability must remain explicitly uncalibrated and score must never be
    // recorded as a probability.
    const PredictionRecord& record = ledger.records().back();
    CHECK(!record.probabilityCalibrated);

    // The prediction must have been persisted to the durable store.
    std::string payload;
    PersistenceRecordMetadata metadata;
    CHECK_EQ(static_cast<int>(store.get("predictions", record.decisionId.value(),
                                        payload, metadata)),
             static_cast<int>(PersistenceStatus::OK));

    // The shadow command carries the deterministic decision identity.
    CHECK_EQ(decision.decisionId.value(),
             std::string("M15-") + std::to_string(decision.asOfBarOpenSec) +
                 "-LONG");

    std::filesystem::remove_all(root);
}

TEST_CASE(re_evaluating_the_same_closed_bar_is_a_no_op) {
    const std::string root = "/tmp/aura-pipeline-dup";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars =
        syntheticUptrend(Timeframe::M15, 1700000000, 200);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    const PipelineCycleReport first =
        pipeline.onClosedBar(bars.back(), bars, now);
    REQUIRE(first.shadowIssued);
    const std::size_t ledgerAfterFirst = ledger.size();
    const std::size_t positionsAfterFirst = positions.openCount();

    // Same bar again: the decision identity is deterministic, so it is a
    // duplicate and must not create a second prediction or position.
    const PipelineCycleReport second =
        pipeline.onClosedBar(bars.back(), bars, now);
    CHECK_EQ(ledger.size(), ledgerAfterFirst);
    CHECK_EQ(positions.openCount(), positionsAfterFirst);
    CHECK(!second.shadowIssued);

    std::filesystem::remove_all(root);
}

TEST_CASE(insufficient_history_yields_no_decision) {
    const std::string root = "/tmp/aura-pipeline-short";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars = syntheticUptrend(Timeframe::M15, 1700000000, 10);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    const PipelineCycleReport report =
        pipeline.onClosedBar(bars.back(), bars, now);
    CHECK(!report.decisionProduced);
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(0));
    CHECK_EQ(positions.openCount(), static_cast<std::size_t>(0));

    std::filesystem::remove_all(root);
}

TEST_CASE(none_candidate_still_persists_a_real_context_timestamp) {
    const std::string root = "/tmp/aura-pipeline-none";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    REQUIRE(store.isAvailable());
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars =
        syntheticFlat(Timeframe::M15, 1700000000, 200);
    REQUIRE(bars.size() >= 60);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    // Flat market: no directional signal. No decision, no shadow, no ledger
    // entry — but the context must still be persisted with the real bar time.
    const PipelineCycleReport report =
        pipeline.onClosedBar(bars.back(), bars, now);
    CHECK(!report.decisionProduced);
    CHECK(!report.shadowIssued);
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(0));
    CHECK_EQ(positions.openCount(), static_cast<std::size_t>(0));

    const DecisionContext& context = pipeline.lastDecisionContext();
    CHECK(context.available);
    CHECK_EQ(context.asOfBarOpenSec, bars.back().openTimeSec);
    CHECK_EQ(static_cast<int>(context.direction),
             static_cast<int>(SignalDirection::NONE));
    // Honest current values, not fabricated ones.
    CHECK(context.regime != RegimeType::UNKNOWN);

    std::filesystem::remove_all(root);
}

// --- T33: multi-timeframe context in the live path ---------------------------

// A caller that supplies no MTF context must behave exactly as the historical
// single-timeframe build: the same closed bar that produced a decision before
// still produces one. Backward compatibility is a hard requirement.
TEST_CASE(mtf_absent_keeps_single_timeframe_behaviour) {
    const std::string root = "/tmp/aura-pipeline-mtf-off";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    REQUIRE(store.isAvailable());
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars = syntheticUptrend(Timeframe::M15, 1700000000, 200);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    const PipelineCycleReport report = pipeline.onClosedBar(bars.back(), bars, now);
    CHECK(report.decisionProduced);
    CHECK(pipeline.lastDecisionContext().available);
    CHECK(!pipeline.lastDecisionContext().mtf.available);

    std::filesystem::remove_all(root);
}

// When a cross-timeframe context was built but its core gate failed (H4 missing
// or not decision-grade), the chain must suppress the decision and report the
// objective as UNKNOWN. It must never assert a bias it could not verify.
TEST_CASE(mtf_failed_core_gate_suppresses_decision) {
    const std::string root = "/tmp/aura-pipeline-mtf-gate";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    REQUIRE(store.isAvailable());
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    const std::vector<Bar> bars = syntheticUptrend(Timeframe::M15, 1700000000, 200);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    MtfContext mtf;
    mtf.available = true;
    mtf.coreGatePassed = false;   // H4 unavailable
    mtf.h4Available = false;
    mtf.blockReason = "multi-timeframe core gate: H4 structural authority unavailable";

    const PipelineCycleReport report =
        pipeline.onClosedBar(bars.back(), bars, now, mtf);
    CHECK(!report.decisionProduced);
    CHECK(!report.shadowIssued);
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(0));

    const DecisionContext& context = pipeline.lastDecisionContext();
    CHECK(context.available);
    CHECK_EQ(context.asOfBarOpenSec, bars.back().openTimeSec);
    CHECK_EQ(static_cast<int>(context.direction),
             static_cast<int>(SignalDirection::NONE));
    CHECK(context.mtf.available);
    CHECK(!context.mtf.coreGatePassed);
    REQUIRE(!context.blockReasons.empty());
    CHECK(context.blockReasons.front().find("core gate") != std::string::npos);

    std::filesystem::remove_all(root);
}

// When H4 structural authority is available and directly opposes the operational
// candidate, the real objective vetoes the trade: no decision, no shadow, and
// the reason is recorded. This is the honest multi-timeframe veto.
TEST_CASE(mtf_h4_authority_vetoes_opposing_candidate) {
    const std::string root = "/tmp/aura-pipeline-mtf-veto";
    std::filesystem::remove_all(root);

    auto guardian = makeGuardian();
    PredictionLedger ledger;
    PositionSimulator positions;
    OutcomeEngine outcomes;
    FilePersistenceStore store(root);
    REQUIRE(store.isAvailable());
    PersistenceEngine persistence(&store);

    DecisionPipeline pipeline(guardian.get(), &ledger, &persistence, &positions,
                              &outcomes);
    // Uptrend on M15 => the chain proposes LONG.
    const std::vector<Bar> bars = syntheticUptrend(Timeframe::M15, 1700000000, 200);
    const Timestamp now = Timestamp::fromEpochMillis(
        (bars.back().openTimeSec + 900) * 1000);

    // Verify the baseline first: without MTF this bar yields a LONG decision.
    {
        DecisionPipeline baseline(guardian.get(), nullptr, nullptr, nullptr, nullptr);
        const PipelineCycleReport base = baseline.onClosedBar(bars.back(), bars, now);
        REQUIRE(base.decisionProduced);
        CHECK_EQ(static_cast<int>(base.decisions[0].direction),
                 static_cast<int>(SignalDirection::LONG));
    }

    MtfContext mtf;
    mtf.available = true;
    mtf.coreGatePassed = true;
    mtf.h4Available = true;
    mtf.d1Available = true;
    mtf.h4Bias = StructureBias::BEARISH;   // H4 disagrees with the LONG
    mtf.h4M15Agreement = -1.0;
    mtf.h4StructuralAuthority = -0.6;
    mtf.mtfConflictScore = 0.5;

    const PipelineCycleReport report =
        pipeline.onClosedBar(bars.back(), bars, now, mtf);
    CHECK(!report.decisionProduced);
    CHECK(!report.shadowIssued);
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(0));

    const DecisionContext& context = pipeline.lastDecisionContext();
    CHECK(context.mtf.available);
    CHECK_EQ(static_cast<int>(context.mtf.h4Bias),
             static_cast<int>(StructureBias::BEARISH));
    REQUIRE(!context.blockReasons.empty());
    CHECK(context.blockReasons.front().find("veto") != std::string::npos);

    std::filesystem::remove_all(root);
}

