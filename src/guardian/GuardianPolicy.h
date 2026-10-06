#pragma once
// GDN-0002 - Guardian contract: the enforced protective policy.
//
// This policy is deliberately conservative and is the only place that could
// ever grant live authority. In this build live execution is disabled by
// construction and cannot be enabled by the runtime.

#include "foundation/SystemMode.h"
#include "foundation/Version.h"

namespace aura {

struct GuardianPolicy {
    Version version;
    bool allowShadowExecution = true;
    bool allowLiveExecution = false;   // fixed false for this build
    bool allowResearchMutation = false;
    bool allowConfigMutation = false;
    bool requireHumanApprovalForPromotion = true;
    double maxRiskFractionPerDecision = 0.01;
    SystemMode defaultMode = SystemMode::SHADOW;

    // A policy is valid only if it never grants live authority in this build.
    bool isValid() const noexcept { return allowLiveExecution == false; }
};

}  // namespace aura
