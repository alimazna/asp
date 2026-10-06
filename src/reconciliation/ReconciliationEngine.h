#pragma once
// SHD-0006 - Reconciliation engine.
//
// Compares the in-memory simulated position state against the durable ledger.
// Discrepancies are reported explicitly; reconciliation never "fixes" state by
// overwriting the record of what happened.

#include "persistence/IPersistenceStore.h"
#include "shadow/PositionSimulator.h"

#include <string>
#include <vector>

namespace aura {

enum class ReconciliationFinding {
    MATCH,
    MISSING_IN_LEDGER,
    MISSING_IN_MEMORY,
    FIELD_MISMATCH,
};

inline const char* toString(ReconciliationFinding f) noexcept {
    switch (f) {
        case ReconciliationFinding::MATCH:              return "MATCH";
        case ReconciliationFinding::MISSING_IN_LEDGER:  return "MISSING_IN_LEDGER";
        case ReconciliationFinding::MISSING_IN_MEMORY:  return "MISSING_IN_MEMORY";
        case ReconciliationFinding::FIELD_MISMATCH:     return "FIELD_MISMATCH";
    }
    return "UNKNOWN";
}

struct ReconciliationItem {
    std::string positionId;
    ReconciliationFinding finding = ReconciliationFinding::MATCH;
    std::string detail;
};

struct ReconciliationReport {
    bool clean = false;
    std::size_t compared = 0;
    std::size_t matched = 0;
    std::size_t mismatched = 0;
    std::vector<ReconciliationItem> items;
    Timestamp ranAt;
};

class ReconciliationEngine {
public:
    // The ledger collection keyed by position id; payload is a compact
    // "entry|stop|target|lots|state|exit|pnl" encoding written by the
    // persistence engine.
    ReconciliationEngine(const IPersistenceStore* store,
                         std::string collection = "shadow_positions")
        : store_(store), collection_(std::move(collection)) {}

    ReconciliationReport reconcile(
        const std::vector<SimulatedPosition>& positions, Timestamp now) const;

private:
    const IPersistenceStore* store_;
    std::string collection_;
};

}  // namespace aura
