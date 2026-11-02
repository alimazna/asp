// AYZR-ENG-100 — Strategy engine base + shared indicators (implementation).

#include "ayzr/engines/StrategyEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

bool barsContiguous(const std::vector<Bar>& bars, Timeframe tf) noexcept {
    if (bars.size() < 2) return !bars.empty();
    const std::int64_t step = [] (Timeframe t) -> std::int64_t {
        switch (t) {
            case Timeframe::M1:  return 60;
            case Timeframe::M5:  return 300;
            case Timeframe::M15: return 900;
            case Timeframe::M30: return 1800;
            case Timeframe::H1:  return 3600;
            case Timeframe::H4:  return 14400;
            case Timeframe::D1:  return 86400;
            case Timeframe::W1:  return 604800;
            case Timeframe::MN1: return 2592000;   // nominal; not used for gaps
        }
        return 0;
    }(tf);
    for (std::size_t i = 1; i < bars.size(); ++i) {
        std::int64_t d = bars[i].openTimeUtc - bars[i - 1].openTimeUtc;
        if (d <= 0) return false;              // out of order or duplicate
        if (step > 0 && d > step) return false; // a gap
    }
    return true;
}

std::optional<double> sma(const std::vector<Bar>& bars, std::size_t n) {
    if (n == 0 || bars.size() < n) return std::nullopt;
    double s = 0.0;
    for (std::size_t i = bars.size() - n; i < bars.size(); ++i) s += bars[i].close;
    return s / static_cast<double>(n);
}

std::optional<double> ema(const std::vector<Bar>& bars, std::size_t n) {
    if (n == 0 || bars.size() < n) return std::nullopt;
    const double k = 2.0 / (static_cast<double>(n) + 1.0);
    double seed = 0.0;
    for (std::size_t i = 0; i < n; ++i) seed += bars[i].close;
    double e = seed / static_cast<double>(n);
    for (std::size_t i = n; i < bars.size(); ++i) {
        e = bars[i].close * k + e * (1.0 - k);
    }
    return e;
}

std::optional<double> atr(const std::vector<Bar>& bars, std::size_t n) {
    if (n == 0 || bars.size() < n + 1) return std::nullopt;
    // True range needs the previous close, so the first usable index is 1.
    std::vector<double> tr;
    tr.reserve(bars.size());
    for (std::size_t i = 1; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        const double pc = bars[i - 1].close;
        const double hi = std::max(b.high, pc);
        const double lo = std::min(b.low, pc);
        tr.push_back(hi - lo);
    }
    if (tr.size() < n) return std::nullopt;
    // Wilder smoothing: first ATR is the simple mean of the first n TRs.
    double a = 0.0;
    for (std::size_t i = 0; i < n; ++i) a += tr[i];
    a /= static_cast<double>(n);
    for (std::size_t i = n; i < tr.size(); ++i) {
        a = (a * static_cast<double>(n - 1) + tr[i]) / static_cast<double>(n);
    }
    return a;
}

std::optional<double> stdev(const std::vector<Bar>& bars, std::size_t n) {
    if (n == 0 || bars.size() < n) return std::nullopt;
    double s = 0.0;
    for (std::size_t i = bars.size() - n; i < bars.size(); ++i) s += bars[i].close;
    const double mean = s / static_cast<double>(n);
    double v = 0.0;
    for (std::size_t i = bars.size() - n; i < bars.size(); ++i) {
        const double d = bars[i].close - mean;
        v += d * d;
    }
    return std::sqrt(v / static_cast<double>(n));
}

std::optional<double> rsi(const std::vector<Bar>& bars, std::size_t n) {
    if (n == 0 || bars.size() < n + 1) return std::nullopt;
    double gain = 0.0, loss = 0.0;
    for (std::size_t i = 1; i <= n; ++i) {
        const double d = bars[i].close - bars[i - 1].close;
        if (d >= 0) gain += d; else loss -= d;
    }
    double avgGain = gain / static_cast<double>(n);
    double avgLoss = loss / static_cast<double>(n);
    for (std::size_t i = n + 1; i < bars.size(); ++i) {
        const double d = bars[i].close - bars[i - 1].close;
        const double g = d > 0 ? d : 0.0;
        const double l = d < 0 ? -d : 0.0;
        avgGain = (avgGain * static_cast<double>(n - 1) + g) / static_cast<double>(n);
        avgLoss = (avgLoss * static_cast<double>(n - 1) + l) / static_cast<double>(n);
    }
    if (avgLoss == 0.0) return 100.0;
    const double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

EngineHealth StrategyEngine::health(const MarketView& view,
                                    std::int64_t nowUtc) const {
    if (view.bars.empty()) return EngineHealth::WAITING_FOR_DATA;
    // A view older than 3 timeframes is stale output, not a live reading.
    const std::int64_t step = [this]() -> std::int64_t {
        switch (timeframe()) {
            case Timeframe::M1:  return 60;
            case Timeframe::M5:  return 300;
            case Timeframe::M15: return 900;
            case Timeframe::M30: return 1800;
            case Timeframe::H1:  return 3600;
            case Timeframe::H4:  return 14400;
            case Timeframe::D1:  return 86400;
            case Timeframe::W1:  return 604800;
            case Timeframe::MN1: return 2592000;
        }
        return 900;
    }();
    if (view.latestOpenUtc > 0 && nowUtc > 0 &&
        nowUtc - view.latestOpenUtc > step * 3) {
        return EngineHealth::STALE_OUTPUT;
    }
    if (view.quality != DataQualityState::VALID) return EngineHealth::DEGRADED;
    return EngineHealth::HEALTHY;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
