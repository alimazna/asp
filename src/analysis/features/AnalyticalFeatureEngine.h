#pragma once
// Agent-A (T01) - Analytical feature engine.
//
// Builds TimeframeFeatures, CrossTimeframeFeatures and the combined
// AnalyticalFeatureSet from closed bars. Deterministic: identical input bars
// always produce identical output. Causal: only bars closed at or before the
// decision bar are read.

#include "analysis/features/AnalyticalFeatures.h"
#include "data/BarNormalizer.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstddef>
#include <map>
#include <vector>

namespace aura {

struct AnalyticalFeatureConfig {
    // Context (3-month) history for the per-timeframe structure/context window.
    std::size_t contextBars = 200;
    // Minimum trigger bars required to emit a valid per-timeframe vector.
    std::size_t minTriggerBars = 3;
    // Minimum context bars for the long-horizon context block to be meaningful.
    std::size_t minContextBars = 10;
};

class AnalyticalFeatureEngine {
public:
    explicit AnalyticalFeatureEngine(AnalyticalFeatureConfig config = {})
        : config_(config) {}

    // Per-timeframe features for one stream.
    //
    // `asOfBarOpenSec` pins the decision bar for causality: when >= 0, only
    // bars with `openTimeSec <= asOfBarOpenSec` are read, so the result is a
    // pure function of the past and cannot see later bars. The default (-1)
    // uses the last supplied bar as the decision bar.
    TimeframeFeatures computeTimeframe(const std::vector<Bar>& bars,
                                       Timeframe timeframe,
                                       std::int64_t asOfBarOpenSec = -1) const;

    // Cross-timeframe features. Looks up M15 / H4 / D1 in `byTimeframe`; a
    // missing stream is reported as unavailable, never fabricated.
    //
    // Causality: `asOfBarOpenSec` is the single decision instant applied to
    // EVERY stream, so M15/H4/D1 all describe the same moment. When < 0, the
    // common instant defaults to the latest supplied bar across the streams
    // (still causal: no stream reads a bar after it). Never per-stream tails.
    CrossTimeframeFeatures computeCross(
        const std::map<Timeframe, std::vector<Bar>>& byTimeframe,
        std::int64_t asOfBarOpenSec = -1) const;

    // The full set over all nine canonical streams. Streams absent from
    // `byTimeframe` are emitted with UNKNOWN quality and valid=false.
    //
    // Causality: one `asOfBarOpenSec` is threaded to every stream, so the set
    // is a single decision snapshot; `AnalyticalFeatureSet::asOfBarOpenSec` is
    // that common instant. When < 0 it defaults to the latest supplied bar
    // across all streams.
    AnalyticalFeatureSet computeAll(
        const std::map<Timeframe, std::vector<Bar>>& byTimeframe,
        std::int64_t asOfBarOpenSec = -1) const;

    const AnalyticalFeatureConfig& config() const noexcept { return config_; }

private:
    AnalyticalFeatureConfig config_;
};

}  // namespace aura
