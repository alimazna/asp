// GOV-0009 - Incident manager implementation.

#include "governance/IncidentManager.h"

namespace aura {

int IncidentManager::severityRank(IncidentSeverity s) noexcept {
    switch (s) {
        case IncidentSeverity::INFO:     return 0;
        case IncidentSeverity::MINOR:    return 1;
        case IncidentSeverity::MAJOR:    return 2;
        case IncidentSeverity::CRITICAL: return 3;
    }
    return 0;
}

EntityId IncidentManager::open(const std::string& title,
                               const std::string& summary,
                               IncidentSeverity severity,
                               const std::string& actor, Timestamp now) {
    const Timestamp when = now.isUnknown() ? Timestamp::now() : now;
    Incident incident;
    incident.incidentId = EntityId("incident-" + std::to_string(++sequence_));
    incident.title = title;
    incident.summary = summary;
    incident.severity = severity;
    incident.state = IncidentState::OPEN;
    incident.openedAt = when;
    incident.updatedAt = when;
    incident.timeline.push_back(
        {when, actor, IncidentState::OPEN, IncidentState::OPEN, "opened"});
    incidents_.push_back(incident);
    return incident.incidentId;
}

bool IncidentManager::transition(const EntityId& incidentId, IncidentState to,
                                 const std::string& actor,
                                 const std::string& note, Timestamp now) {
    const Timestamp when = now.isUnknown() ? Timestamp::now() : now;
    for (auto& incident : incidents_) {
        if (incident.incidentId != incidentId) continue;
        if (incident.state == IncidentState::CLOSED) return false;   // terminal
        IncidentEvent event;
        event.at = when;
        event.actor = actor;
        event.from = incident.state;
        event.to = to;
        event.note = note;
        incident.timeline.push_back(event);
        incident.state = to;
        incident.updatedAt = when;
        return true;
    }
    return false;
}

bool IncidentManager::raiseSeverity(const EntityId& incidentId,
                                    IncidentSeverity severity,
                                    const std::string& actor, Timestamp now) {
    for (auto& incident : incidents_) {
        if (incident.incidentId != incidentId) continue;
        // Severity may only increase.
        if (severityRank(severity) <= severityRank(incident.severity)) return false;
        incident.severity = severity;
        incident.updatedAt = now.isUnknown() ? Timestamp::now() : now;
        incident.timeline.push_back({incident.updatedAt, actor, incident.state,
                                     incident.state,
                                     std::string("severity raised to ") +
                                         toString(severity)});
        return true;
    }
    return false;
}

EntityId IncidentManager::fromHealth(const BackendHealth& health, Timestamp now) {
    const bool degraded = health.aggregate != ServiceState::ONLINE;
    if (!degraded) {
        if (!healthIncidentId_.empty()) {
            transition(healthIncidentId_, IncidentState::RESOLVED, "health-monitor",
                       "aggregate health recovered", now);
        }
        return EntityId();
    }

    const IncidentSeverity severity =
        health.aggregate == ServiceState::ERROR ||
                health.aggregate == ServiceState::OFFLINE ||
                health.aggregate == ServiceState::BLOCKED
            ? IncidentSeverity::CRITICAL
            : IncidentSeverity::MAJOR;

    if (healthIncidentId_.empty()) {
        std::string summary = "aggregate=" + std::string(toString(health.aggregate));
        for (const auto& reason : health.degradedReasons) summary += " | " + reason;
        healthIncidentId_ =
            open("Backend health degraded", summary, severity, "health-monitor", now);
        return healthIncidentId_;
    }

    raiseSeverity(healthIncidentId_, severity, "health-monitor", now);
    return healthIncidentId_;
}

bool IncidentManager::get(const EntityId& incidentId, Incident& out) const {
    for (const auto& incident : incidents_) {
        if (incident.incidentId == incidentId) {
            out = incident;
            return true;
        }
    }
    return false;
}

std::vector<Incident> IncidentManager::all() const { return incidents_; }

std::vector<Incident> IncidentManager::active() const {
    std::vector<Incident> result;
    for (const auto& incident : incidents_) {
        if (incident.state != IncidentState::CLOSED &&
            incident.state != IncidentState::RESOLVED) {
            result.push_back(incident);
        }
    }
    return result;
}

}  // namespace aura
