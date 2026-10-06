// TST-0003 - Data quality propagation: stale/invalid/unknown gating.

#include "TestHarness.h"

#include "data/BarFinalizer.h"
#include "data/TimeframeStateStore.h"
#include "resilience/CapabilityId.h"
#include "resilience/CriticalityPolicy.h"
#include "resilience/DegradationImpact.h"
#include "resilience/DependencyGraph.h"
#include "resilience/ImpactResolver.h"
#include "mt5/Mt5BridgeContract.h"

using namespace aura;

namespace {
constexpr std::int64_t kM15 = 15 * 60;
}

TEST_CASE(unknown_is_not_decision_grade) {
    CHECK(!isDecisionGrade(DataQualityState::UNKNOWN));
    CHECK(!isDecisionGrade(DataQualityState::STALE));
    CHECK(!isDecisionGrade(DataQualityState::MISSING));
    CHECK(!isDecisionGrade(DataQualityState::INVALID));
    CHECK(!isDecisionGrade(DataQualityState::DEGRADED));
    CHECK(isDecisionGrade(DataQualityState::VALID));
}

TEST_CASE(unobserved_timeframe_reports_unknown_not_valid) {
    TimeframeStateStore store;
    CHECK_EQ(store.qualityOf(Timeframe::M15), DataQualityState::UNKNOWN);
    CHECK(!store.hasClosedBar(Timeframe::H4));

    TimeframeState state;
    CHECK(!store.get(Timeframe::M15, state));
}

TEST_CASE(stale_timeframe_is_not_fresh) {
    TimeframeStateStore store;
    const Bar bar = test::makeBar(Timeframe::M15, 0, 2000, 2005, 1995, 2001);
    store.updateFromClosedBar(bar, 1);

    // Observe far in the future so the last bar is well past the freshness
    // allowance; quality must degrade rather than remain VALID.
    const Timestamp later =
        Timestamp::fromEpochMillis((20 * kM15) * 1000);
    store.refreshFreshness(Timeframe::M15, later, 2);

    const DataQualityState quality = store.qualityOf(Timeframe::M15);
    CHECK(quality != DataQualityState::VALID);
}

TEST_CASE(bridge_failure_blocks_dependents_not_independents) {
    DependencyGraph graph;
    const CapabilityId bridge("capability.bridge");
    const CapabilityId features("capability.features");
    const CapabilityId audit("capability.audit");

    // features depends on bridge (hard); audit is independent.
    graph.addDependency({features, bridge, true});
    graph.addDependency({audit, bridge, false});

    CriticalityPolicy policy;
    policy.set(bridge, Criticality::IMPORTANT);

    ImpactResolver resolver(graph, policy);
    const auto impacts = resolver.resolve(bridge);

    bool featuresBlocked = false;
    bool auditAffected = false;
    bool auditBlocked = false;
    bool bridgeBlocked = false;
    for (const auto& impact : impacts) {
        if (impact.capability == features && impact.level == ImpactLevel::BLOCKED) {
            featuresBlocked = true;
        }
        if (impact.capability == audit) {
            auditAffected = true;
            if (impact.level == ImpactLevel::BLOCKED) auditBlocked = true;
        }
        if (impact.capability == bridge && impact.level == ImpactLevel::BLOCKED) {
            bridgeBlocked = true;
        }
    }

    CHECK(bridgeBlocked);
    CHECK(featuresBlocked);
    // Soft edge: audit degrades at most, and is never hard-blocked.
    CHECK(!auditBlocked);
    (void)auditAffected;
}
