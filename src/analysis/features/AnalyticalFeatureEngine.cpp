// Agent-A (T01) - Analytical feature engine implementation.
//
// Formula reference and ranges: src/analysis/features/FEATURES.md.
// All features are causal: the engine never reads a bar with a close time
// later than the decision bar. `asOfBarOpenSec` is the last closed bar used.

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {

namespace {

double clampSigned(double x) noexcept {
    if (!std::isfinite(x)) return 0.0;
    if (x < -1.0) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

double clampUnit(double x) noexcept {
    if (!std::isfinite(x)) return 0.0;
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

int signOf(double x) noexcept {
    if (x > 0.0) return 1;
    if (x < 0.0) return -1;
    return 0;
}

// Sample standard deviation of the log returns of a close series. Non-positive
// closes are skipped so the result is never NaN.
double logReturnStdev(const std::vector<Bar>& bars) {
    std::vector<double> rets;
    rets.reserve(bars.size());
    for (std::size_t i = 1; i < bars.size(); ++i) {
        const double a = bars[i - 1].close;
        const double b = bars[i].close;
        if (a > 0.0 && b > 0.0) rets.push_back(std::log(b / a));
    }
    if (rets.size() < 2) return 0.0;
    double mean = 0.0;
    for (double v : rets) mean += v;
    mean /= static_cast<double>(rets.size());
    double var = 0.0;
    for (double v : rets) var += (v - mean) * (v - mean);
    var /= static_cast<double>(rets.size() - 1);
    return std::sqrt(var);
}

// Mean true range over a bar series (Wilder's TR definition).
double meanTrueRange(const std::vector<Bar>& bars) {
    if (bars.size() < 2) return 0.0;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 1; i < bars.size(); ++i) {
        const double prevClose = bars[i - 1].close;
        const double tr = std::max(
            bars[i].high - bars[i].low,
            std::max(std::fabs(bars[i].high - prevClose),
                     std::fabs(bars[i].low - prevClose)));
        sum += tr;
        ++count;
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

// Higher-high / higher-low transition balance over a bar series, in [-1,1].
// Positive => net higher highs and higher lows (up structure).
double structureTrendOf(const std::vector<Bar>& bars) {
    if (bars.size() < 2) return 0.0;
    int up = 0;
    int down = 0;
    for (std::size_t i = 1; i < bars.size(); ++i) {
        if (bars[i].high > bars[i - 1].high) ++up;
        else if (bars[i].high < bars[i - 1].high) ++down;
        if (bars[i].low > bars[i - 1].low) ++up;
        else if (bars[i].low < bars[i - 1].low) ++down;
    }
    const double denom = 2.0 * static_cast<double>(bars.size() - 1);
    return clampSigned(static_cast<double>(up - down) / denom);
}

std::vector<Bar> lastN(const std::vector<Bar>& bars, std::size_t n) {
    if (bars.size() <= n) return bars;
    return std::vector<Bar>(bars.end() - static_cast<std::ptrdiff_t>(n),
                            bars.end());
}

}  // namespace

TimeframeFeatures AnalyticalFeatureEngine::computeTimeframe(
    const std::vector<Bar>& bars, Timeframe timeframe,
    std::int64_t asOfBarOpenSec) const {
    TimeframeFeatures f;
    f.timeframe = timeframe;

    // Causality: drop any bar whose open time is after the decision bar. Bars
    // arrive in ascending open time, so a cutoff keeps the causal prefix only.
    std::vector<Bar> causal = bars;
    if (asOfBarOpenSec >= 0) {
        causal.erase(
            std::remove_if(causal.begin(), causal.end(),
                           [asOfBarOpenSec](const Bar& b) {
                               return b.openTimeSec > asOfBarOpenSec;
                           }),
            causal.end());
    }
    f.barsAvailable = causal.size();

    if (causal.empty()) {
        f.quality = DataQualityState::INCOMPLETE;
        f.detail = "no closed bars at or before the decision bar";
        return f;
    }

    const Bar& lastBar = causal.back();
    f.asOfBarOpenSec = lastBar.openTimeSec;

    const std::vector<Bar> window = lastN(causal, kTriggerWindow);
    const std::size_t n = window.size();
    f.windowUsed = n;

    if (n < config_.minTriggerBars) {
        f.quality = DataQualityState::INCOMPLETE;
        f.detail = "insufficient trigger bars: " + std::to_string(n) + " < " +
                   std::to_string(config_.minTriggerBars);
        return f;
    }

    const Bar& first = window.front();
    double wHigh = window.front().high;
    double wLow = window.front().low;
    for (const Bar& b : window) {
        wHigh = std::max(wHigh, b.high);
        wLow = std::min(wLow, b.low);
    }
    const double wRange = wHigh - wLow;

    // --- Candle behaviour (last closed candle) -----------------------------
    const double r = lastBar.high - lastBar.low;
    if (r > 0.0) {
        f.bodyRatio = clampUnit(std::fabs(lastBar.close - lastBar.open) / r);
        f.upperWickRatio = clampUnit(
            (lastBar.high - std::max(lastBar.open, lastBar.close)) / r);
        f.lowerWickRatio = clampUnit(
            (std::min(lastBar.open, lastBar.close) - lastBar.low) / r);
    }
    f.candleDirection = static_cast<double>(signOf(lastBar.close - lastBar.open));

    // --- Structure over the trigger window ---------------------------------
    int hh = 0;
    int ll = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (window[i].high > window[i - 1].high) ++hh;
        if (window[i].low < window[i - 1].low) ++ll;
    }
    const double trans = static_cast<double>(n - 1);
    f.higherHighShare = clampUnit(static_cast<double>(hh) / trans);
    f.lowerLowShare = clampUnit(static_cast<double>(ll) / trans);
    f.structureTrend = structureTrendOf(window);

    // --- Range position ----------------------------------------------------
    f.rangePosition = wRange > 0.0 ? clampUnit((lastBar.close - wLow) / wRange)
                                   : 0.5;

    // --- Swing asymmetry: which extreme happened most recently -------------
    std::size_t lastHighIdx = 0;
    std::size_t lastLowIdx = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (window[i].high >= window[lastHighIdx].high) lastHighIdx = i;
        if (window[i].low <= window[lastLowIdx].low) lastLowIdx = i;
    }
    f.swingAsymmetry = clampSigned(
        static_cast<double>(static_cast<long>(lastHighIdx) -
                            static_cast<long>(lastLowIdx)) /
        trans);

    // --- Run balance: consecutive same-direction candles -------------------
    int run = 0;
    for (std::size_t i = n; i-- > 1;) {
        const int d = signOf(window[i].close - window[i].open);
        if (d == 0) break;
        if (run == 0) {
            run = d;
        } else if (signOf(static_cast<double>(run)) == d) {
            run += d;
        } else {
            break;
        }
    }
    f.runBalance = clampSigned(static_cast<double>(run) / trans);

    // --- Momentum ----------------------------------------------------------
    const double net = lastBar.close - first.close;
    double path = 0.0;
    for (std::size_t i = 1; i < n; ++i) {
        path += std::fabs(window[i].close - window[i - 1].close);
    }
    const double typical = path / trans;
    f.momentumNorm = typical > 0.0 ? clampSigned(net / (trans * typical)) : 0.0;
    f.momentumPersistence = path > 0.0 ? clampUnit(std::fabs(net) / path) : 0.0;

    const std::size_t mid = n / 2;
    const double earlierSpan = mid > 0 ? static_cast<double>(mid) : 1.0;
    const double recentSpan = static_cast<double>(n - 1 - mid) > 0.0
                                  ? static_cast<double>(n - 1 - mid)
                                  : 1.0;
    const double earlierSpeed = std::fabs(window[mid].close - first.close) / earlierSpan;
    const double recentSpeed = std::fabs(lastBar.close - window[mid].close) / recentSpan;
    const double speedSum = earlierSpeed + recentSpeed;
    f.momentumAcceleration =
        speedSum > 0.0 ? clampSigned((recentSpeed - earlierSpeed) / speedSum) : 0.0;

    // --- Volatility / regime ----------------------------------------------
    const std::vector<Bar> context = lastN(causal, config_.contextBars);
    const double shortVol = logReturnStdev(window);
    const double longVol = logReturnStdev(context);
    const double volSum = shortVol + longVol;
    f.volatilityRatio = volSum > 0.0 ? clampUnit(shortVol / volSum) : 0.5;

    const double atrWindow = meanTrueRange(window);
    const double atrContext = meanTrueRange(context);
    const double atrSum = atrWindow + atrContext;
    f.atrRatio = atrSum > 0.0 ? clampUnit(atrWindow / atrSum) : 0.5;

    // --- Local pattern -----------------------------------------------------
    f.netChangeRatio = wRange > 0.0 ? clampSigned(net / wRange) : 0.0;
    f.patternScore = clampSigned(0.5 * f.structureTrend + 0.3 * f.netChangeRatio +
                                 0.2 * f.runBalance);

    // --- 3-month context window -------------------------------------------
    if (context.size() >= config_.minContextBars) {
        f.contextTrend = structureTrendOf(context);
        f.contextVolatility = clampUnit(longVol);
        double cHigh = context.front().high;
        double cLow = context.front().low;
        for (const Bar& b : context) {
            cHigh = std::max(cHigh, b.high);
            cLow = std::min(cLow, b.low);
        }
        const double cRange = cHigh - cLow;
        f.contextRangePosition =
            cRange > 0.0 ? clampUnit((lastBar.close - cLow) / cRange) : 0.5;
    }

    if (!(wRange > 0.0)) {
        f.quality = DataQualityState::INVALID;
        f.detail = "degenerate window: zero high-low range";
        return f;
    }

    f.valid = true;
    f.quality = DataQualityState::VALID;
    return f;
}

CrossTimeframeFeatures AnalyticalFeatureEngine::computeCross(
    const std::map<Timeframe, std::vector<Bar>>& byTimeframe) const {
    CrossTimeframeFeatures c;

    const auto itM15 = byTimeframe.find(Timeframe::M15);
    const auto itH4 = byTimeframe.find(Timeframe::H4);
    const auto itD1 = byTimeframe.find(Timeframe::D1);

    TimeframeFeatures m15;
    TimeframeFeatures h4;
    TimeframeFeatures d1;
    if (itM15 != byTimeframe.end()) m15 = computeTimeframe(itM15->second, Timeframe::M15);
    if (itH4 != byTimeframe.end()) h4 = computeTimeframe(itH4->second, Timeframe::H4);
    if (itD1 != byTimeframe.end()) d1 = computeTimeframe(itD1->second, Timeframe::D1);

    c.m15Available = m15.valid;
    c.h4Available = h4.valid;
    c.d1Available = d1.valid;
    c.asOfBarOpenSec = m15.asOfBarOpenSec;

    const int m15Sign = signOf(m15.structureTrend);
    const int h4Sign = signOf(h4.structureTrend);
    const int d1Sign = signOf(d1.structureTrend);

    c.h4M15Agreement = (m15Sign != 0 && m15Sign == h4Sign)
                           ? static_cast<double>(h4Sign)
                           : 0.0;
    c.h4D1Agreement = (d1Sign != 0 && d1Sign == h4Sign)
                          ? static_cast<double>(h4Sign)
                          : 0.0;

    // Conflict share over the available subset of {M15, H4, D1}.
    const int signs[3] = {m15Sign, h4Sign, d1Sign};
    const bool present[3] = {m15.valid, h4.valid, d1.valid};
    int pairs = 0;
    int conflicts = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (!present[i] || !present[j]) continue;
            ++pairs;
            if (signs[i] != signs[j]) ++conflicts;
        }
    }
    c.mtfConflictScore = pairs > 0 ? static_cast<double>(conflicts) / pairs : 0.0;

    c.h4StructuralAuthority = clampSigned(h4.structureTrend);
    c.m15TriggerState = clampSigned(0.5 * m15.structureTrend + 0.5 * m15.patternScore);

    if (!m15.valid || !h4.valid) {
        c.quality = m15.valid || h4.valid ? DataQualityState::DEGRADED
                                          : DataQualityState::INCOMPLETE;
        c.detail = !m15.valid ? "M15 (operational) stream unavailable"
                              : "H4 (structural authority) stream unavailable";
        return c;
    }

    c.valid = true;
    c.quality = d1.valid ? DataQualityState::VALID : DataQualityState::DEGRADED;
    if (!d1.valid) c.detail = "D1 unavailable: H4/D1 agreement not evaluated";
    return c;
}

AnalyticalFeatureSet AnalyticalFeatureEngine::computeAll(
    const std::map<Timeframe, std::vector<Bar>>& byTimeframe) const {
    AnalyticalFeatureSet set;
    std::size_t validStreams = 0;

    for (Timeframe tf : allTimeframes()) {
        const auto it = byTimeframe.find(tf);
        if (it == byTimeframe.end()) {
            TimeframeFeatures f;
            f.timeframe = tf;
            f.quality = DataQualityState::UNKNOWN;
            f.detail = "stream not supplied";
            set.perTimeframe.push_back(std::move(f));
            continue;
        }
        TimeframeFeatures f = computeTimeframe(it->second, tf);
        if (f.valid) ++validStreams;
        set.perTimeframe.push_back(std::move(f));
    }

    set.cross = computeCross(byTimeframe);

    const bool corePresent = set.cross.m15Available && set.cross.h4Available;
    set.valid = corePresent && validStreams == kTimeframeCount;
    if (!corePresent) {
        set.quality = DataQualityState::INCOMPLETE;
        set.detail = "core streams (M15 trigger / H4 authority) incomplete";
    } else if (set.valid) {
        set.quality = DataQualityState::VALID;
    } else {
        set.quality = DataQualityState::DEGRADED;
        set.detail = std::to_string(validStreams) + "/" +
                     std::to_string(kTimeframeCount) + " timeframe streams valid";
    }
    return set;
}

}  // namespace aura
