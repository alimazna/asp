#pragma once
// AYZR-ENG-100 — Strategy engine interface.
//
// Every specialised strategy engine implements this one interface and emits
// EngineResult values through the standard contract. An engine receives a
// frozen MarketView (closed bars only) and returns zero or more results. It must
// never fabricate data: when its required inputs are missing it returns an empty
// vector or a NO_TRADE / WARNING result with an explicit reason.

#include "ayzr/ecosystem/EngineContract.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {
namespace eco {

/// One closed OHLC bar. Only closed bars reach an engine, so there is no
/// look-ahead by construction. `openTimeUtc` is the bar's open, epoch seconds.
struct Bar {
    std::int64_t openTimeUtc = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t tickVolume = 0;
    std::int64_t spread = 0;      // points; 0 when the source did not supply it
    bool hasSpread = false;       // distinguishes a real 0 from "not supplied"
};

/// A frozen, read-only view of the market for one symbol/timeframe. `bars` are
/// ordered oldest -> newest and are all closed. The engine must not mutate it.
struct MarketView {
    std::string symbol = "XAUUSD";
    Timeframe timeframe = Timeframe::M15;
    std::vector<Bar> bars;
    /// Provenance of these bars ("biquote" | "dukascopy" | "mock").
    std::string dataSource;
    /// Worst data-quality state observed across the window. Engines propagate
    /// this into their result; a non-VALID view cannot produce a publishable
    /// opportunity (the orchestrator enforces this too).
    DataQualityState quality = DataQualityState::UNKNOWN;
    /// When the newest bar opened (the data timestamp).
    std::int64_t latestOpenUtc = 0;
    /// Wall-clock instant the snapshot was taken.
    std::int64_t snapshotUtc = 0;

    std::size_t size() const noexcept { return bars.size(); }
    const Bar& newest() const { return bars.back(); }
    bool has(std::size_t n) const noexcept { return bars.size() >= n; }
};

/// True when `bars` are strictly ascending in open time with no gaps larger than
/// one timeframe step. Used by engines that need contiguous history.
bool barsContiguous(const std::vector<Bar>& bars, Timeframe tf) noexcept;

/// Simple average of closes over the last `n` bars. nullopt when too few bars.
std::optional<double> sma(const std::vector<Bar>& bars, std::size_t n);
/// Exponential moving average (seeded with the SMA of the first n). nullopt when
/// there are fewer than n bars.
std::optional<double> ema(const std::vector<Bar>& bars, std::size_t n);
/// Wilder's Average True Range over `n` bars. nullopt when too few bars.
std::optional<double> atr(const std::vector<Bar>& bars, std::size_t n);
/// Population standard deviation of closes over the last `n` bars.
std::optional<double> stdev(const std::vector<Bar>& bars, std::size_t n);
/// RSI(n) using Wilder smoothing. nullopt when too few bars.
std::optional<double> rsi(const std::vector<Bar>& bars, std::size_t n);

/// The base every strategy engine implements.
class StrategyEngine {
public:
    virtual ~StrategyEngine() = default;

    /// Stable instance id, e.g. "trend-following-m15".
    virtual std::string id() const = 0;
    /// Engine version (bump when the logic changes).
    virtual std::string version() const = 0;
    /// Strategy family identity.
    virtual StrategyFamily family() const = 0;
    /// The operational timeframe this engine instance runs on.
    virtual Timeframe timeframe() const = 0;

    /// Evaluate one frozen view. Returns zero or more results. Must be
    /// side-effect free and deterministic for identical input.
    virtual std::vector<EngineResult> evaluate(const MarketView& view) const = 0;

    /// Health as of `nowUtc`. Engines that depend on unavailable inputs (e.g.
    /// microstructure without bid/ask) report UNAVAILABLE rather than guessing.
    virtual EngineHealth health(const MarketView& view, std::int64_t nowUtc) const;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
