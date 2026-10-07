#pragma once
// PKG-0001 - Backend facade: the frontend-facing API surface (v1).
//
// The facade is the ONLY thing the Alpha frontend talks to. It reads live
// backend state and renders it into the versioned API schema. It exposes no
// mutation path except the explicitly versioned, policy-checked command
// contracts. It never touches the Python bridge, the persistence store, or the
// broker directly on the frontend's behalf.

#include "api/BackendApiSchema.h"
#include "api/ProbabilityApi.h"
#include "governance/ApprovalGate.h"
#include "governance/IncidentManager.h"
#include "health/HealthMonitor.h"
#include "ledger/PredictionLedger.h"
#include "runtime/AuraRuntime.h"
#include "shadow/PositionSimulator.h"
#include "telegram/TelegramGateway.h"

#include <string>
#include <vector>

namespace aura {

struct FacadeDependencies {
    AuraRuntime* runtime = nullptr;
    HealthMonitor* health = nullptr;
    PredictionLedger* ledger = nullptr;
    PositionSimulator* positions = nullptr;
    IncidentManager* incidents = nullptr;
    ApprovalGate* approvals = nullptr;
    TelegramGateway* telegram = nullptr;
    // Optional. When null, the probability surface still reports honestly
    // (available=false / uncalibrated) rather than fabricating a value.
    ProbabilityApi* probability = nullptr;
};

struct CommandRequest {
    std::string command;      // e.g. "pause", "resume", "notify"
    std::string actor;
    std::string payload;
};

class BackendFacade {
public:
    explicit BackendFacade(FacadeDependencies deps) : deps_(deps) {}

    // Read-only routes. Every route returns the standard envelope or an error
    // object; a missing dependency yields 503, never fabricated data.
    ApiResponse systemState() const;
    ApiResponse health() const;
    ApiResponse timeframes() const;
    ApiResponse timeframeSnapshot(const std::string& timeframe) const;
    ApiResponse latestSignal() const;
    ApiResponse latestProbability() const;
    ApiResponse latestRisk() const;
    ApiResponse shadowPositions() const;
    ApiResponse shadowOutcomes() const;
    ApiResponse researchStatus() const;
    ApiResponse governanceStatus() const;
    ApiResponse bridgeStatus() const;
    ApiResponse recentAudit() const;

    // Route table: method + path dispatch. Returns 404 for unknown paths and
    // 405 for a known path with the wrong method.
    ApiResponse handle(const std::string& method, const std::string& path) const;

    // Command contract (versioned, policy-checked). Only a small allow-list of
    // non-execution commands is accepted.
    ApiResponse command(const CommandRequest& request);

    const FacadeDependencies& dependencies() const noexcept { return deps_; }

private:
    FacadeDependencies deps_;
    std::vector<std::string> commandLog_;
};

}  // namespace aura
