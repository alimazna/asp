// AYZR-ECO-RT — Ecosystem runtime (implementation).

#include "ayzr/ecosystem/EcosystemRuntime.h"

#include "ayzr/engines/BreakoutEngine.h"
#include "ayzr/engines/EventDrivenEngine.h"
#include "ayzr/engines/MeanReversionEngine.h"
#include "ayzr/engines/MicrostructureEngine.h"
#include "ayzr/engines/TrendFollowingEngine.h"
#include "persistence/CandleStore.h"

namespace aura {
namespace ayzr {
namespace eco {

namespace {
// The operational timeframe the ecosystem runs on, plus the higher-context one.
// H4 provides context; M15 is operational (verified against the ASTRA design).
constexpr Timeframe kOperational = Timeframe::M15;
constexpr Timeframe kContext = Timeframe::H4;
}  // namespace

EcosystemRuntime::EcosystemRuntime(DecisionLedger* ledger, OrchestratorConfig cfg,
                                   const std::string& symbol)
    : ledger_(ledger), symbol_(symbol), orchestrator_(ledger, cfg) {
    // One instance per (strategy, operational timeframe). Microstructure and
    // EventDriven are registered too: they report honestly when their inputs
    // are absent, which is itself a required behaviour.
    engines_.push_back(std::make_unique<TrendFollowingEngine>(kOperational));
    engines_.push_back(std::make_unique<MeanReversionEngine>(kOperational));
    engines_.push_back(std::make_unique<BreakoutEngine>(kOperational));
    engines_.push_back(std::make_unique<MicrostructureEngine>(kOperational));
    engines_.push_back(std::make_unique<EventDrivenEngine>(kOperational));
    // A longer-horizon trend instance gives the orchestrator multi-timeframe
    // context without any engine mutating another.
    engines_.push_back(std::make_unique<TrendFollowingEngine>(kContext));
}

MarketView EcosystemRuntime::buildView(const CandleStore& store, Timeframe tf,
                                       std::int64_t nowUtc, int limit) const {
    MarketView v;
    v.symbol = symbol_;
    v.timeframe = tf;
    v.snapshotUtc = nowUtc;
    v.dataSource = "store";
    // The store already quarantines a contaminated series, so whatever it
    // returns is a clean series; if it returns nothing we report empty and the
    // engines stay silent.
    std::vector<StoredCandle> rows = store.recent(symbol_, toString(tf), limit);
    if (rows.empty()) {
        v.quality = DataQualityState::MISSING;
        return v;
    }
    // The store returns ascending by open time; convert to Bars and check
    // contiguity. A gapped series is still usable but flagged INCOMPLETE.
    v.bars.reserve(rows.size());
    for (const StoredCandle& c : rows) {
        Bar b;
        b.openTimeUtc = c.openTimeUtc;
        b.open = c.open;
        b.high = c.high;
        b.low = c.low;
        b.close = c.close;
        b.tickVolume = c.tickVolume;
        b.spread = c.spread;
        b.hasSpread = c.spread != 0;
        v.bars.push_back(b);
    }
    v.latestOpenUtc = v.bars.back().openTimeUtc;
    // A gap that falls entirely inside a scheduled market closure (the daily
    // rollover or the weekend) is an expected absence in a real gold feed, not
    // corrupted data. Only a gap during open hours — or an out-of-order /
    // duplicate bar — makes the series non-decision-grade. The skipped closures
    // are recorded so the caller can surface them instead of hiding them.
    std::vector<GapInfo> closures;
    v.quality = barsContiguousAllowingClosures(v.bars, tf, &closures)
                    ? DataQualityState::VALID
                    : DataQualityState::INCOMPLETE;
    v.closureGaps = std::move(closures);
    return v;
}

CycleResult EcosystemRuntime::runCycle(const MarketView& view,
                                       std::int64_t nowUtc,
                                       const std::string& correlationId) {
    std::vector<EngineResult> all;
    for (const auto& eng : engines_) {
        // Only engines whose timeframe matches the view contribute; an engine
        // never sees a view for another timeframe (no silent cross-timeframe).
        if (eng->timeframe() != view.timeframe) continue;
        try {
            std::vector<EngineResult> rs = eng->evaluate(view);
            for (auto& r : rs) all.push_back(std::move(r));
        } catch (const std::exception& e) {
            // An engine failure must not take down the cycle.
            all.push_back(EngineResult{});
            EngineResult& f = all.back();
            f.engineId = eng->id();
            f.engineVersion = eng->version();
            f.strategyFamily = eng->family();
            f.eventType = "engine_failure";
            f.symbol = view.symbol;
            f.timeframe = view.timeframe;
            f.dataTimestamp = view.latestOpenUtc;
            f.createdAt = nowUtc;
            f.updatedAt = nowUtc;
            f.direction = EngineDirection::NO_TRADE;
            f.status = EventStatus::WARNING;
            f.reasonCodes = {"engine_exception"};
            f.explanationAr = "تعذّر تشغيل المحرك؛ تم عزل الخطأ.";
            (void)e;
        }
    }
    return orchestrator_.runCycle(all, nowUtc, correlationId);
}

std::vector<EngineDescriptor> EcosystemRuntime::describeEngines(
    const MarketView& view, std::int64_t nowUtc) const {
    std::vector<EngineDescriptor> out;
    for (const auto& eng : engines_) {
        EngineDescriptor d;
        d.engineId = eng->id();
        d.version = eng->version();
        d.family = eng->family();
        d.timeframe = eng->timeframe();
        // Health is reported against the view only when the timeframe matches;
        // otherwise the engine is "waiting for data" for that timeframe.
        if (eng->timeframe() == view.timeframe) {
            d.health = eng->health(view, nowUtc);
        } else {
            d.health = EngineHealth::WAITING_FOR_DATA;
        }
        switch (d.family) {
            case StrategyFamily::MICROSTRUCTURE:
                d.noteAr = "لا يتوفر مصدر موثوق لبيانات بنية السوق بعد.";
                break;
            case StrategyFamily::EVENT_DRIVEN:
                d.noteAr = "يتطلب حدثاً اقتصادياً مؤكداً وحديثاً.";
                break;
            default:
                d.noteAr = "";
        }
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
