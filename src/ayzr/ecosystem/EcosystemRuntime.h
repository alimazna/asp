#pragma once
// AYZR-ECO-RT — Ecosystem runtime: engines + orchestrator + ledger, wired.
//
// Owns the concrete engine instances, evaluates them over a frozen MarketView,
// hands their results to the CentralOrchestrator, and returns the cycle. Also
// builds a MarketView from the server's CandleStore so the engines run on real
// persisted candles, never on fabricated data.
//
// This class holds no secrets and performs no I/O other than reading candles.

#include "ayzr/ecosystem/CentralOrchestrator.h"
#include "ayzr/ecosystem/DecisionLedger.h"
#include "ayzr/ecosystem/EcosystemJson.h"
#include "ayzr/engines/StrategyEngine.h"

#include <memory>
#include <string>
#include <vector>

namespace aura {
class CandleStore;
}

namespace aura {
namespace ayzr {
namespace eco {

class EcosystemRuntime {
public:
    /// Construct with a ledger (required) and a config. The ledger may be
    /// memory-only (tests) or SQLite-backed (server).
    explicit EcosystemRuntime(DecisionLedger* ledger,
                              OrchestratorConfig cfg = {},
                              const std::string& symbol = "XAUUSD");

    /// Build a MarketView from persisted candles for `timeframe`. Returns a view
    /// with empty bars when the store has none; the caller must treat that as
    /// "no data", not as a zero-price market. `nowUtc` stamps the snapshot.
    MarketView buildView(const CandleStore& store, Timeframe tf,
                         std::int64_t nowUtc, int limit = 300) const;

    /// Evaluate every engine on the view and run one orchestration cycle.
    CycleResult runCycle(const MarketView& view, std::int64_t nowUtc,
                         const std::string& correlationId);

    /// Descriptors for the engines route, including current health on `view`.
    std::vector<EngineDescriptor> describeEngines(const MarketView& view,
                                                std::int64_t nowUtc) const;

    /// The engine set (read-only), for tests.
    const std::vector<std::unique_ptr<StrategyEngine>>& engines() const {
        return engines_;
    }

    /// The instrument this runtime is scoped to.
    const std::string& symbol() const noexcept { return symbol_; }

private:
    DecisionLedger* ledger_;
    std::string symbol_;
    CentralOrchestrator orchestrator_;
    std::vector<std::unique_ptr<StrategyEngine>> engines_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
