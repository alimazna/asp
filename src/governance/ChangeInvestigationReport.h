#pragma once
// GOV-0003 - Change investigation report.
//
// A structured record of why a strategy change was proposed: the triggering
// evidence, the hypothesis, the candidate, and the expected effect. It is the
// audit artifact that precedes any promotion.

#include "evolution/CandidateRegistry.h"
#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "research/FailureMemory.h"

#include <string>
#include <vector>

namespace aura {

struct ChangeInvestigationReport {
    EntityId reportId;
    EntityId hypothesisId;
    EntityId candidateId;
    std::vector<EntityId> triggeringFailures;
    std::vector<EntityId> evidenceEntries;
    std::string summary;
    std::string expectedEffect;
    std::string riskAssessment;
    std::string author;
    Timestamp createdAt;
    bool complete = false;

    bool valid() const noexcept { return !reportId.empty() && !candidateId.empty(); }
};

class ChangeInvestigationBuilder {
public:
    ChangeInvestigationBuilder& fromHypothesis(const EntityId& hypothesisId) {
        report_.hypothesisId = hypothesisId;
        return *this;
    }
    ChangeInvestigationBuilder& forCandidate(const EntityId& candidateId) {
        report_.candidateId = candidateId;
        return *this;
    }
    ChangeInvestigationBuilder& addFailure(const FailureRecord& failure) {
        report_.triggeringFailures.push_back(failure.failureId);
        return *this;
    }
    ChangeInvestigationBuilder& addEvidence(const EntityId& knowledgeEntryId) {
        report_.evidenceEntries.push_back(knowledgeEntryId);
        return *this;
    }
    ChangeInvestigationBuilder& withSummary(const std::string& summary) {
        report_.summary = summary;
        return *this;
    }
    ChangeInvestigationBuilder& withExpectedEffect(const std::string& effect) {
        report_.expectedEffect = effect;
        return *this;
    }
    ChangeInvestigationBuilder& withRiskAssessment(const std::string& assessment) {
        report_.riskAssessment = assessment;
        return *this;
    }
    ChangeInvestigationBuilder& authoredBy(const std::string& author) {
        report_.author = author;
        return *this;
    }

    ChangeInvestigationReport build(const EntityId& reportId, Timestamp now) {
        report_.reportId = reportId;
        report_.createdAt = now.isUnknown() ? Timestamp::now() : now;
        report_.complete = !report_.candidateId.empty() &&
                           !report_.summary.empty() &&
                           !report_.riskAssessment.empty();
        return report_;
    }

private:
    ChangeInvestigationReport report_;
};

}  // namespace aura
