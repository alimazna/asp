// AYZR-ENG-104 — Market Microstructure Engine (implementation).

#include "ayzr/engines/MicrostructureEngine.h"

#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

std::string MicrostructureEngine::id() const {
    return std::string("microstructure-") + toString(tf_);
}

EngineHealth MicrostructureEngine::health(const MarketView& view,
                                          std::int64_t nowUtc) const {
    // With no verified microstructure feed the engine is unavailable by fact,
    // regardless of how fresh the OHLC data is.
    (void)view; (void)nowUtc;
    return EngineHealth::UNAVAILABLE;
}

std::vector<EngineResult> MicrostructureEngine::evaluate(const MarketView& view) const {
    // No microstructure input supplied => be honest.
    return evaluateWith(view, MicrostructureInput{});
}

std::vector<EngineResult> MicrostructureEngine::evaluateWith(
    const MarketView& view, const MicrostructureInput& micro) const {
    std::vector<EngineResult> out;
    EngineResult base;
    base.engineId = id();
    base.engineVersion = version();
    base.strategyFamily = family();
    base.eventType = "health";
    base.symbol = view.symbol;
    base.timeframe = tf_;
    base.dataTimestamp = view.latestOpenUtc;
    base.createdAt = view.snapshotUtc;
    base.updatedAt = view.snapshotUtc;
    base.dataSource = micro.sourceId.empty() ? view.dataSource : micro.sourceId;
    base.dataQuality = view.quality;
    base.inputSnapshotReference = id() + "@" + std::to_string(view.latestOpenUtc);
    base.correlationId = std::to_string(view.latestOpenUtc);
    base.eventId = makeEventId(id(), view.symbol, tf_, view.latestOpenUtc);

    const bool hasAnyQuote = micro.bid.has_value() || micro.ask.has_value() ||
                             micro.spreadPoints.has_value() ||
                             micro.tickImbalance.has_value() ||
                             micro.depthImbalance.has_value();
    if (!hasAnyQuote) {
        base.direction = EngineDirection::NO_TRADE;
        base.status = EventStatus::WARNING;
        base.reasonCodes = {"microstructure_unavailable"};
        base.riskFlags = {"no_bid_ask_feed", "no_order_flow_feed"};
        base.explanationAr =
            "بنية السوق غير متاحة: لا يتوفر مصدر موثوق لسعري الشراء والبيع أو "
            "تدفق الأوامر. لن يتم تخمين أي منها.";
        out.push_back(std::move(base));
        return out;
    }

    // A real feed: report what we actually have. We still never fabricate a
    // direction from microstructure alone; this is a risk/context observation.
    base.status = EventStatus::WARNING;
    base.direction = EngineDirection::NO_TRADE;

    if (micro.bid.has_value() && micro.ask.has_value()) {
        const double spread = *micro.ask - *micro.bid;
        base.supportingEvidence.push_back(
            {"bid_ask_spread", "فارق سعر الشراء والبيع", spread});
        base.reasonCodes.push_back("spread_observed");
        if (spread > 0.0 && spread * 10.0 > cfg_.wideSpreadPoints) {
            base.riskFlags.push_back("wide_spread");
            base.explanationAr = "فارق سعر واسع غير معتاد؛ ظروف تنفيذ محفوفة بالمخاطر.";
        } else {
            base.explanationAr = "ظروف التنفيذ ضمن النطاق المعتاد.";
        }
    }
    if (micro.spreadPoints.has_value()) {
        base.supportingEvidence.push_back(
            {"spread_points", "الفارق بالنقاط", static_cast<double>(*micro.spreadPoints)});
    }
    if (micro.tickImbalance.has_value()) {
        base.supportingEvidence.push_back(
            {"tick_imbalance", "اختلال الصفقات الصاعدة/الهابطة", *micro.tickImbalance});
    }
    if (micro.depthImbalance.has_value()) {
        base.supportingEvidence.push_back(
            {"depth_imbalance", "اختلال عمق السوق", *micro.depthImbalance});
    }
    if (base.explanationAr.empty()) {
        base.explanationAr = "تم رصد بيانات بنية سوق حقيقية، دون توليد اتجاه.";
    }
    out.push_back(std::move(base));
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
