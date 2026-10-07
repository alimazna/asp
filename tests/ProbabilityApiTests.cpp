// TST-0010 - Probability API surface (T09).
//
// RULE C is the whole point of these cases: the surface may present a value as
// a *probability* only when it is calibrated AND the calibration is audited.
// Every other path must report calibrated=false and a null probability.

#include "TestHarness.h"

#include "api/BackendFacade.h"
#include "api/ProbabilityApi.h"
#include "governance/ApprovalGate.h"
#include "governance/IncidentManager.h"
#include "health/HealthMonitor.h"
#include "ledger/PredictionLedger.h"
#include "runtime/AuraRuntime.h"
#include "shadow/PositionSimulator.h"
#include "telegram/TelegramGateway.h"

#include <string>

using namespace aura;

namespace {

struct Fixture {
    AuraRuntime runtime;
    HealthMonitor health;
    PredictionLedger ledger;
    PositionSimulator positions;
    IncidentManager incidents;
    ApprovalGate approvals;
    TelegramGateway telegram;
    ProbabilityApi probability;
    BackendFacade facade;

    Fixture()
        : runtime(StartupOptions{}, 100),
          approvals(nullptr),
          facade(FacadeDependencies{&runtime, &health, &ledger, &positions,
                                    &incidents, &approvals, &telegram,
                                    &probability}) {}
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

PredictionRecord makeRecord(SignalDirection direction, double score,
                            double probability, bool calibrated) {
    PredictionRecord record;
    record.decisionId = EntityId("M15-9000-LONG");
    record.timeframe = Timeframe::M15;
    record.asOfBarOpenSec = 9000;
    record.direction = direction;
    record.score = score;
    record.confidence = 0.6;
    record.probabilityEstimate = probability;
    record.probabilityCalibrated = calibrated;
    record.recordedAt = Timestamp::fromEpochMillis(9000000);
    return record;
}

}  // namespace

TEST_CASE(probability_route_is_unavailable_without_a_surface) {
    AuraRuntime runtime(StartupOptions{}, 100);
    HealthMonitor health;
    PredictionLedger ledger;
    PositionSimulator positions;
    IncidentManager incidents;
    ApprovalGate approvals(nullptr);
    TelegramGateway telegram;
    BackendFacade facade(FacadeDependencies{&runtime, &health, &ledger,
                                            &positions, &incidents, &approvals,
                                            &telegram});  // probability = null
    const ApiResponse response = facade.handle("GET", "/api/v1/probability/latest");
    CHECK_EQ(response.status, 503);
    CHECK(contains(response.body, "dependency_unavailable"));
}

TEST_CASE(probability_route_reports_no_prediction_yet) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"available\":false"));
    CHECK(contains(response.body, "no predictions recorded yet"));
}

TEST_CASE(uncalibrated_value_is_not_presented_as_a_probability) {
    Fixture fixture;
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::LONG, 71.0, 0.72, /*calibrated=*/false)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":false"));
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "\"score\":71"));
    CHECK(contains(response.body, "\"score_is_probability\":false"));
    CHECK(contains(response.body, "\"direction\":\"UP\""));
    CHECK(contains(response.body, "uncalibrated"));
}

TEST_CASE(calibrated_but_unaudited_value_stays_uncalibrated) {
    // RULE C: calibration alone is not enough; the audit gate must also be open.
    Fixture fixture;
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::LONG, 71.0, 0.72, /*calibrated=*/true)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":false"));
    CHECK(contains(response.body, "\"probability\":null"));
}

TEST_CASE(calibrated_and_audited_value_is_presented_with_its_tier) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::LONG, 71.0, 0.72, /*calibrated=*/true)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":true"));
    CHECK(contains(response.body, "\"probability\":0.72"));
    CHECK(contains(response.body, "\"coverage_tier\":\"high\""));
    CHECK(contains(response.body, "\"direction\":\"UP\""));
}

TEST_CASE(direction_none_is_never_a_probability) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::NONE, 0.0, 0.5, /*calibrated=*/true)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":false"));
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "\"direction\":\"NONE\""));
}

TEST_CASE(out_of_range_calibrated_value_is_rejected_not_clamped) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::SHORT, 10.0, 1.4, /*calibrated=*/true)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":false"));
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "out of range"));
}

TEST_CASE(short_direction_maps_to_down) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::SHORT, 30.0, 0.2, /*calibrated=*/true)));

    const ApiResponse response = fixture.facade.latestProbability();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"calibrated\":true"));
    CHECK(contains(response.body, "\"direction\":\"DOWN\""));
    CHECK(contains(response.body, "\"coverage_tier\":\"low\""));
}

TEST_CASE(tier_boundaries_match_the_producer_contract) {
    CHECK(probabilityTierBoundariesMatchProducer());
    CHECK_EQ(std::string(probabilityTier(0.0)), std::string("low"));
    CHECK_EQ(std::string(probabilityTier(0.5)), std::string("medium"));
    CHECK_EQ(std::string(probabilityTier(0.9)), std::string("high"));
    CHECK(probabilityTier(-0.01) == nullptr);
    CHECK(probabilityTier(1.01) == nullptr);
}

TEST_CASE(payload_is_deterministic_for_a_fixed_record) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.ledger.append(
        makeRecord(SignalDirection::LONG, 71.0, 0.72, /*calibrated=*/true)));

    const ApiResponse first = fixture.facade.latestProbability();
    const ApiResponse second = fixture.facade.latestProbability();
    CHECK_EQ(first.body, second.body);
    CHECK(contains(first.body, "\"api\":\"v1\""));
    CHECK(contains(first.body, "\"schema\":\"1.0\""));
}
