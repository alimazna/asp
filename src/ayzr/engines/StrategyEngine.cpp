// AYZR-ENG-100 — Strategy engine base + shared indicators (implementation).

#include "ayzr/engines/StrategyEngine.h"

#include <algorithm>
#include <cmath>

namespace aura {
namespace ayzr {
namespace eco {

namespace {

/// Seconds in one bar of `tf`, or 0 when the timeframe is not a grid we model.
std::int64_t timeframeStepSeconds(Timeframe t) noexcept {
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
}

}  // namespace

bool barsContiguous(const std::vector<Bar>& bars, Timeframe tf) noexcept {
    if (bars.size() < 2) return !bars.empty();
    const std::int64_t step = timeframeStepSeconds(tf);
    for (std::size_t i = 1; i < bars.size(); ++i) {
        std::int64_t d = bars[i].openTimeUtc - bars[i - 1].openTimeUtc;
        if (d <= 0) return false;              // out of order or duplicate
        if (step > 0 && d > step) return false; // a gap
    }
    return true;
}

bool isScheduledClosure(std::int64_t fromUtc, std::int64_t toUtc) noexcept {
    if (toUtc <= fromUtc) return false;
    // Gold's feed stops for a daily rollover and over the weekend. Brokers place
    // the rollover at slightly different wall-clock times (the operator's MT5
    // export closes 23:00->01:00 UTC; BiQuote closes 21:00->22:00 UTC), so the
    // predicate models a *band* that covers every observed variant rather than
    // one broker's exact minute:
    //   daily   : [20:00, 03:00 next day] UTC
    //   weekend : [Friday 19:00, Monday 03:00] UTC
    // An interval counts as a closure ONLY when it lies wholly inside a single
    // such band. A gap that starts mid-session, or runs past the band, is a
    // defect — the predicate never degrades into "any gap is fine".
    const std::int64_t daySec = 86400;
    const std::int64_t hour = 3600;
    const std::int64_t tod = ((fromUtc % daySec) + daySec) % daySec;
    const std::int64_t startOfDay = fromUtc - tod;
    const std::int64_t days = fromUtc >= 0 ? fromUtc / daySec
                                           : -(( -fromUtc + daySec - 1) / daySec);
    const std::int64_t dow = ((days % 7) + 7 + 4) % 7;   // 0=Sun

    // Daily band containing `fromUtc`.
    std::int64_t bandStart = 0, bandEnd = 0;
    if (tod >= 20 * hour) {
        bandStart = startOfDay + 20 * hour;
        bandEnd = startOfDay + daySec + 3 * hour;
    } else if (tod <= 3 * hour) {
        bandStart = startOfDay - 4 * hour;               // previous day 20:00
        bandEnd = startOfDay + 3 * hour;
    } else {
        bandStart = bandEnd = 0;
    }
    if (bandEnd > bandStart && fromUtc >= bandStart && toUtc <= bandEnd) return true;

    // Weekend band: [Fri 19:00, Mon 03:00] UTC. It only applies when `fromUtc`
    // actually falls in that window (Fri/Sat/Sun, or the small hours of Monday);
    // a mid-week gap must never be mistaken for it.
    const bool inWeekendWindow =
        dow == 5 || dow == 6 || dow == 0 || (dow == 1 && tod <= 3 * hour);
    if (inWeekendWindow) {
        std::int64_t untilMon = (1 - dow + 7) % 7;       // days from dow to Monday
        if (dow == 1) untilMon = 0;                      // Monday itself
        const std::int64_t weekendEnd = startOfDay + untilMon * daySec + 3 * hour;
        const std::int64_t weekendStart = weekendEnd - 56 * hour;
        if (fromUtc >= weekendStart && toUtc <= weekendEnd) return true;
    }

    return false;
}

bool barsContiguousAllowingClosures(const std::vector<Bar>& bars, Timeframe tf,
                                    std::vector<GapInfo>* closures) noexcept {
    if (bars.size() < 2) return !bars.empty();
    const std::int64_t step = timeframeStepSeconds(tf);
    for (std::size_t i = 1; i < bars.size(); ++i) {
        std::int64_t d = bars[i].openTimeUtc - bars[i - 1].openTimeUtc;
        if (d <= 0) return false;              // out of order or duplicate
        if (step > 0 && d > step) {
            if (isScheduledClosure(bars[i - 1].openTimeUtc, bars[i].openTimeUtc)) {
                if (closures) {
                    GapInfo g;
                    g.prevOpenUtc = bars[i - 1].openTimeUtc;
                    g.nextOpenUtc = bars[i].openTimeUtc;
                    g.missingBars = (d / step) - 1;
                    closures->push_back(g);
                }
                continue;                      // expected absence, not a defect
            }
            return false;                      // a real gap during open hours
        }
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
