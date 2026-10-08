// TST-0009 - Stable frontend-safe backend API contract (v1).

#include "TestHarness.h"

#include "api/BackendFacade.h"
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
    AuraRuntime runtime;      // not started: exercises the degraded surface
    HealthMonitor health;
    PredictionLedger ledger;
    PositionSimulator positions;
    IncidentManager incidents;
    ApprovalGate approvals;
    TelegramGateway telegram;
    BackendFacade facade;

    Fixture()
        : runtime(StartupOptions{}, 100),
          approvals(nullptr),
          facade(FacadeDependencies{&runtime, &health, &ledger, &positions,
                                    &incidents, &approvals, &telegram}) {}
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE(system_state_route_is_versioned_and_shadow_only) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.systemState();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"api\":\"v1\""));
    CHECK(contains(response.body, "\"schema\":\"1.0\""));
    CHECK(contains(response.body, "\"shadow_only\":true"));
}

TEST_CASE(health_route_never_reports_unknown_as_safe) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.health();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"unknown_is_not_safe\":true"));
    CHECK(contains(response.body, "aggregate"));
}

TEST_CASE(timeframes_route_lists_canonical_streams) {
    Fixture fixture;
    // Populate one timeframe so the array is non-trivial.
    const Bar bar = test::makeBar(Timeframe::M15, 0, 2000, 2005, 1995, 2001);
    fixture.runtime.timeframeStore().updateFromClosedBar(bar, 1);

    const ApiResponse response = fixture.facade.timeframes();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "M15"));
    CHECK(contains(response.body, "\"decision_grade\":true"));
}

TEST_CASE(timeframe_snapshot_rejects_unknown_timeframe) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.handle(
        "GET", "/api/v1/timeframes/M7/snapshot");
    CHECK_EQ(response.status, 400);
    CHECK(contains(response.body, "unknown_timeframe"));
}

TEST_CASE(unobserved_timeframe_snapshot_is_explicit) {
    Fixture fixture;
    const ApiResponse response = fixture.facade.handle(
        "GET", "/api/v1/timeframes/H4/snapshot");
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"observed\":false"));
    // D-1 (Lead cycle 28): the unobserved branch now emits the object quality
    // shape, matching the list route / observed branch / frozen schema.
    CHECK(contains(response.body, "\"quality\":{\"state\":\"UNKNOWN\""));
}

TEST_CASE(latest_signal_keeps_probability_uncalibrated) {
    Fixture fixture;
    PredictionRecord record;
    record.decisionId = EntityId("M15-9000-LONG");
    record.timeframe = Timeframe::M15;
    record.asOfBarOpenSec = 9000;
    record.direction = SignalDirection::LONG;
    record.score = 71.0;
    record.confidence = 0.6;
    record.probabilityCalibrated = false;
    record.recordedAt = Timestamp::fromEpochMillis(9000000);
    REQUIRE(fixture.ledger.append(record));

    const ApiResponse response = fixture.facade.latestSignal();
    CHECK_EQ(response.status, 200);
    CHECK(contains(response.body, "\"probability\":null"));
    CHECK(contains(response.body, "\"score_is_probability\":false"));
    CHECK(contains(response.body, "\"system_mode\":\"SHADOW\""));
}

TEST_CASE(unknown_route_and_bad_method_are_rejected) {
    Fixture fixture;
    const ApiResponse notFound = fixture.facade.handle("GET", "/api/v1/nope");
    CHECK_EQ(notFound.status, 404);

    const ApiResponse badMethod =
        fixture.facade.handle("POST", "/api/v1/system/state");
    CHECK_EQ(badMethod.status, 405);
}

TEST_CASE(command_allow_list_is_enforced) {
    Fixture fixture;

    CommandRequest forbidden;
    forbidden.command = "live.execute";
    forbidden.actor = "operator";
    const ApiResponse denied = fixture.facade.command(forbidden);
    CHECK_EQ(denied.status, 403);

    CommandRequest missingActor;
    missingActor.command = "notify";
    missingActor.payload = "hello";
    CHECK_EQ(fixture.facade.command(missingActor).status, 400);

    CommandRequest notify;
    notify.command = "notify";
    notify.actor = "operator";
    notify.payload = "backend online";
    const ApiResponse accepted = fixture.facade.command(notify);
    CHECK_EQ(accepted.status, 202);
    CHECK(contains(accepted.body, "\"accepted\":true"));
}

TEST_CASE(missing_dependency_yields_503_not_fabricated_data) {
    // A facade with no runtime must not invent a system state.
    BackendFacade bare(FacadeDependencies{});
    const ApiResponse response = bare.systemState();
    CHECK_EQ(response.status, 503);
    CHECK(contains(response.body, "dependency_unavailable"));
}
