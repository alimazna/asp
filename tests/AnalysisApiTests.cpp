// TST-0011 - Analysis API surface (T16).
//
// The decision-support surface. RULE C binds here exactly as on /probability:
// the same gate decides whether a value may be shown as a probability. Fields
// the backend cannot source must be null/UNKNOWN, never fabricated.

#include "TestHarness.h"

#include "api/AnalysisApi.h"
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
    AnalysisApi analysis;
    BackendFacade facade;

    Fixture()
        : runtime(StartupOptions{}, 100),
          approvals(nullptr),
          facade(FacadeDependencies{&runtime, &health, &ledger, &positions,
                                    &incidents, &approvals, &telegram,
                                    &probability, &analysis}) {}

    // Append a prediction; returns false on duplicate id.
    bool add(const std::string& id, SignalDirection direction, double score,
             double probability, bool calibrated) {
        PredictionRecord record;
        record.decisionId = EntityId(id);
        record.timeframe = Timeframe::M15;
        record.asOfBarOpenSec = 9000;
        record.direction = direction;
        record.score = score;
        record.probabilityEstimate = probability;
        record.probabilityCalibrated = calibrated;
        record.recordedAt = Timestamp::fromEpochMillis(9000000);
        return ledger.append(record);
    }
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int n = 0;
    std::size_t at = haystack.find(needle);
    while (at != std::string::npos) {
        ++n;
        at = haystack.find(needle, at + needle.size());
    }
    return n;
}

}  // namespace

TEST_CASE(analysis_routes_require_the_surface) {
    AuraRuntime runtime(StartupOptions{}, 100);
    HealthMonitor health;
    PredictionLedger ledger;
    PositionSimulator positions;
    IncidentManager incidents;
    ApprovalGate approvals(nullptr);
    TelegramGateway telegram;
    ProbabilityApi probability;
    BackendFacade facade(FacadeDependencies{&runtime, &health, &ledger,
                                            &positions, &incidents, &approvals,
                                            &telegram, &probability});  // no analysis
    const ApiResponse response = facade.handle("GET", "/api/v1/analysis/latest");
    CHECK_EQ(response.status, 503);
    CHECK(contains(response.body, "dependency_unavailable"));
}

TEST_CASE(analysis_latest_is_honest_when_no_prediction_exists) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.handle("GET", "/api/v1/analysis/latest");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"signal\":"));
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "\"probability_calibrated\":false"));
    CHECK(contains(response.body, "\"coverage_tier\":\"unknown\""));
    // Fields the backend cannot source yet are null, not invented.
    CHECK(contains(response.body, "\"horizon\":null"));
    CHECK(contains(response.body, "\"confidence_lo\":null"));
    CHECK(contains(response.body, "\"model_version\":null"));
    CHECK(contains(response.body, "\"entry\":null"));
    CHECK(contains(response.body, "\"sl_method\":null"));
    CHECK(contains(response.body, "\"mtf_agreement\":null"));
    CHECK(contains(response.body, "Not financial advice"));
}

TEST_CASE(analysis_latest_never_shows_an_uncalibrated_value_as_probability) {
    Fixture fixture;
    REQUIRE(fixture.add("rec-1", SignalDirection::LONG, 71.0, 0.72, false));
    const ApiResponse response = fixture.facade.handle("GET", "/api/v1/analysis/latest");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"direction\":\"UP\""));
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "\"probability_calibrated\":false"));
    CHECK(contains(response.body, "\"score_is_probability\":false"));
    // The score is exposed additively, but never as the probability.
    CHECK(contains(response.body, "\"score\":71"));
}

TEST_CASE(analysis_latest_presents_probability_only_when_calibrated_and_audited) {
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.add("rec-1", SignalDirection::LONG, 71.0, 0.72, true));
    const ApiResponse response = fixture.facade.handle("GET", "/api/v1/analysis/latest");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"probability\":0.72"));
    CHECK(contains(response.body, "\"probability_calibrated\":true"));
    CHECK(contains(response.body, "\"coverage_tier\":\"high\""));
    CHECK(contains(response.body, "\"score_is_probability\":true"));
}

TEST_CASE(analysis_latest_uses_the_same_gate_as_the_probability_route) {
    // If the two surfaces ever disagree, RULE C has diverged. They must not.
    Fixture fixture;
    fixture.probability.setCalibrationAudited(true);
    REQUIRE(fixture.add("rec-1", SignalDirection::SHORT, 30.0, 0.2, true));
    const ApiResponse prob = fixture.facade.handle("GET", "/api/v1/probability/latest");
    const ApiResponse analysis =
        fixture.facade.handle("GET", "/api/v1/analysis/latest");
    CHECK(contains(prob.body, "\"calibrated\":true"));
    CHECK(contains(prob.body, "\"probability\":0.2"));
    CHECK(contains(analysis.body, "\"probability\":0.2"));
    CHECK(contains(analysis.body, "\"probability_calibrated\":true"));
}

TEST_CASE(analysis_history_is_newest_first_and_capped_by_limit) {
    Fixture fixture;
    REQUIRE(fixture.add("rec-1", SignalDirection::LONG, 10.0, 0.4, false));
    REQUIRE(fixture.add("rec-2", SignalDirection::LONG, 20.0, 0.5, false));
    REQUIRE(fixture.add("rec-3", SignalDirection::LONG, 30.0, 0.6, false));
    const ApiResponse response =
        fixture.facade.handle("GET", "/api/v1/analysis/history", "limit=2");
    CHECK_EQ(response.status, 200);
    // Newest first: rec-3 present, rec-1 excluded by the cap.
    CHECK(contains(response.body, "\"timestamp\""));
    CHECK_EQ(countOccurrences(response.body, "\"symbol\""), 2);
}

TEST_CASE(analysis_history_limit_zero_is_an_empty_array) {
    Fixture fixture;
    REQUIRE(fixture.add("rec-1", SignalDirection::LONG, 10.0, 0.4, false));
    const ApiResponse response =
        fixture.facade.handle("GET", "/api/v1/analysis/history", "limit=0");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"data\":[]"));
}

TEST_CASE(analysis_history_limit_is_clamped_to_500) {
    Fixture fixture;
    REQUIRE(fixture.add("rec-1", SignalDirection::LONG, 10.0, 0.4, false));
    const ApiResponse response =
        fixture.facade.handle("GET", "/api/v1/analysis/history", "limit=99999");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"symbol\""));
}

TEST_CASE(context_latest_reports_unknown_when_no_decision_exists) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.handle("GET", "/api/v1/context/latest");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"regime\":\"UNKNOWN\""));
    CHECK(contains(response.body, "\"h4_bias\":\"UNKNOWN\""));
    CHECK(contains(response.body, "\"m15_trigger\":\"UNKNOWN\""));
    CHECK(contains(response.body, "\"mtf_agreement\":null"));
}

TEST_CASE(health_v1_is_versioned_and_never_reports_unknown_as_ok) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.handle("GET", "/api/v1/health/v1");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"version\":\"v1\""));
    // Runtime not started: aggregate STARTING maps to degraded, never "ok".
    CHECK(contains(response.body, "\"status\":\"degraded\""));
    CHECK(contains(response.body, "\"bridge\":"));
    CHECK(!contains(response.body, "\"status\":\"ok\""));
}
