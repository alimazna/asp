#pragma once
// DEC-0002 - Deterministic feature engine.
//
// Computes features from closed bars only. Insufficient history yields an
// UNKNOWN-quality snapshot rather than a partial one with fabricated values.

#include "data/BarNormalizer.h"
#include "features/FeatureSnapshot.h"

#include <cstddef>
#include <vector>

namespace aura {

struct FeatureConfig {
    std::size_t fastPeriod = 20;
    std::size_t slowPeriod = 50;
    std::size_t rsiPeriod = 14;
    std::size_t atrPeriod = 14;
    std::size_t momentumPeriod = 10;
    std::size_t volatilityPeriod = 20;
    std::size_t minBars = 55;
};

class FeatureEngine {
public:
    explicit FeatureEngine(FeatureConfig config = {}) : config_(config) {}

    const FeatureConfig& config() const noexcept { return config_; }

    FeatureSnapshot compute(const std::vector<Bar>& bars, Timeframe timeframe) const;

    static double ema(const std::vector<double>& values, std::size_t period);
    static double rsi(const std::vector<double>& closes, std::size_t period);
    static double atr(const std::vector<Bar>& bars, std::size_t period);
    static double stdev(const std::vector<double>& values);

private:
    FeatureConfig config_;
};

}  // namespace aura
