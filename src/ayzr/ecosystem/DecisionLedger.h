#pragma once
// AYZR-LED-001 — Central Decision Ledger.
//
// An append-only event store: every engine event, opportunity creation, merge,
// conflict, publication, withholding, risk alert and engine failure is recorded
// with its time, identity, reason and correlation id. Rows are never updated or
// deleted by this API — the ledger is the audit trail.
//
// Storage is SQLite (the backend's existing dependency). An in-memory mode is
// provided for tests and for a read-only observatory that has no store.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace aura {
namespace ayzr {
namespace eco {

/// Ledger event kinds. ASCII ids are stable; Arabic labels are for the UI.
enum class LedgerEventType {
    ENGINE_EVENT,
    OPPORTUNITY_CREATED,
    OPPORTUNITY_UPDATED,
    OPPORTUNITY_MERGED,
    CONFLICT_DETECTED,
    DECISION_PUBLISHED,
    DECISION_WITHHELD,
    DECISION_WATCH,
    RISK_ALERT,
    ENGINE_FAILURE,
    DATA_QUALITY_FAILURE,
    OPPORTUNITY_EXPIRED,
    SESSION_STATE,
};

const char* toString(LedgerEventType t) noexcept;
const char* ledgerEventLabelAr(LedgerEventType t) noexcept;

struct LedgerEntry {
    std::int64_t seq = 0;          // assigned on append
    std::int64_t atUtc = 0;        // epoch seconds
    LedgerEventType type = LedgerEventType::ENGINE_EVENT;
    std::string correlationId;
    std::string opportunityId;     // empty when not applicable
    std::string engineId;          // empty when not applicable
    std::string reasonCode;        // stable machine token
    std::string detailAr;          // Arabic human-readable detail
    std::string payloadJson;       // optional structured payload
};

class DecisionLedger {
public:
    DecisionLedger() = default;
    ~DecisionLedger();
    DecisionLedger(const DecisionLedger&) = delete;
    DecisionLedger& operator=(const DecisionLedger&) = delete;

    /// Open a SQLite-backed ledger. Returns false on failure (caller keeps an
    /// in-memory ledger). Schema is created if absent; existing rows are kept.
    bool open(const std::string& path);

    /// True when backed by SQLite (persistent) rather than memory-only.
    bool persistent() const noexcept { return db_ != nullptr; }

    /// Append one entry. Returns the assigned sequence, or 0 on failure.
    /// Never mutates or removes an existing row.
    std::int64_t append(LedgerEntry entry);

    /// Read the newest `limit` entries (newest last). `limit == 0` => all.
    std::vector<LedgerEntry> recent(std::size_t limit) const;

    /// All entries for one opportunity id, oldest first.
    std::vector<LedgerEntry> byOpportunity(const std::string& opportunityId) const;

    /// All entries for one correlation id, oldest first.
    std::vector<LedgerEntry> byCorrelation(const std::string& correlationId) const;

    /// Count of entries of a type (used by health/observability views).
    std::int64_t countOf(LedgerEventType type) const;

    std::size_t size() const;

private:
    mutable std::mutex mu_;
    sqlite3* db_ = nullptr;
    std::vector<LedgerEntry> mem_;
    std::int64_t nextSeq_ = 1;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
