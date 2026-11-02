// AYZR-LIFE-001 — Trade Lifecycle Manager implementation.

#include "ayzr/TradeLifecycle.h"

#include <algorithm>
#include <sstream>

namespace aura {
namespace ayzr {

namespace {

/// Map a reached target index to the matching status label.
TradeStatus statusForTarget(std::size_t index) {
    switch (index) {
        case 0: return TradeStatus::TP1_REACHED;
        case 1: return TradeStatus::TP2_REACHED;
        default: return TradeStatus::TP3_REACHED;
    }
}

}  // namespace

std::string TradeLifecycleManager::open(const TradeOrigin& origin) {
    TradeRecord rec;
    rec.origin = origin;
    rec.status = TradeStatus::AWAITING_ENTRY;
    rec.outcome = TradeOutcome::OPEN;
    rec.lastUpdateSec = origin.publishedAtSec;
    rec.lastKnownPrice = origin.entry;
    rec.lastKnownPriceAtSec = origin.publishedAtSec;

    std::ostringstream id;
    id << toString(origin.source) << "-" << nextId_++;
    rec.origin.tradeId = id.str();

    rec.events.push_back({origin.publishedAtSec, TradeStatus::AWAITING_ENTRY,
                          "أصدر المحرك إشارة جديدة"});
    trades_.push_back(std::move(rec));
    return trades_.back().origin.tradeId;
}

TradeRecord* TradeLifecycleManager::find(const std::string& tradeId) {
    for (auto& t : trades_) {
        if (t.origin.tradeId == tradeId) return &t;
    }
    return nullptr;
}

const TradeRecord* TradeLifecycleManager::find(
    const std::string& tradeId) const {
    for (const auto& t : trades_) {
        if (t.origin.tradeId == tradeId) return &t;
    }
    return nullptr;
}

bool TradeLifecycleManager::markTargetReached(const std::string& tradeId,
                                              std::size_t targetIndex,
                                              std::int64_t atSec) {
    TradeRecord* t = find(tradeId);
    if (t == nullptr || t->isClosed()) return false;
    if (targetIndex >= t->origin.targets.size()) return false;

    t->origin.targets[targetIndex].reached = true;
    t->origin.targets[targetIndex].reachedAtSec = atSec;
    // A reached target promotes the status but does NOT close the trade.
    if (!isTerminal(t->status)) {
        t->status = statusForTarget(targetIndex);
    }
    t->lastUpdateSec = atSec;
    std::ostringstream d;
    d << "تحقق الهدف رقم " << (targetIndex + 1);
    t->events.push_back({atSec, t->status, d.str()});
    return true;
}

bool TradeLifecycleManager::recommendClose(const std::string& tradeId,
                                           std::int64_t atSec,
                                           const std::string& reasonAr) {
    TradeRecord* t = find(tradeId);
    if (t == nullptr || t->isClosed()) return false;
    // A recommendation is NOT a close: the trade stays open, the outcome stays
    // OPEN, and the horizon slot remains occupied.
    t->status = TradeStatus::CLOSE_RECOMMENDED;
    t->lastUpdateSec = atSec;
    t->events.push_back({atSec, TradeStatus::CLOSE_RECOMMENDED,
                         "توصية إغلاق: " + reasonAr});
    return true;
}

bool TradeLifecycleManager::markWeakening(const std::string& tradeId,
                                          std::int64_t atSec,
                                          const std::string& reasonAr) {
    TradeRecord* t = find(tradeId);
    if (t == nullptr || t->isClosed()) return false;
    t->status = TradeStatus::WEAKENING;
    t->lastUpdateSec = atSec;
    t->events.push_back({atSec, TradeStatus::WEAKENING,
                         "ضعف الصفقة: " + reasonAr});
    return true;
}

bool TradeLifecycleManager::close(const std::string& tradeId,
                                  TradeStatus terminalStatus,
                                  std::int64_t atSec,
                                  const std::string& reasonAr) {
    TradeRecord* t = find(tradeId);
    if (t == nullptr || t->isClosed()) return false;
    if (!isTerminal(terminalStatus)) return false;  // must be a terminal state

    // Capture the pre-close status: invalidation before entry is a no-fill,
    // not a loss.
    const bool wasAwaitingEntry = (t->status == TradeStatus::AWAITING_ENTRY);

    t->status = terminalStatus;
    t->lastUpdateSec = atSec;
    t->closeReasonAr = reasonAr;

    switch (terminalStatus) {
        case TradeStatus::STOPPED_OUT:
            t->outcome = TradeOutcome::LOSS;
            break;
        case TradeStatus::CLOSED_AT_TARGET:
            t->outcome = TradeOutcome::WIN;
            break;
        case TradeStatus::INVALIDATED:
            t->outcome = wasAwaitingEntry ? TradeOutcome::BREAKEVEN
                                          : TradeOutcome::LOSS;
            break;
        case TradeStatus::CLOSED_MANUALLY:
            t->outcome = TradeOutcome::BREAKEVEN;
            break;
        default:
            break;
    }

    t->events.push_back({atSec, terminalStatus, "إغلاق: " + reasonAr});
    return true;
}

bool TradeLifecycleManager::markUndetermined(const std::string& tradeId,
                                             std::int64_t atSec,
                                             const std::string& reasonAr) {
    TradeRecord* t = find(tradeId);
    if (t == nullptr || t->isClosed()) return false;
    // TP and SL were both touched inside one candle and the order is unknown.
    // We refuse to guess: the outcome is explicitly undetermined.
    t->outcome = TradeOutcome::UNDETERMINED;
    t->lastUpdateSec = atSec;
    t->events.push_back({atSec, t->status,
                         "نتيجة غير محسومة: " + reasonAr});
    return true;
}

std::vector<TradeRecord> TradeLifecycleManager::all() const {
    std::vector<TradeRecord> out(trades_);
    std::reverse(out.begin(), out.end());  // newest first
    return out;
}

std::vector<TradeRecord> TradeLifecycleManager::openTrades() const {
    std::vector<TradeRecord> out;
    for (const auto& t : trades_) {
        if (t.isOpen()) out.push_back(t);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace ayzr
}  // namespace aura
