// AYZR Quant Lab — ASTRA fidelity oracle (research only).
//
// Runs the REAL C++ ASTRA engines (FeatureEngine, StructureEngine, RegimeEngine,
// EligibilityEngine, SignalEngine) over a bar window read from stdin and prints
// every computed field as JSON. The Python port in ayzr_lab/astra_port.py is
// diffed against this output, so the lab's research math is provably the same
// math ASTRA CORE uses. ASTRA source is linked, never modified.
//
// stdin : first line "N timeframe" then N lines "openTimeSec open high low close volume"
// stdout: one JSON object per engine stage.

#include "features/FeatureEngine.h"
#include "structure/StructureEngine.h"
#include "regime/RegimeEngine.h"
#include "eligibility/EligibilityEngine.h"
#include "signals/SignalEngine.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace aura;

static const char* tfName(Timeframe t) {
    switch (t) {
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

static Timeframe parseTf(const std::string& s) {
    if (s == "M1") return Timeframe::M1;
    if (s == "M5") return Timeframe::M5;
    if (s == "M15") return Timeframe::M15;
    if (s == "M30") return Timeframe::M30;
    if (s == "H1") return Timeframe::H1;
    if (s == "H4") return Timeframe::H4;
    if (s == "D1") return Timeframe::D1;
    if (s == "W1") return Timeframe::W1;
    if (s == "MN1") return Timeframe::MN1;
    return Timeframe::M15;
}

int main() {
    std::string header;
    if (!std::getline(std::cin, header)) return 2;
    std::istringstream hs(header);
    std::size_t n = 0;
    std::string tfs;
    hs >> n >> tfs;
    const Timeframe tf = parseTf(tfs);

    std::vector<Bar> bars;
    bars.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) { --i; continue; }
        std::istringstream ls(line);
        Bar b;
        b.timeframe = tf;
        double vol = 0.0;
        ls >> b.openTimeSec >> b.open >> b.high >> b.low >> b.close >> vol;
        b.tickVolume = static_cast<std::int64_t>(vol);
        bars.push_back(b);
    }

    FeatureEngine fe;
    StructureEngine se;
    RegimeEngine re;
    EligibilityEngine ee;
    SignalEngine sge;

    const FeatureSnapshot f = fe.compute(bars, tf);
    const StructureSnapshot s = se.compute(bars, f);
    const RegimeState r = re.compute(f, s);
    const EligibilityState e = ee.evaluate(f, r, s);
    const SignalCandidate c = sge.generate(f, s, r, e);

    std::printf("{\n");
    std::printf("  \"timeframe\": \"%s\",\n", tfName(tf));
    std::printf("  \"features\": {\"valid\": %s, \"barCount\": %zu, \"asOf\": %lld, "
                "\"lastClose\": %.10g, \"emaFast\": %.10g, \"emaSlow\": %.10g, "
                "\"trendSlope\": %.10g, \"atr\": %.10g, \"volatility\": %.10g, "
                "\"rsi\": %.10g, \"momentum\": %.10g, \"bodyRatio\": %.10g, "
                "\"rangeRatio\": %.10g},\n",
                f.valid ? "true" : "false", f.barCount,
                static_cast<long long>(f.asOfBarOpenSec), f.lastClose, f.emaFast,
                f.emaSlow, f.trendSlope, f.atr, f.volatility, f.rsi, f.momentum,
                f.bodyRatio, f.rangeRatio);
    std::printf("  \"structure\": {\"valid\": %s, \"bias\": \"%s\", \"score\": %.10g, "
                "\"swings\": %zu, \"lastSwingHigh\": %.10g, \"lastSwingLow\": %.10g},\n",
                s.valid ? "true" : "false", toString(s.bias), s.structureScore,
                s.swings.size(), s.lastSwingHigh, s.lastSwingLow);
    std::printf("  \"regime\": {\"valid\": %s, \"regime\": \"%s\", "
                "\"trendStrength\": %.10g, \"volatilityLevel\": %.10g},\n",
                r.valid ? "true" : "false", toString(r.regime), r.trendStrength,
                r.volatilityLevel);
    std::printf("  \"eligibility\": {\"decision\": \"%s\"},\n",
                toString(e.decision));
    std::printf("  \"signal\": {\"valid\": %s, \"direction\": \"%s\", "
                "\"rawStrength\": %.10g, \"referencePrice\": %.10g, "
                "\"suggestedStop\": %.10g, \"suggestedTarget\": %.10g}\n",
                c.valid ? "true" : "false", toString(c.direction), c.rawStrength,
                c.referencePrice, c.suggestedStop, c.suggestedTarget);
    std::printf("}\n");
    return 0;
}
