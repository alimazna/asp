// DEC-0003 - Feature engine implementation.

#include "features/FeatureEngine.h"

#include <cmath>

namespace aura {

double FeatureEngine::ema(const std::vector<double>& values, std::size_t period) {
    if (values.empty() || period == 0) return 0.0;
    const double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    double result = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) {
        result = alpha * values[i] + (1.0 - alpha) * result;
    }
    return result;
}

double FeatureEngine::rsi(const std::vector<double>& closes, std::size_t period) {
    if (closes.size() < 2 || period == 0) return 50.0;
    const std::size_t start = closes.size() > period ? closes.size() - period : 1;
    double gain = 0.0;
    double loss = 0.0;
    for (std::size_t i = start; i < closes.size(); ++i) {
        const double change = closes[i] - closes[i - 1];
        if (change >= 0.0) gain += change;
        else loss -= change;
    }
    if (loss == 0.0) return gain == 0.0 ? 50.0 : 100.0;
    const double rs = gain / loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

double FeatureEngine::atr(const std::vector<Bar>& bars, std::size_t period) {
    if (bars.size() < 2 || period == 0) return 0.0;
    const std::size_t start = bars.size() > period ? bars.size() - period : 1;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < bars.size(); ++i) {
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

double FeatureEngine::stdev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double mean = 0.0;
    for (double v : values) mean += v;
    mean /= static_cast<double>(values.size());
    double variance = 0.0;
    for (double v : values) variance += (v - mean) * (v - mean);
    variance /= static_cast<double>(values.size() - 1);
    return std::sqrt(variance);
}

FeatureSnapshot FeatureEngine::compute(const std::vector<Bar>& bars,
                                       Timeframe timeframe) const {
    FeatureSnapshot snapshot;
    snapshot.timeframe = timeframe;
    snapshot.barCount = bars.size();

    if (bars.size() < config_.minBars) {
        snapshot.quality = DataQualityState::INCOMPLETE;
        snapshot.detail = "insufficient closed bars: " + std::to_string(bars.size()) +
                          " < " + std::to_string(config_.minBars);
        return snapshot;
    }

    const Bar& last = bars.back();
    snapshot.asOfBarOpenSec = last.openTimeSec;
    snapshot.lastClose = last.close;
    snapshot.lastOpen = last.open;
    snapshot.lastHigh = last.high;
    snapshot.lastLow = last.low;

    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) closes.push_back(bar.close);

    snapshot.emaFast = ema(closes, config_.fastPeriod);
    snapshot.emaSlow = ema(closes, config_.slowPeriod);
    snapshot.trendSlope = snapshot.emaSlow != 0.0
                              ? (snapshot.emaFast - snapshot.emaSlow) / snapshot.emaSlow
                              : 0.0;

    snapshot.atr = atr(bars, config_.atrPeriod);
    snapshot.rsi = rsi(closes, config_.rsiPeriod);

    const std::size_t momentumIndex =
        closes.size() > config_.momentumPeriod
            ? closes.size() - 1 - config_.momentumPeriod
            : 0;
    snapshot.momentum = closes.back() - closes[momentumIndex];

    std::vector<double> logReturns;
    const std::size_t volStart = closes.size() > config_.volatilityPeriod
                                     ? closes.size() - config_.volatilityPeriod - 1
                                     : 0;
    for (std::size_t i = volStart + 1; i < closes.size(); ++i) {
        if (closes[i - 1] > 0.0 && closes[i] > 0.0) {
            logReturns.push_back(std::log(closes[i] / closes[i - 1]));
        }
    }
    snapshot.volatility = stdev(logReturns);

    const double range = last.high - last.low;
    snapshot.bodyRatio = range > 0.0 ? std::fabs(last.close - last.open) / range : 0.0;
    snapshot.rangeRatio = snapshot.atr > 0.0 ? range / snapshot.atr : 0.0;

    if (snapshot.atr <= 0.0 || !std::isfinite(snapshot.trendSlope)) {
        snapshot.quality = DataQualityState::INVALID;
        snapshot.detail = "degenerate feature values (zero ATR or non-finite slope)";
        return snapshot;
    }

    snapshot.valid = true;
    snapshot.quality = DataQualityState::VALID;
    return snapshot;
}

}  // namespace aura
