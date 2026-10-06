// SHD-0007 - Reconciliation implementation.

#include "reconciliation/ReconciliationEngine.h"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace aura {

namespace {

double parseField(const std::string& payload, std::size_t index) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < index; ++i) {
        start = payload.find('|', start);
        if (start == std::string::npos) return 0.0;
        ++start;
    }
    const std::size_t end = payload.find('|', start);
    const std::string field =
        payload.substr(start, end == std::string::npos ? std::string::npos
                                                       : end - start);
    try {
        return std::stod(field);
    } catch (...) {
        return 0.0;
    }
}

}  // namespace

ReconciliationReport ReconciliationEngine::reconcile(
    const std::vector<SimulatedPosition>& positions, Timestamp now) const {
    ReconciliationReport report;
    report.ranAt = now;
    report.clean = true;

    if (store_ == nullptr) {
        report.clean = false;
        report.items.push_back({"", ReconciliationFinding::MISSING_IN_LEDGER,
                                "no persistence store available"});
        ++report.mismatched;
        return report;
    }

    std::vector<std::string> ledgerIds;
    if (store_->isAvailable()) {
        ledgerIds = store_->keys(collection_);
    }

    // Positions present in memory.
    std::vector<std::string> memoryIds;
    for (const auto& position : positions) {
        const std::string id = position.positionId.value();
        memoryIds.push_back(id);
        ++report.compared;

        std::string payload;
        PersistenceRecordMetadata metadata;
        const PersistenceStatus status =
            store_->get(collection_, id, payload, metadata);
        if (status == PersistenceStatus::NOT_FOUND) {
            report.items.push_back({id, ReconciliationFinding::MISSING_IN_LEDGER,
                                    "position not persisted"});
            ++report.mismatched;
            report.clean = false;
            continue;
        }

        // Compare a subset of critical fields.
        const double ledgerEntry = parseField(payload, 0);
        const double ledgerStop = parseField(payload, 1);
        const double ledgerLots = parseField(payload, 3);
        const double epsilon = 1e-9;
        if (std::abs(ledgerEntry - position.entryPrice) > epsilon ||
            std::abs(ledgerStop - position.stopPrice) > epsilon ||
            std::abs(ledgerLots - position.lots) > epsilon) {
            report.items.push_back({id, ReconciliationFinding::FIELD_MISMATCH,
                                    "entry/stop/lots differ from ledger"});
            ++report.mismatched;
            report.clean = false;
        } else {
            report.items.push_back({id, ReconciliationFinding::MATCH, "ok"});
            ++report.matched;
        }
    }

    // Ledger entries absent from memory.
    for (const auto& ledgerId : ledgerIds) {
        bool found = false;
        for (const auto& memoryId : memoryIds) {
            if (memoryId == ledgerId) { found = true; break; }
        }
        if (!found) {
            report.items.push_back({ledgerId, ReconciliationFinding::MISSING_IN_MEMORY,
                                    "ledger entry has no live position"});
            ++report.mismatched;
            report.clean = false;
        }
    }

    return report;
}

}  // namespace aura
