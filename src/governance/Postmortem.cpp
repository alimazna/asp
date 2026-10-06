// GOV-0010 - Postmortem implementation.

#include "governance/Postmortem.h"

#include <sstream>

namespace aura {

Postmortem PostmortemBuilder::build(
    const EntityId& incidentId, const std::string& impact,
    const std::string& rootCause,
    const std::vector<std::string>& contributingFactors, Timestamp now) {
    Postmortem postmortem;
    if (incidents_ == nullptr) return postmortem;

    Incident incident;
    if (!incidents_->get(incidentId, incident)) return postmortem;
    if (incident.state != IncidentState::RESOLVED &&
        incident.state != IncidentState::CLOSED) {
        // A postmortem for an active incident would be premature.
        return postmortem;
    }

    postmortem.postmortemId = EntityId("postmortem-" + incidentId.value());
    postmortem.incidentId = incidentId;
    postmortem.title = incident.title;
    postmortem.impact = impact;
    postmortem.rootCause = rootCause;
    postmortem.contributingFactors = contributingFactors;
    postmortem.timeline = incident.timeline;
    postmortem.createdAt = now.isUnknown() ? Timestamp::now() : now;
    postmortem.valid = true;
    return postmortem;
}

std::string PostmortemBuilder::render(const Postmortem& postmortem) {
    if (!postmortem.valid) return "invalid postmortem";
    std::ostringstream out;
    out << "# Postmortem: " << postmortem.title << "\n";
    out << "Incident: " << postmortem.incidentId.value() << "\n";
    out << "Impact: " << postmortem.impact << "\n";
    out << "Root cause: " << postmortem.rootCause << "\n";
    out << "Contributing factors:\n";
    for (const auto& factor : postmortem.contributingFactors) {
        out << "  - " << factor << "\n";
    }
    out << "Timeline:\n";
    for (const auto& event : postmortem.timeline) {
        out << "  [" << event.at.epochMillis() << "] " << toString(event.from)
            << " -> " << toString(event.to) << " (" << event.actor << ") "
            << event.note << "\n";
    }
    out << "Actions:\n";
    for (const auto& action : postmortem.actions) {
        out << "  - [" << (action.completed ? "x" : " ") << "] "
            << action.description << " (" << action.owner << ")\n";
    }
    return out.str();
}

}  // namespace aura
