// AYZR-ENG-101 — Trend Following Engine (implementation).

#include "ayzr/engines/TrendFollowingEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

std::string TrendFollowingEngine::id() const {
    return std::string("trend-following-") + toString(tf_);
}

std::vector<EngineResult> TrendFollowingEngine::evaluate(const MarketView& view) const {
    std::vector<EngineResult> out;
    const std::size_t need = std::max({cfg_.trendEma + 2, cfg_.slowEma + 2,
                                       cfg_.atrPeriod + 2, cfg_.rsiPeriod + 2});
    if (!view.has(need)) return out;   // not enough history: stay silent

    auto fast = ema(view.bars, cfg_.fastEma);
    auto slow = ema(view.bars, cfg_.slowEma);
    auto trend = ema(view.bars, cfg_.trendEma);
    auto a = atr(view.bars, cfg_.atrPeriod);
    auto r = rsi(view.bars, cfg_.rsiPeriod);
    if (!fast || !slow || !trend || !a || !r) return out;

    // Slow-EMA slope over the last few bars, normalised by ATR, so "sloping" is
    // measured, not assumed. We re-evaluate the slow EMA on a truncated window.
    std::vector<Bar> prev(view.bars.begin(), view.bars.end() - 3);
    auto slowPrev = ema(prev, cfg_.slowEma);
    const double slopeAtr = slowPrev ? (*slow - *slowPrev) / (*a == 0.0 ? 1.0 : *a)
                                     : 0.0;

    const double price = view.bars.back().close;
    const bool bullAlign = *fast > *slow && price > *trend && slopeAtr > cfg_.minSlowEmaSlopeAtr;
    const bool bearAlign = *fast < *slow && price < *trend && slopeAtr < -cfg_.minSlowEmaSlopeAtr;

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
    base.inputSnapshotReference =
        id() + "@" + std::to_string(view.latestOpenUtc);
    base.correlationId = std::to_string(view.latestOpenUtc);

    // Momentum must lean the right way, else this is not a trend entry.
    if (bullAlign && *r >= cfg_.minRsiForLong) {
        base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);
        base.direction = EngineDirection::BULLISH;
        base.status = EventStatus::PENDING_CONFIRMATION;
        base.expiresAt = view.snapshotUtc + cfg_.expirySeconds;
        base.entry = PriceLevel{price, "engine_last_close", ""};
        base.stopLoss = PriceLevel{price - cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples) {
            base.takeProfitLevels.push_back(
                PriceLevel{price + m * *a, "engine_atr", "هدف وفق تقلب ATR"});
        }
        base.reasonCodes = {"ema_fast_above_slow", "price_above_trend_ema",
                            "trend_ema_rising", "rsi_momentum_positive"};
        base.supportingEvidence = {
            {"ema_alignment", "المتوسط السريع أعلى من البطيء", *fast - *slow},
            {"trend_filter", "السعر فوق متوسط الاتجاه", price - *trend},
            {"slope", "ميل متوسط الاتجاه صاعد مقارنةً بـ ATR", slopeAtr},
            {"momentum", "مؤشر RSI يدعم الاتجاه الصاعد", *r},
        };
        base.explanationAr =
            "اتجاه صاعد مؤكد بتوافق المتوسطات وميل متوسط الاتجاه ودعم الزخم.";
        base.confidence.score = std::nullopt;
        out.push_back(std::move(base));
        return out;
    }
    if (bearAlign && *r <= cfg_.maxRsiForShort) {
        base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);
        base.direction = EngineDirection::BEARISH;
        base.status = EventStatus::PENDING_CONFIRMATION;
        base.expiresAt = view.snapshotUtc + cfg_.expirySeconds;
        base.entry = PriceLevel{price, "engine_last_close", ""};
        base.stopLoss = PriceLevel{price + cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples) {
            base.takeProfitLevels.push_back(
                PriceLevel{price - m * *a, "engine_atr", "هدف وفق تقلب ATR"});
        }
        base.reasonCodes = {"ema_fast_below_slow", "price_below_trend_ema",
                            "trend_ema_falling", "rsi_momentum_negative"};
        base.supportingEvidence = {
            {"ema_alignment", "المتوسط السريع أدنى من البطيء", *fast - *slow},
            {"trend_filter", "السعر تحت متوسط الاتجاه", price - *trend},
            {"slope", "ميل متوسط الاتجاه هابط مقارنةً بـ ATR", slopeAtr},
            {"momentum", "مؤشر RSI يدعم الاتجاه الهابط", *r},
        };
        base.explanationAr =
            "اتجاه هابط مؤكد بتوافق المتوسطات وميل متوسط الاتجاه ودعم الزخم.";
        out.push_back(std::move(base));
        return out;
    }

    // No confirmed trend: emit an honest NO_TRADE so the UI can explain silence.
    base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);
    base.direction = EngineDirection::NO_TRADE;
    base.status = EventStatus::WITHHELD;
    base.reasonCodes = {"no_confirmed_trend"};
    base.explanationAr = "لا يوجد اتجاه مؤكد؛ شروط التتبع غير مكتملة.";
    out.push_back(std::move(base));
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
