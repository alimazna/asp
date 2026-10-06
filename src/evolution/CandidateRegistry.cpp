// EVO-0004 - Candidate registry implementation.

#include "evolution/CandidateRegistry.h"

#include <sstream>

namespace aura {

namespace {

std::string escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '|' || c == '\\' || c == '\n' || c == ',' || c == '=') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == delimiter) {
            parts.push_back(current);
            current.clear();
        } else if (text[i] == '\\' && i + 1 < text.size()) {
            current.push_back(text[++i]);
        } else {
            current.push_back(text[i]);
        }
    }
    parts.push_back(current);
    return parts;
}

CandidateStatus parseStatus(const std::string& text) {
    for (CandidateStatus candidate :
         {CandidateStatus::GENERATED, CandidateStatus::UNDER_VALIDATION,
          CandidateStatus::VALIDATED, CandidateStatus::REJECTED,
          CandidateStatus::PROMOTED, CandidateStatus::RETIRED}) {
        if (text == toString(candidate)) return candidate;
    }
    return CandidateStatus::GENERATED;
}

}  // namespace

std::string CandidateRegistry::encode(const RegisteredCandidate& entry) const {
    std::ostringstream out;
    out << escape(entry.candidate.candidateId.value()) << "|"
        << escape(entry.candidate.originHypothesis.value()) << "|"
        << escape(entry.candidate.label) << "|";
    bool first = true;
    for (const auto& kv : entry.candidate.parameters) {
        if (!first) out << ",";
        out << escape(kv.first) << "=" << kv.second;
        first = false;
    }
    out << "|" << toString(entry.status) << "|" << escape(entry.reason) << "|"
        << entry.registeredAt.epochMillis() << "|" << entry.updatedAt.epochMillis();
    return out.str();
}

bool CandidateRegistry::decode(const std::string& payload,
                               RegisteredCandidate& out) const {
    const auto parts = split(payload, '|');
    if (parts.size() < 8) return false;
    out.candidate.candidateId = EntityId(parts[0]);
    out.candidate.originHypothesis = EntityId(parts[1]);
    out.candidate.label = parts[2];
    out.candidate.valid = true;
    if (!parts[3].empty()) {
        for (const auto& pair : split(parts[3], ',')) {
            const auto kv = split(pair, '=');
            if (kv.size() != 2) continue;
            try {
                out.candidate.parameters[kv[0]] = std::stod(kv[1]);
            } catch (...) {
            }
        }
    }
    out.status = parseStatus(parts[4]);
    out.reason = parts[5];
    try {
        out.registeredAt = Timestamp::fromEpochMillis(std::stoll(parts[6]));
        out.updatedAt = Timestamp::fromEpochMillis(std::stoll(parts[7]));
    } catch (...) {
        return false;
    }
    return true;
}

void CandidateRegistry::persist(const RegisteredCandidate& entry) {
    if (store_ == nullptr || !store_->isAvailable()) return;
    PersistenceRecordMetadata metadata;
    metadata.recordId = entry.candidate.candidateId;
    metadata.recordType = collection_;
    metadata.schemaVersion = Version{1, 0, 0};
    metadata.createdAt = entry.registeredAt;
    metadata.updatedAt = entry.updatedAt;
    metadata.idempotencyKey = entry.candidate.candidateId.value();
    store_->put(collection_, entry.candidate.candidateId.value(), encode(entry),
                metadata);
}

bool CandidateRegistry::registerCandidate(const Candidate& candidate, Timestamp now) {
    if (!candidate.isValid()) return false;
    if (candidates_.find(candidate.candidateId) != candidates_.end()) return false;

    RegisteredCandidate entry;
    entry.candidate = candidate;
    entry.status = CandidateStatus::GENERATED;
    entry.registeredAt = now.isUnknown() ? Timestamp::now() : now;
    entry.updatedAt = entry.registeredAt;
    candidates_[candidate.candidateId] = entry;
    persist(entry);
    return true;
}

bool CandidateRegistry::setStatus(const EntityId& candidateId,
                                  CandidateStatus status,
                                  const std::string& reason, Timestamp now) {
    auto it = candidates_.find(candidateId);
    if (it == candidates_.end()) return false;
    it->second.status = status;
    it->second.reason = reason;
    it->second.updatedAt = now.isUnknown() ? Timestamp::now() : now;
    it->second.candidate.active = (status == CandidateStatus::PROMOTED);
    persist(it->second);
    return true;
}

bool CandidateRegistry::get(const EntityId& candidateId,
                            RegisteredCandidate& out) const {
    auto it = candidates_.find(candidateId);
    if (it == candidates_.end()) return false;
    out = it->second;
    return true;
}

std::vector<RegisteredCandidate> CandidateRegistry::all() const {
    std::vector<RegisteredCandidate> result;
    for (const auto& kv : candidates_) result.push_back(kv.second);
    return result;
}

std::vector<RegisteredCandidate> CandidateRegistry::byStatus(
    CandidateStatus status) const {
    std::vector<RegisteredCandidate> result;
    for (const auto& kv : candidates_) {
        if (kv.second.status == status) result.push_back(kv.second);
    }
    return result;
}

std::size_t CandidateRegistry::loadFromStore() {
    if (store_ == nullptr || !store_->isAvailable()) return 0;
    std::size_t loaded = 0;
    for (const auto& key : store_->keys(collection_)) {
        std::string payload;
        PersistenceRecordMetadata metadata;
        if (store_->get(collection_, key, payload, metadata) != PersistenceStatus::OK) {
            continue;
        }
        RegisteredCandidate entry;
        if (decode(payload, entry)) {
            candidates_[entry.candidate.candidateId] = entry;
            ++loaded;
        }
    }
    return loaded;
}

}  // namespace aura
