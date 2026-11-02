// AYZR-ENG-001 — Multi-horizon engine implementation.
//
// Every decision here is delegated to aura::SignalEngine::generate(). This
// file adds ONLY:
//   1. per-horizon config profiles (documented tuning, not renamed copies),
//   2. the busy-slot gate (one active trade per horizon),
//   3. the higher-timeframe alignment gate,
//   4. the minimum-strength gate.
//
// ASTRA's own engine instance and its default config are never touched.

#include "ayzr/HorizonEngine.h"

#include <cmath>

namespace aura {
namespace ayzr {

namespace {

/// Map a regime to a coarse trend sign: +1 up, -1 down, 0 unclear.
int trendSign(const RegimeState& r) {
    // RegimeType is the authoritative classifier; trendStrength only decides
    // whether the trend is meaningful enough to align against.
    const bool strong = r.trendStrength >= 0.25;
    switch (r.regime) {
        case RegimeType::TRENDING_UP:
            return strong ? +1 : 0;
        case RegimeType::TRENDING_DOWN:
            return strong ? -1 : 0;
        default:
            return 0;
    }
}

}  // namespace

HorizonProfile makeShortProfile() {
    HorizonProfile p;
    p.horizon = Horizon::SHORT;
    p.entryTimeframe = Timeframe::M5;
    p.contextTimeframes = {Timeframe::M15};
    // Tighter band: a short-horizon entry needs a real directional push.
    p.signal.rsiLongThreshold = 55.0;
    p.signal.rsiShortThreshold = 45.0;
    p.signal.atrStopMultiplier = 1.2;    // tight stop
    p.signal.atrTargetMultiplier = 2.0;  // fast target
    // Ranges are tradeable on a short horizon.
    p.eligibility.allowRanging = true;
    p.eligibility.allowVolatile = false;
    p.eligibility.requireValidStructure = true;
    p.requireContextAlignment = false;  // M5 leads; context is informational
    // rawStrength is votes/3 -> 0.667 (two votes) or 1.0 (three votes).
    // SHORT accepts a two-vote setup.
    p.minStrength = 0.60;
    return p;
}

HorizonProfile makeMediumProfile() {
    HorizonProfile p;
    p.horizon = Horizon::MEDIUM;
    p.entryTimeframe = Timeframe::M15;
    p.contextTimeframes = {Timeframe::H1, Timeframe::H4};
    p.signal.rsiLongThreshold = 53.0;
    p.signal.rsiShortThreshold = 47.0;
    p.signal.atrStopMultiplier = 1.6;    // balanced
    p.signal.atrTargetMultiplier = 3.0;  // wider than SHORT
    p.eligibility.allowRanging = true;
    p.eligibility.allowVolatile = false;
    p.eligibility.requireValidStructure = true;
    p.requireContextAlignment = true;  // H1/H4 must agree
    // Two votes plus an agreeing H1/H4 context is the MEDIUM bar.
    p.minStrength = 0.60;
    return p;
}

HorizonProfile makeLongProfile() {
    HorizonProfile p;
    p.horizon = Horizon::LONG;
    p.entryTimeframe = Timeframe::H4;
    p.contextTimeframes = {Timeframe::D1};
    // A long-horizon entry does not need an extreme; the trend carries it.
    p.signal.rsiLongThreshold = 51.0;
    p.signal.rsiShortThreshold = 49.0;
    p.signal.atrStopMultiplier = 2.2;    // wide stop for noise tolerance
    p.signal.atrTargetMultiplier = 4.5;  // distant target
    // A long-horizon trade needs a trend, so ranging is refused outright.
    p.eligibility.allowRanging = false;
    p.eligibility.allowVolatile = false;
    p.eligibility.requireValidStructure = true;
    p.requireContextAlignment = true;  // D1 must agree
    // LONG demands unanimity: all three votes (structure + regime + RSI).
    p.minStrength = 0.99;
    return p;
}

HorizonProfile profileFor(Horizon h) {
    switch (h) {
        case Horizon::SHORT:  return makeShortProfile();
        case Horizon::MEDIUM: return makeMediumProfile();
        case Horizon::LONG:   return makeLongProfile();
    }
    return makeShortProfile();
}

HorizonEngine::HorizonEngine(HorizonProfile profile)
    : profile_(std::move(profile)) {}

HorizonSignal HorizonEngine::evaluate(
    const FeatureSnapshot& entryFeatures,
    const StructureSnapshot& entryStructure,
    const RegimeState& entryRegime,
    const EligibilityState& entryEligibility,
    const std::vector<RegimeState>& contextRegimes) const {
    HorizonSignal out;
    out.horizon = profile_.horizon;
    out.timeframe = profile_.entryTimeframe;
    out.asOfBarOpenSec = entryFeatures.asOfBarOpenSec;

    // Gate 1 — the busy slot. A horizon with an active trade stays silent.
    if (busy_) {
        out.rationale = "المحرك مشغول بصفقة نشطة";
        return out;
    }

    // Gate 2 — delegate the actual decision to ASTRA's own engine, with this
    // horizon's thresholds. The engine is constructed on the stack so ASTRA's
    // shared instance is never mutated.
    SignalEngine engine(profile_.signal);
    const SignalCandidate cand = engine.generate(
        entryFeatures, entryStructure, entryRegime, entryEligibility);

    if (!cand.valid || cand.direction == SignalDirection::NONE) {
        out.rationale = cand.rationale.empty() ? "لا توجد إشارة صالحة"
                                               : cand.rationale;
        out.quality = cand.quality;
        return out;
    }

    // Gate 3 — minimum strength for this horizon.
    if (cand.rawStrength < profile_.minStrength) {
        out.rationale = "قوة الإشارة أقل من الحد المطلوب لهذا الأفق";
        out.quality = cand.quality;
        return out;
    }

    // Gate 4 — higher-timeframe alignment. Each context regime must agree, or
    // at worst be neutral. A single opposing context frame vetoes the signal.
    if (profile_.requireContextAlignment && !contextRegimes.empty()) {
        const int want = (cand.direction == SignalDirection::LONG) ? +1 : -1;
        for (const auto& ctx : contextRegimes) {
            const int sign = trendSign(ctx);
            if (sign != 0 && sign != want) {
                out.rationale = "سياق الأطر الأعلى يعارض اتجاه الإشارة";
                out.quality = cand.quality;
                return out;
            }
        }
    }

    out.direction = cand.direction;
    out.entry = cand.referencePrice;
    out.stop = cand.suggestedStop;
    out.target = cand.suggestedTarget;
    out.strength = cand.rawStrength;
    out.rationale = cand.rationale;
    out.quality = cand.quality;
    out.valid = true;
    return out;
}

HorizonRegistry::HorizonRegistry()
    : short_(makeShortProfile()),
      medium_(makeMediumProfile()),
      long_(makeLongProfile()) {}

HorizonEngine& HorizonRegistry::engine(Horizon h) {
    switch (h) {
        case Horizon::SHORT:  return short_;
        case Horizon::MEDIUM: return medium_;
        case Horizon::LONG:   return long_;
    }
    return short_;
}

const HorizonEngine& HorizonRegistry::engine(Horizon h) const {
    switch (h) {
        case Horizon::SHORT:  return short_;
        case Horizon::MEDIUM: return medium_;
        case Horizon::LONG:   return long_;
    }
    return short_;
}

std::vector<HorizonSignal> HorizonRegistry::evaluateAll(
    const FeatureSnapshot& entryFeatures,
    const StructureSnapshot& entryStructure,
    const RegimeState& entryRegime,
    const EligibilityState& entryEligibility,
    const std::vector<RegimeState>& contextRegimes) {
    std::vector<HorizonSignal> published;
    // Evaluate each horizon in isolation. A busy or silent horizon simply
    // contributes nothing; the others are unaffected.
    for (Horizon h : {Horizon::SHORT, Horizon::MEDIUM, Horizon::LONG}) {
        HorizonEngine& e = engine(h);
        if (e.isBusy()) continue;
        const HorizonSignal sig = e.evaluate(entryFeatures, entryStructure,
                                             entryRegime, entryEligibility,
                                             contextRegimes);
        if (sig.valid) {
            e.notePublished();
            published.push_back(sig);
        }
    }
    return published;
}

}  // namespace ayzr
}  // namespace aura
