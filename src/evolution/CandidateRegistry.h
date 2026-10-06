#pragma once
// EVO-0003 - Candidate registry.
//
// Durable registry of every generated candidate and its status. Candidates are
// never activated by the registry; activation requires the governance gate.
// History is append-only: a candidate is retired, never erased.

#include "evolution/CandidateGenerator.h"
#include "persistence/IPersistenceStore.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

enum class CandidateStatus {
    GENERATED,
    UNDER_VALIDATION,
    VALIDATED,
    REJECTED,
    PROMOTED,
    RETIRED,
};

inline const char* toString(CandidateStatus s) noexcept {
    switch (s) {
        case CandidateStatus::GENERATED:        return "GENERATED";
        case CandidateStatus::UNDER_VALIDATION: return "UNDER_VALIDATION";
        case CandidateStatus::VALIDATED:        return "VALIDATED";
        case CandidateStatus::REJECTED:         return "REJECTED";
        case CandidateStatus::PROMOTED:         return "PROMOTED";
        case CandidateStatus::RETIRED:          return "RETIRED";
    }
    return "GENERATED";
}

struct RegisteredCandidate {
    Candidate candidate;
    CandidateStatus status = CandidateStatus::GENERATED;
    std::string reason;
    Timestamp registeredAt;
    Timestamp updatedAt;
};

class CandidateRegistry {
public:
    explicit CandidateRegistry(IPersistenceStore* store = nullptr,
                               std::string collection = "candidates")
        : store_(store), collection_(std::move(collection)) {}

    bool registerCandidate(const Candidate& candidate, Timestamp now);

    bool setStatus(const EntityId& candidateId, CandidateStatus status,
                   const std::string& reason, Timestamp now);

    bool get(const EntityId& candidateId, RegisteredCandidate& out) const;
    std::vector<RegisteredCandidate> all() const;
    std::vector<RegisteredCandidate> byStatus(CandidateStatus status) const;

    std::size_t loadFromStore();
    std::size_t size() const noexcept { return candidates_.size(); }

private:
    std::string encode(const RegisteredCandidate& entry) const;
    bool decode(const std::string& payload, RegisteredCandidate& out) const;
    void persist(const RegisteredCandidate& entry);

    IPersistenceStore* store_;
    std::string collection_;
    std::map<EntityId, RegisteredCandidate> candidates_;
};

}  // namespace aura
