// AYZR-ENG-105 — Event Driven Engine (implementation).

#include "ayzr/engines/EventDrivenEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

std::string EventDrivenEngine::id() const {
    return std::string("event-driven-") + toString(tf_);
}

EngineHealth EventDrivenEngine::health(const MarketView& view,
                                       std::int64_t nowUtc) const {
    if (view.bars.empty()) return EngineHealth::WAITING_FOR_DATA;
    if (view.latestOpenUtc > 0 && nowUtc > 0 &&
        nowUtc - view.latestOpenUtc > cfg_.maxDataAgeSeconds) {
        return EngineHealth::STALE_OUTPUT;
    }
    return StrategyEngine::health(view, nowUtc);
}

std::vector<EngineResult> EventDrivenEngine::evaluate(const MarketView& view) const {
    (void)view;
    return {};   // no event => no opinion (this engine has no OHLC-only edge)
}

std::vector<EngineResult> EventDrivenEngine::evaluateEvent(
    const MarketView& view, const EconomicEvent& ev, std::int64_t nowUtc) const {
    std::vector<EngineResult> out;

    EngineResult base;
    base.engineId = id();
    base.engineVersion = version();
    base.strategyFamily = family();
    base.eventType = "signal";
    base.symbol = view.symbol;
    base.timeframe = tf_;
    base.dataTimestamp = view.latestOpenUtc;
    base.createdAt = nowUtc;
    base.updatedAt = nowUtc;
    base.dataSource = view.dataSource;
    base.dataQuality = view.quality;
    base.inputSnapshotReference = id() + "@" + ev.eventId;
    base.correlationId = ev.eventId;
    base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);

    // ---- Condition 1: credible + recent event --------------------------------
    const bool recent = ev.publishedAt > 0 &&
                        nowUtc - ev.publishedAt <= cfg_.maxNewsAgeSeconds;
    if (!ev.verified || !recent) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WARNING;
        base.reasonCodes.push_back(!ev.verified ? "event_unverified" : "event_stale");
        base.riskFlags.push_back(!ev.verified ? "rumour" : "stale_news");
        base.explanationAr = !ev.verified
            ? "خبر غير مؤكد: يُعرض كتحذير فقط دون صفقة."
            : "خبر قديم: خارج نافذة الحداثة المسموحة.";
        if (!ev.summaryAr.empty()) base.supportingEvidence.push_back(
            {"event_summary", ev.summaryAr, std::nullopt});
        out.push_back(std::move(base));
        return out;
    }

    // ---- Condition 2: fresh market data --------------------------------------
    const bool fresh = view.latestOpenUtc > 0 &&
                       nowUtc - view.latestOpenUtc <= cfg_.maxDataAgeSeconds;
    if (!fresh || view.bars.empty()) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WARNING;
        base.reasonCodes.push_back("market_data_stale_or_missing");
        base.riskFlags.push_back("stale_market_data");
        base.explanationAr = "بيانات السوق غير حديثة؛ لا يمكن بناء صفقة على الخبر.";
        out.push_back(std::move(base));
        return out;
    }

    // ---- Condition 3: strategy conditions met --------------------------------
    auto a = atr(view.bars, cfg_.atrPeriod);
    const bool hasBias = ev.goldBias.has_value() &&
                         std::fabs(*ev.goldBias) >= cfg_.minAbsBias;
    if (!hasBias || !a || *a <= 0.0) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WARNING;
        base.reasonCodes.push_back(hasBias ? "insufficient_atr" : "bias_below_threshold");
        base.explanationAr =
            "الخبر مؤكد وحديث لكن شروط الاستراتيجية غير مكتملة؛ يُعرض كتحذير.";
        out.push_back(std::move(base));
        return out;
    }

    // ---- Condition 5: rule set independently tested --------------------------
    if (!cfg_.ruleSetTested) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WARNING;
        base.reasonCodes.push_back("ruleset_not_independently_tested");
        base.riskFlags.push_back("unvalidated_ruleset");
        base.explanationAr =
            "قواعد المحرك لم تُختبر استقلالياً بعد؛ لا يتم إصدار صفقة.";
        out.push_back(std::move(base));
        return out;
    }

    // ---- Condition 4: explainable entry/SL/TP --------------------------------
    const double price = view.bars.back().close;
    const double bias = *ev.goldBias;
    base.status = EventStatus::PENDING_CONFIRMATION;
    base.expiresAt = nowUtc + cfg_.expirySeconds;
    base.entry = PriceLevel{price, "engine_last_close", ""};
    if (bias > 0) {
        base.direction = EngineDirection::BULLISH;
        base.stopLoss = PriceLevel{price - cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{price + m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    } else {
        base.direction = EngineDirection::BEARISH;
        base.stopLoss = PriceLevel{price + cfg_.stopAtrMultiple * *a, "engine_atr",
                                   "وقف وفق تقلب ATR"};
        for (double m : cfg_.targetAtrMultiples)
            base.takeProfitLevels.push_back(
                PriceLevel{price - m * *a, "engine_atr", "هدف وفق تقلب ATR"});
    }
    base.reasonCodes = {"event_verified_recent", "market_data_fresh",
                        "bias_above_threshold", "ruleset_tested",
                        "levels_explainable"};
    base.supportingEvidence = {
        {"event_summary", ev.summaryAr, std::nullopt},
        {"gold_bias", "انحياز الحدث المتوقع للذهب", bias},
        {"event_age", "عمر الخبر بالثواني",
         static_cast<double>(nowUtc - ev.publishedAt)},
    };
    if (ev.actual.has_value())
        base.supportingEvidence.push_back(
            {"actual", "القيمة الفعلية", *ev.actual});
    if (ev.forecast.has_value())
        base.supportingEvidence.push_back(
            {"forecast", "القيمة المتوقعة", *ev.forecast});
    base.explanationAr =
        "حدث اقتصادي مؤكد وحديث، مع بيانات سوق حديثة وقواعد مختبرة ومستويات قابلة "
        "للتفسير؛ اكتملت شروط الصفقة المدفوعة بالحدث.";
    out.push_back(std::move(base));
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
