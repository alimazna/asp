#pragma once
// AYZR-LIFE-001 — Trade Lifecycle Manager.
//
// Owns every trade published by a horizon engine or by the news engine.
// Responsibilities:
//   * freeze the origin snapshot at publish time (never mutated afterwards),
//   * track lifecycle state with Arabic status labels,
//   * record which targets were hit and which remain,
//   * distinguish a CLOSE RECOMMENDATION from an actual close,
//   * release a horizon's slot only on a real close event.
//
// Safety rules honoured here:
//   * A recommendation to close is NOT a close. The slot stays occupied until
//     an explicit close is recorded.
//   * If TP and SL are both touched inside one candle and the ordering is
//     unknown, the outcome is recorded as UNDETERMINED, never guessed.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {

/// Where a trade came from. NEWS trades are separate from horizon trades and
/// do not consume a horizon slot.
enum class TradeSource {
    HORIZON_SHORT,
    HORIZON_MEDIUM,
    HORIZON_LONG,
    NEWS,
};

inline const char* toString(TradeSource s) noexcept {
    switch (s) {
        case TradeSource::HORIZON_SHORT:  return "SHORT";
        case TradeSource::HORIZON_MEDIUM: return "MEDIUM";
        case TradeSource::HORIZON_LONG:   return "LONG";
        case TradeSource::NEWS:           return "NEWS";
    }
    return "NEWS";
}

inline const char* sourceLabelAr(TradeSource s) noexcept {
    switch (s) {
        case TradeSource::HORIZON_SHORT:  return "قصير المدى";
        case TradeSource::HORIZON_MEDIUM: return "متوسط المدى";
        case TradeSource::HORIZON_LONG:   return "بعيد المدى";
        case TradeSource::NEWS:           return "صفقة خبر";
    }
    return "صفقة خبر";
}

enum class TradeDirection { BUY, SELL };

inline const char* directionLabelAr(TradeDirection d) noexcept {
    return d == TradeDirection::BUY ? "شراء" : "بيع";
}

/// Lifecycle status. Overlapping states are intentional: a trade can be
/// "active" AND "target 1 reached" at the same time.
enum class TradeStatus {
    AWAITING_ENTRY,       // بانتظار الدخول
    ACTIVE,               // صفقة نشطة
    TP1_REACHED,          // تحقق الهدف الأول
    TP2_REACHED,          // تحقق الهدف الثاني
    TP3_REACHED,          // تحقق الهدف الثالث
    WEAKENING,            // تحذير من ضعف الصفقة
    CLOSE_RECOMMENDED,    // يُنصح بإغلاق الصفقة
    INVALIDATED,          // فقدت الصفقة صلاحيتها
    STOPPED_OUT,          // تحقق وقف الخسارة
    CLOSED_AT_TARGET,     // أغلقت الصفقة عند الهدف
    CLOSED_MANUALLY,      // أغلقت الصفقة يدوياً
};

inline const char* statusLabelAr(TradeStatus s) noexcept {
    switch (s) {
        case TradeStatus::AWAITING_ENTRY:    return "بانتظار الدخول";
        case TradeStatus::ACTIVE:            return "صفقة نشطة";
        case TradeStatus::TP1_REACHED:       return "تحقق الهدف الأول";
        case TradeStatus::TP2_REACHED:       return "تحقق الهدف الثاني";
        case TradeStatus::TP3_REACHED:       return "تحقق الهدف الثالث";
        case TradeStatus::WEAKENING:         return "تحذير من ضعف الصفقة";
        case TradeStatus::CLOSE_RECOMMENDED: return "يُوصى بإغلاق الصفقة";
        case TradeStatus::INVALIDATED:       return "فقدت الصفقة صلاحيتها";
        case TradeStatus::STOPPED_OUT:       return "تحقق وقف الخسارة";
        case TradeStatus::CLOSED_AT_TARGET:  return "أغلقت الصفقة عند الهدف";
        case TradeStatus::CLOSED_MANUALLY:   return "أغلقت الصفقة يدوياً";
    }
    return "صفقة نشطة";
}

/// True when the status ends the trade and frees the horizon slot.
inline bool isTerminal(TradeStatus s) noexcept {
    switch (s) {
        case TradeStatus::INVALIDATED:
        case TradeStatus::STOPPED_OUT:
        case TradeStatus::CLOSED_AT_TARGET:
        case TradeStatus::CLOSED_MANUALLY:
            return true;
        default:
            return false;
    }
}

/// How the trade finally resolved. UNDETERMINED is used when TP and SL were
/// touched in the same candle and the order cannot be established.
enum class TradeOutcome { OPEN, WIN, LOSS, BREAKEVEN, UNDETERMINED };

inline const char* outcomeLabelAr(TradeOutcome o) noexcept {
    switch (o) {
        case TradeOutcome::OPEN:         return "قيد التنفيذ";
        case TradeOutcome::WIN:          return "رابحة";
        case TradeOutcome::LOSS:         return "خاسرة";
        case TradeOutcome::BREAKEVEN:    return "متعادلة";
        case TradeOutcome::UNDETERMINED: return "غير محسومة";
    }
    return "قيد التنفيذ";
}

/// A take-profit level.
struct TargetLevel {
    double price = 0.0;
    bool reached = false;
    std::int64_t reachedAtSec = 0;
};

/// The immutable origin snapshot, captured at publish time.
struct TradeOrigin {
    std::string tradeId;
    TradeSource source = TradeSource::NEWS;
    TradeDirection direction = TradeDirection::BUY;
    double entry = 0.0;
    double stop = 0.0;
    std::vector<TargetLevel> targets;
    std::int64_t publishedAtSec = 0;
    std::string entryTimeframe;                 // "M5", "M15", "H4"
    std::vector<std::string> contextTimeframes; // ["H1","H4"]
    std::string rationale;                      // engine reasoning (frozen)
    std::string engineVersion;                  // e.g. "AYZR-ENG-001"
    double originStrength = 0.0;                // engine rawStrength, frozen
    std::string symbol = "XAUUSD";
};

/// A lifecycle event: what changed, when, and why.
struct TradeEvent {
    std::int64_t atSec = 0;
    TradeStatus status = TradeStatus::ACTIVE;
    std::string detailAr;
};

/// The mutable lifecycle record around the frozen origin.
struct TradeRecord {
    TradeOrigin origin;
    TradeStatus status = TradeStatus::AWAITING_ENTRY;
    TradeOutcome outcome = TradeOutcome::OPEN;
    std::int64_t lastUpdateSec = 0;
    double lastKnownPrice = 0.0;
    std::int64_t lastKnownPriceAtSec = 0;
    std::string closeReasonAr;
    std::vector<TradeEvent> events;

    bool isOpen() const { return !isTerminal(status); }
    bool isClosed() const { return isTerminal(status); }
};

/// Manager owning all live and closed trades. The slot bookkeeping lives in
/// the HorizonEngine, but this manager is the single source of truth for
/// "did a real close happen?".
class TradeLifecycleManager {
public:
    /// Register a new trade from its origin snapshot. Returns the assigned id.
    std::string open(const TradeOrigin& origin);

    /// Look up a trade. Returns nullptr when unknown.
    TradeRecord* find(const std::string& tradeId);
    const TradeRecord* find(const std::string& tradeId) const;

    /// Record that a target level was reached. Does NOT close the trade — a
    /// reached target is not a close.
    bool markTargetReached(const std::string& tradeId, std::size_t targetIndex,
                           std::int64_t atSec);

    /// Record a close recommendation. The trade stays OPEN; only the status
    /// changes. This is explicitly NOT a close.
    bool recommendClose(const std::string& tradeId, std::int64_t atSec,
                        const std::string& reasonAr);

    /// Record a weakening warning without closing.
    bool markWeakening(const std::string& tradeId, std::int64_t atSec,
                       const std::string& reasonAr);

    /// Record a REAL close. This is the only call that ends a trade.
    bool close(const std::string& tradeId, TradeStatus terminalStatus,
               std::int64_t atSec, const std::string& reasonAr);

    /// Resolve an ambiguous candle where both TP and SL were touched and the
    /// order is unknown. Records UNDETERMINED rather than guessing.
    bool markUndetermined(const std::string& tradeId, std::int64_t atSec,
                          const std::string& reasonAr);

    /// All trades, newest first.
    std::vector<TradeRecord> all() const;

    /// Trades still open.
    std::vector<TradeRecord> openTrades() const;

    std::size_t size() const noexcept { return trades_.size(); }

private:
    std::vector<TradeRecord> trades_;
    std::uint64_t nextId_ = 1;
};

}  // namespace ayzr
}  // namespace aura
