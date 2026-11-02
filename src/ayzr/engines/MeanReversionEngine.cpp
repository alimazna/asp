// AYZR-ENG-102 — Mean Reversion Engine (implementation).

#include "ayzr/engines/MeanReversionEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

std::string MeanReversionEngine::id() const {
    return std::string("mean-reversion-") + toString(tf_);
}

std::vector<EngineResult> MeanReversionEngine::evaluate(const MarketView& view) const {
    std::vector<EngineResult> out;
    const std::size_t need = std::max({cfg_.meanPeriod + 4, cfg_.zPeriod + 4,
                                       cfg_.atrPeriod + 4});
    if (!view.has(need)) return out;

    auto mean = sma(view.bars, cfg_.meanPeriod);
    auto sd = stdev(view.bars, cfg_.zPeriod);
    auto a = atr(view.bars, cfg_.atrPeriod);
    if (!mean || !sd || !a) return out;
    if (*sd == 0.0) return out;   // no dispersion => no deviation to trade

    const double price = view.bars.back().close;
    const double z = (price - *mean) / *sd;

    // Trend guard: measure the slope of a longer EMA, normalised by ATR.
    std::vector<Bar> prev(view.bars.begin(), view.bars.end() - 3);
    auto longNow = ema(view.bars, std::min<std::size_t>(50, view.bars.size() - 1));
    auto longPrev = ema(prev, std::min<std::size_t>(50, prev.size() - 1));
    double slopeAtr = 0.0;
    if (longNow && longPrev && *a > 0.0) slopeAtr = (*longNow - *longPrev) / *a;
    const bool trending = std::fabs(slopeAtr) > cfg_.maxTrendSlopeAtr;

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

    if (trending) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WITHHELD;
        base.reasonCodes = {"strong_trend_refusal"};
        base.contradictingEvidence = {
            {"trend_present", "السوق في اتجاه قوي؛ لا يجوز معاكسته", slopeAtr}};
        base.explanationAr =
            "تم رفض الإشارة: السوق في اتجاه قوي ولا يُنصح بمعاكسته.";
        out.push_back(std::move(base));
        return out;
    }

    if (std::fabs(z) < cfg_.zEntry) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WITHHELD;
        base.reasonCodes = {"deviation_below_threshold"};
        base.explanationAr =
            "الانحراف عن المتوسط دون الحد المطلوب؛ لا توجد فرصة عودة للمتوسط.";
        out.push_back(std::move(base));
        return out;
    }

    const bool stretchedDown = z <= -cfg_.zEntry;   // price far below mean => buy
    base.status = EventStatus::PENDING_CONFIRMATION;
    base.expiresAt = view.snapshotUtc + cfg_.expirySeconds;
    base.direction = stretchedDown ? EngineDirection::BULLISH : EngineDirection::BEARISH;
    base.entry = PriceLevel{price, "engine_last_close", ""};
    if (stretchedDown) {
        base.stopLoss = PriceLevel{price - cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{price + m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    } else {
        base.stopLoss = PriceLevel{price + cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{price - m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    }
    base.reasonCodes = {"range_regime", "deviation_exceeds_threshold"};
    base.supportingEvidence = {
        {"z_score", "انحراف السعر عن المتوسط بوحدات الانحراف المعياري", z},
        {"range_guard", "لا يوجد اتجاه قوي يستدعي تجنب المعاكسة", slopeAtr},
        {"mean", "متوسط الفترة المستخدم", *mean},
    };
    base.explanationAr = stretchedDown
        ? "انحراف هبوطي مبالغ فيه ضمن سوق عرضي؛ فرصة عودة نحو المتوسط."
        : "انحراف صعودي مبالغ فيه ضمن سوق عرضي؛ فرصة عودة نحو المتوسط.";
    out.push_back(std::move(base));
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
