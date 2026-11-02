// AYZR-API-001 — JSON serialization implementation.

#include "ayzr/AyzrJson.h"

#include <sstream>

namespace aura {
namespace ayzr {

namespace {

std::string num(double v) {
    std::ostringstream o;
    o.precision(10);
    o << v;
    return o.str();
}

std::string i64(std::int64_t v) { return std::to_string(v); }

const char* timeframeName(Timeframe tf) {
    switch (tf) {
        case Timeframe::M1:  return "M1";
        case Timeframe::M5:  return "M5";
        case Timeframe::M15: return "M15";
        case Timeframe::M30: return "M30";
        case Timeframe::H1:  return "H1";
        case Timeframe::H4:  return "H4";
        case Timeframe::D1:  return "D1";
        case Timeframe::W1:  return "W1";
        case Timeframe::MN1: return "MN1";
    }
    return "M15";
}

}  // namespace

std::string tradeRecordJson(const TradeRecord& t, bool includeEvents) {
    const TradeOrigin& o = t.origin;
    std::ostringstream j;
    j << "{";
    j << "\"id\":" << jsonString(o.tradeId) << ",";
    j << "\"source\":" << jsonString(toString(o.source)) << ",";
    j << "\"source_label_ar\":" << jsonString(sourceLabelAr(o.source)) << ",";
    j << "\"symbol\":" << jsonString(o.symbol) << ",";
    j << "\"direction\":" << jsonString(o.direction == TradeDirection::BUY
                                             ? "BUY"
                                             : "SELL")
      << ",";
    j << "\"direction_label_ar\":"
      << jsonString(directionLabelAr(o.direction)) << ",";
    j << "\"entry\":" << num(o.entry) << ",";
    j << "\"stop\":" << num(o.stop) << ",";
    j << "\"status\":" << jsonString(std::to_string(
                                 static_cast<int>(t.status)))
      << ",";
    j << "\"status_label_ar\":" << jsonString(statusLabelAr(t.status)) << ",";
    j << "\"outcome\":" << jsonString(std::to_string(
                                 static_cast<int>(t.outcome)))
      << ",";
    j << "\"outcome_label_ar\":" << jsonString(outcomeLabelAr(t.outcome))
      << ",";
    j << "\"is_open\":" << (t.isOpen() ? "true" : "false") << ",";
    j << "\"published_at\":" << i64(o.publishedAtSec) << ",";
    j << "\"last_update\":" << i64(t.lastUpdateSec) << ",";
    j << "\"last_price\":" << num(t.lastKnownPrice) << ",";
    j << "\"last_price_at\":" << i64(t.lastKnownPriceAtSec) << ",";
    j << "\"entry_timeframe\":" << jsonString(o.entryTimeframe) << ",";

    // Context timeframes array.
    j << "\"context_timeframes\":[";
    for (std::size_t i = 0; i < o.contextTimeframes.size(); ++i) {
        if (i) j << ",";
        j << jsonString(o.contextTimeframes[i]);
    }
    j << "],";

    // Targets with reached flags.
    j << "\"targets\":[";
    for (std::size_t i = 0; i < o.targets.size(); ++i) {
        if (i) j << ",";
        const auto& tg = o.targets[i];
        j << "{";
        j << "\"index\":" << i << ",";
        j << "\"price\":" << num(tg.price) << ",";
        j << "\"reached\":" << (tg.reached ? "true" : "false") << ",";
        j << "\"reached_at\":" << i64(tg.reachedAtSec);
        j << "}";
    }
    j << "],";

    j << "\"rationale\":" << jsonString(o.rationale) << ",";
    j << "\"engine_version\":" << jsonString(o.engineVersion) << ",";
    j << "\"origin_strength\":" << num(o.originStrength) << ",";
    j << "\"close_reason_ar\":" << jsonString(t.closeReasonAr);

    if (includeEvents) {
        j << ",\"events\":[";
        for (std::size_t i = 0; i < t.events.size(); ++i) {
            if (i) j << ",";
            const auto& e = t.events[i];
            j << "{";
            j << "\"at\":" << i64(e.atSec) << ",";
            j << "\"status_label_ar\":" << jsonString(statusLabelAr(e.status))
              << ",";
            j << "\"detail_ar\":" << jsonString(e.detailAr);
            j << "}";
        }
        j << "]";
    }

    j << "}";
    return j.str();
}

std::string enginesJson(const HorizonRegistry& registry) {
    std::ostringstream j;
    j << "{\"engines\":[";
    const Horizon hs[3] = {Horizon::SHORT, Horizon::MEDIUM, Horizon::LONG};
    for (std::size_t i = 0; i < 3; ++i) {
        if (i) j << ",";
        const HorizonEngine& e = registry.engine(hs[i]);
        const HorizonProfile& p = e.profile();
        j << "{";
        j << "\"id\":" << jsonString(toString(p.horizon)) << ",";
        j << "\"label_ar\":" << jsonString(horizonLabelAr(p.horizon)) << ",";
        j << "\"entry_timeframe\":" << jsonString(timeframeName(p.entryTimeframe))
          << ",";
        j << "\"context_timeframes\":[";
        for (std::size_t k = 0; k < p.contextTimeframes.size(); ++k) {
            if (k) j << ",";
            j << jsonString(timeframeName(p.contextTimeframes[k]));
        }
        j << "],";
        j << "\"busy\":" << (e.isBusy() ? "true" : "false") << ",";
        j << "\"published_count\":" << e.publishedCount() << ",";
        j << "\"rsi_long\":" << num(p.signal.rsiLongThreshold) << ",";
        j << "\"rsi_short\":" << num(p.signal.rsiShortThreshold) << ",";
        j << "\"stop_atr\":" << num(p.signal.atrStopMultiplier) << ",";
        j << "\"target_atr\":" << num(p.signal.atrTargetMultiplier) << ",";
        j << "\"min_strength\":" << num(p.minStrength) << ",";
        j << "\"require_context_alignment\":"
          << (p.requireContextAlignment ? "true" : "false") << ",";
        j << "\"allow_ranging\":"
          << (p.eligibility.allowRanging ? "true" : "false");
        j << "}";
    }
    j << "]}";
    return j.str();
}

std::string tradesJson(const TradeLifecycleManager& mgr,
                       const std::string& stateFilter) {
    std::vector<TradeRecord> rows;
    if (stateFilter == "open") {
        rows = mgr.openTrades();
    } else if (stateFilter == "closed") {
        for (const auto& t : mgr.all()) {
            if (t.isClosed()) rows.push_back(t);
        }
    } else {
        rows = mgr.all();
    }

    std::ostringstream j;
    j << "{\"count\":" << rows.size() << ",\"trades\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i) j << ",";
        j << tradeRecordJson(rows[i], false);
    }
    j << "]}";
    return j.str();
}

std::string tradeDetailJson(const TradeLifecycleManager& mgr,
                            const std::string& tradeId) {
    const TradeRecord* t = mgr.find(tradeId);
    if (t == nullptr) {
        return "{\"error\":\"not_found\",\"message\":\"لا توجد صفقة بهذا "
               "المعرّف\"}";
    }
    return tradeRecordJson(*t, true);
}

}  // namespace ayzr
}  // namespace aura
