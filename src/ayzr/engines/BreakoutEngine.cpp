// AYZR-ENG-103 — Breakout Engine (implementation).

#include "ayzr/engines/BreakoutEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

std::string BreakoutEngine::id() const {
    return std::string("breakout-") + toString(tf_);
}

std::vector<EngineResult> BreakoutEngine::evaluate(const MarketView& view) const {
    std::vector<EngineResult> out;
    const std::size_t need = std::max(cfg_.rangeLookback + 2, cfg_.atrPeriod + 2);
    if (!view.has(need)) return out;

    auto a = atr(view.bars, cfg_.atrPeriod);
    if (!a || *a <= 0.0) return out;

    // The box is the lookback window EXCLUDING the newest bar, so the newest bar
    // is the one that may break the box.
    const std::size_t n = view.bars.size();
    const std::size_t start = n - 1 - cfg_.rangeLookback;
    double hi = view.bars[start].high;
    double lo = view.bars[start].low;
    for (std::size_t i = start; i < n - 1; ++i) {
        hi = std::max(hi, view.bars[i].high);
        lo = std::min(lo, view.bars[i].low);
    }
    const double height = hi - lo;
    const double compression = height / *a;
    const double buffer = cfg_.bufferAtr * *a;
    const Bar& last = view.bars.back();
    const double close = last.close;

    EngineResult base;
    base.engineId = id();
    base.engineVersion = version();
    base.strategyFamily = family();
    base.eventType = "signal";
    base.symbol = view.symbol;
    base.timeframe = tf_;
    base.dataTimestamp = view.latestOpenUtc;
    base.createdAt = view.snapshotUtc;
    base.updatedAt = view.snapshotUtc;
    base.dataSource = view.dataSource;
    base.dataQuality = view.quality;
    base.inputSnapshotReference = id() + "@" + std::to_string(view.latestOpenUtc);
    base.correlationId = std::to_string(view.latestOpenUtc);
    base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);

    const bool brokeUp = close > hi + buffer;
    const bool brokeDown = close < lo - buffer;

    if (!brokeUp && !brokeDown) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WITHHELD;
        base.reasonCodes = {"no_breakout"};
        base.supportingEvidence = {
            {"range_high", "قمة النطاق", hi},
            {"range_low", "قاع النطاق", lo},
            {"compression", "نسبة ارتفاع النطاق إلى ATR", compression},
        };
        base.explanationAr =
            "لم يتحقق اختراق مؤكد لحدود النطاق حتى الآن.";
        out.push_back(std::move(base));
        return out;
    }

    // A break out of an already-wide range is a chase, not a breakout.
    if (compression > cfg_.maxCompressionAtr) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WITHHELD;
        base.reasonCodes = {"range_not_compressed"};
        base.contradictingEvidence = {
            {"compression", "النطاق واسع نسبةً إلى ATR؛ الاختراق مطاردة لا انفجار",
             compression}};
        base.explanationAr =
            "تم حجب الإشارة: النطاق غير منضغط، والاختراق يشبه المطاردة.";
        out.push_back(std::move(base));
        return out;
    }

    base.status = EventStatus::PENDING_CONFIRMATION;   // never auto-PUBLISH
    base.expiresAt = view.snapshotUtc + cfg_.expirySeconds;
    base.entry = PriceLevel{close, "engine_last_close", ""};
    if (brokeUp) {
        base.direction = EngineDirection::BULLISH;
        base.stopLoss = PriceLevel{hi - cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف أسفل حد النطاق المكسور"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{close + m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    } else {
        base.direction = EngineDirection::BEARISH;
        base.stopLoss = PriceLevel{lo + cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف أعلى حد النطاق المكسور"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{close - m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    }
    base.reasonCodes = {"range_compressed", "close_beyond_range_buffer",
                        "session_activated_monitoring"};
    base.supportingEvidence = {
        {"range_high", "قمة النطاق قبل الاختراق", hi},
        {"range_low", "قاع النطاق قبل الاختراق", lo},
        {"compression", "النطاق منضغط نسبةً إلى ATR", compression},
        {"break_buffer", "مقدار تجاوز حد النطاق", buffer},
    };
    base.explanationAr = brokeUp
        ? "اختراق صاعد مؤكد لنطاق منضغط، وافتتاح الجلسة فعّل المراقبة."
        : "اختراق هابط مؤكد لنطاق منضغط، وافتتاح الجلسة فعّل المراقبة.";
    out.push_back(std::move(base));
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
