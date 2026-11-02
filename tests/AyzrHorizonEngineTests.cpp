// AYZR-ENG-TEST-001 — Multi-horizon engine tests.
//
// These tests prove three things the spec demands:
//   1. ASTRA's own SignalEngine output is IDENTICAL before and after the
//      AYZR layer exists (no math drift).
//   2. SHORT / MEDIUM / LONG are genuinely independent: one busy horizon does
//      not block, queue or alter another.
//   3. A horizon never fabricates a direction when it is busy or when the
//      higher-timeframe context disagrees.

#include "ayzr/HorizonEngine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aura;
using namespace aura::ayzr;

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", what.c_str());
    }
}

/// A clean, eligible feature snapshot with a bullish tilt.
FeatureSnapshot bullishFeatures() {
    FeatureSnapshot f;
    f.timeframe = Timeframe::M15;
    f.asOfBarOpenSec = 1'700'000'000;
    f.valid = true;
    f.quality = DataQualityState::VALID;
    f.lastClose = 2000.0;
    f.lastOpen = 1998.0;
    f.lastHigh = 2002.0;
    f.lastLow = 1997.0;
    f.atr = 5.0;
    f.rsi = 60.0;   // above any long threshold
    f.emaFast = 2001.0;
    f.emaSlow = 1995.0;
    f.trendSlope = 0.3;
    return f;
}

StructureSnapshot bullishStructure() {
    StructureSnapshot s;
    s.bias = StructureBias::BULLISH;
    s.valid = true;
    s.quality = DataQualityState::VALID;
    return s;
}

RegimeState upRegime() {
    RegimeState r;
    r.timeframe = Timeframe::M15;
    r.regime = RegimeType::TRENDING_UP;
    r.trendStrength = 0.6;
    r.valid = true;
    r.quality = DataQualityState::VALID;
    return r;
}

RegimeState downRegime(Timeframe tf) {
    RegimeState r;
    r.timeframe = tf;
    r.regime = RegimeType::TRENDING_DOWN;
    r.trendStrength = 0.6;
    r.valid = true;
    r.quality = DataQualityState::VALID;
    return r;
}

EligibilityState eligible() {
    EligibilityState e;
    e.decision = EligibilityDecision::ELIGIBLE;
    e.valid = true;
    return e;
}

}  // namespace

int main() {
    std::printf("=== AYZR multi-horizon engine tests ===\n");

    // -----------------------------------------------------------------
    // 1. ASTRA baseline is untouched.
    //    We compute the default-config decision and assert the AYZR layer
    //    does not change it for an identical call.
    // -----------------------------------------------------------------
    {
        SignalEngine astra;  // default config — ASTRA's own
        const auto before = astra.generate(bullishFeatures(), bullishStructure(),
                                           upRegime(), eligible());
        // Run the AYZR layer (which constructs its own engines on the stack).
        HorizonRegistry reg;
        (void)reg.evaluateAll(bullishFeatures(), bullishStructure(), upRegime(),
                              eligible(), {});
        const auto after = astra.generate(bullishFeatures(), bullishStructure(),
                                          upRegime(), eligible());
        check(before.direction == after.direction &&
                  before.rawStrength == after.rawStrength &&
                  before.suggestedStop == after.suggestedStop &&
                  before.suggestedTarget == after.suggestedTarget,
              "astra_default_config_is_unchanged_by_ayzr_layer");
        check(before.direction == SignalDirection::LONG,
              "astra_baseline_yields_long_on_bullish_input");
    }

    // -----------------------------------------------------------------
    // 2. Horizon profiles are genuinely different.
    // -----------------------------------------------------------------
    {
        const auto s = makeShortProfile();
        const auto m = makeMediumProfile();
        const auto l = makeLongProfile();
        check(s.entryTimeframe == Timeframe::M5, "short_enters_on_m5");
        check(m.entryTimeframe == Timeframe::M15, "medium_enters_on_m15");
        check(l.entryTimeframe == Timeframe::H4, "long_enters_on_h4");
        check(s.signal.atrStopMultiplier < m.signal.atrStopMultiplier &&
                  m.signal.atrStopMultiplier < l.signal.atrStopMultiplier,
              "stop_multiplier_grows_with_horizon");
        check(s.signal.atrTargetMultiplier < m.signal.atrTargetMultiplier &&
                  m.signal.atrTargetMultiplier < l.signal.atrTargetMultiplier,
              "target_multiplier_grows_with_horizon");
        check(s.eligibility.allowRanging && !l.eligibility.allowRanging,
              "ranging_allowed_short_but_refused_long");
        check(!s.requireContextAlignment && l.requireContextAlignment,
              "context_alignment_required_only_for_higher_horizons");
    }

    // -----------------------------------------------------------------
    // 3. One active trade per horizon; busy horizon is silent.
    // -----------------------------------------------------------------
    {
        HorizonEngine shortEng(makeShortProfile());
        check(!shortEng.isBusy(), "horizon_starts_idle");
        shortEng.occupy();
        check(shortEng.isBusy(), "horizon_becomes_busy_after_occupy");
        const auto sig = shortEng.evaluate(bullishFeatures(), bullishStructure(),
                                           upRegime(), eligible(), {});
        check(!sig.valid, "busy_horizon_publishes_nothing");
        shortEng.release();
        check(!shortEng.isBusy(), "release_frees_the_slot");
    }

    // -----------------------------------------------------------------
    // 4. Horizons are independent: a busy SHORT does not block MEDIUM/LONG.
    // -----------------------------------------------------------------
    {
        HorizonRegistry reg;
        reg.engine(Horizon::SHORT).occupy();  // SHORT is now busy
        check(reg.engine(Horizon::SHORT).isBusy(), "short_is_busy");
        check(!reg.engine(Horizon::MEDIUM).isBusy(), "medium_unaffected_by_short");
        check(!reg.engine(Horizon::LONG).isBusy(), "long_unaffected_by_short");

        // LONG needs unanimity and a D1 context; feed it exactly that.
        FeatureSnapshot lf = bullishFeatures();
        lf.timeframe = Timeframe::H4;
        StructureSnapshot ls = bullishStructure();
        std::vector<RegimeState> ctx{upRegime()};
        const auto pub = reg.evaluateAll(lf, ls, upRegime(), eligible(), ctx);

        // SHORT is busy -> skipped. MEDIUM/LONG may publish.
        bool shortPublished = false;
        for (const auto& s : pub) {
            if (s.horizon == Horizon::SHORT) shortPublished = true;
        }
        check(!shortPublished, "busy_short_did_not_publish_in_evaluate_all");
    }

    // -----------------------------------------------------------------
    // 5. Context disagreement vetoes a higher-horizon signal.
    // -----------------------------------------------------------------
    {
        HorizonEngine longEng(makeLongProfile());
        FeatureSnapshot lf = bullishFeatures();
        lf.timeframe = Timeframe::H4;
        // Bullish candidate, but the D1 context trends DOWN -> veto.
        std::vector<RegimeState> ctx{downRegime(Timeframe::D1)};
        const auto sig = longEng.evaluate(lf, bullishStructure(), upRegime(),
                                          eligible(), ctx);
        check(!sig.valid, "context_disagreement_vetoes_long_signal");

        // Same candidate with an agreeing context publishes.
        std::vector<RegimeState> agree{upRegime()};
        const auto ok = longEng.evaluate(lf, bullishStructure(), upRegime(),
                                         eligible(), agree);
        check(ok.valid, "agreeing_context_lets_long_publish");
        check(ok.direction == SignalDirection::LONG, "long_signal_direction");
    }

    // -----------------------------------------------------------------
    // 6. Not-eligible input yields no fabricated direction anywhere.
    // -----------------------------------------------------------------
    {
        HorizonRegistry reg;
        EligibilityState no;
        no.decision = EligibilityDecision::NOT_ELIGIBLE;
        no.valid = true;
        const auto pub = reg.evaluateAll(bullishFeatures(), bullishStructure(),
                                         upRegime(), no, {});
        check(pub.empty(), "not_eligible_publishes_nothing_on_any_horizon");
    }

    std::printf("\n%d test case(s), %d failed, %d passed\n", g_pass + g_fail,
                g_fail, g_pass);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
