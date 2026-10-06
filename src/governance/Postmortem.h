#pragma once
// GOV-0010 - Postmortem.
//
// A structured, blameless review produced after an incident is resolved. It
// captures the timeline, contributing factors, and follow-up actions. It is a
// record for learning; it never rewrites the incident history.

#include "governance/IncidentManager.h"
#include "research/FailureMemory.h"

#include <string>
#include <vector>

namespace aura {

struct PostmortemAction {
    std::string description;
    std::string owner;
    bool completed = false;
};

struct Postmortem {
    EntityId postmortemId;
    EntityId incidentId;
    std::string title;
    std::string impact;
    std::string rootCause;
    std::vector<std::string> contributingFactors;
    std::vector<PostmortemAction> actions;
    std::vector<IncidentEvent> timeline;
    Timestamp createdAt;
    bool valid = false;
};

class PostmortemBuilder {
public:
    explicit PostmortemBuilder(const IncidentManager* incidents)
        : incidents_(incidents) {}

    // Build a postmortem from a resolved incident. Refuses to build from an
    // incident that is still active.
    Postmortem build(const EntityId& incidentId, const std::string& impact,
                     const std::string& rootCause,
                     const std::vector<std::string>& contributingFactors,
                     Timestamp now);

    static std::string render(const Postmortem& postmortem);

private:
    const IncidentManager* incidents_;
};

}  // namespace aura
