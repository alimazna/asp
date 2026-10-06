#pragma once
// GOV-0009 - Incident manager.
//
// Tracks operational incidents from detection to closure. Incidents are
// append-only; severity may only be raised, never silently lowered, and every
// state change is attributed.

#include "foundation/EntityId.h"
#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"
#include "health/HealthMonitor.h"

#include <string>
#include <vector>

namespace aura {

enum class IncidentSeverity {
    INFO,
    MINOR,
    MAJOR,
    CRITICAL,
};

inline const char* toString(IncidentSeverity s) noexcept {
    switch (s) {
        case IncidentSeverity::INFO:     return "INFO";
        case IncidentSeverity::MINOR:    return "MINOR";
        case IncidentSeverity::MAJOR:    return "MAJOR";
        case IncidentSeverity::CRITICAL: return "CRITICAL";
    }
    return "INFO";
}

enum class IncidentState {
    OPEN,
    ACKNOWLEDGED,
    MITIGATING,
    RESOLVED,
    CLOSED,
};

inline const char* toString(IncidentState s) noexcept {
    switch (s) {
        case IncidentState::OPEN:         return "OPEN";
        case IncidentState::ACKNOWLEDGED: return "ACKNOWLEDGED";
        case IncidentState::MITIGATING:   return "MITIGATING";
        case IncidentState::RESOLVED:     return "RESOLVED";
        case IncidentState::CLOSED:       return "CLOSED";
    }
    return "OPEN";
}

struct IncidentEvent {
    Timestamp at;
    std::string actor;
    IncidentState from;
    IncidentState to;
    std::string note;
};

struct Incident {
    EntityId incidentId;
    std::string title;
    std::string summary;
    IncidentSeverity severity = IncidentSeverity::MINOR;
    IncidentState state = IncidentState::OPEN;
    Timestamp openedAt;
    Timestamp updatedAt;
    std::vector<IncidentEvent> timeline;

    bool valid() const noexcept { return !incidentId.empty(); }
};

class IncidentManager {
public:
    IncidentManager() = default;

    EntityId open(const std::string& title, const std::string& summary,
                  IncidentSeverity severity, const std::string& actor,
                  Timestamp now);

    bool transition(const EntityId& incidentId, IncidentState to,
                    const std::string& actor, const std::string& note,
                    Timestamp now);

    bool raiseSeverity(const EntityId& incidentId, IncidentSeverity severity,
                       const std::string& actor, Timestamp now);

    // Derive an incident from backend health: a non-online aggregate opens or
    // escalates a health incident.
    EntityId fromHealth(const BackendHealth& health, Timestamp now);

    bool get(const EntityId& incidentId, Incident& out) const;
    std::vector<Incident> all() const;
    std::vector<Incident> active() const;

private:
    static int severityRank(IncidentSeverity s) noexcept;

    std::vector<Incident> incidents_;
    EntityId healthIncidentId_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
