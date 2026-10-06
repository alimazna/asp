#pragma once
// HOST-0009 - Top-level backend runtime.
//
// Owns the wiring between startup, ingestion, the data bus, and per-timeframe
// state. It exposes a single tick() that ingests closed bars for all
// timeframes; a failure in one timeframe never aborts the others.

#include "data/DataBus.h"
#include "data/MarketDataIngestor.h"
#include "data/TimeframeStateStore.h"
#include "foundation/ServiceState.h"
#include "foundation/SystemMode.h"
#include "foundation/Timestamp.h"
#include "guardian/IGuardian.h"
#include "health/HealthMonitor.h"
#include "ledger/PredictionLedger.h"
#include "outcomes/OutcomeEngine.h"
#include "persistence/FilePersistenceStore.h"
#include "persistence/PersistenceEngine.h"
#include "platform/windows/StartupCoordinator.h"
#include "runtime/DecisionPipeline.h"
#include "shadow/PositionSimulator.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aura {

struct RuntimeCycleReport {
    Timestamp at;
    int timeframesIngested = 0;
    int timeframesFailed = 0;
    int shadowCommandsIssued = 0;
    std::map<std::string, DataQualityState> qualityByTimeframe;
    std::vector<std::string> issues;
};

class AuraRuntime {
public:
    AuraRuntime(StartupOptions startupOptions = {},
                int candlesPerTimeframe = 500);

    // Run startup. Returns true when the runtime reached READY.
    bool start(std::string& error);

    // Ingest one cycle across all canonical timeframes. Never throws; reports.
    RuntimeCycleReport tick(Timestamp now);

    void stop();

    bool isReady() const noexcept { return ready_; }
    SystemMode mode() const noexcept { return mode_; }
    ServiceState bridgeState() const noexcept;

    const StartupReport& startupReport() const noexcept { return startupReport_; }
    TimeframeStateStore& timeframeStore() noexcept { return timeframeStore_; }
    const TimeframeStateStore& timeframeStore() const noexcept { return timeframeStore_; }
    DataBus& dataBus() noexcept { return dataBus_; }
    std::shared_ptr<IPythonBridgeClient> bridge() const noexcept {
        return startup_.bridgeClient();
    }

    // Decision/shadow surface. The facade reads these; nothing else mutates
    // them outside the runtime cycle.
    PredictionLedger& ledger() noexcept { return ledger_; }
    const PredictionLedger& ledger() const noexcept { return ledger_; }
    PositionSimulator& positions() noexcept { return positions_; }
    const PositionSimulator& positions() const noexcept { return positions_; }
    OutcomeEngine& outcomes() noexcept { return outcomes_; }
    const OutcomeEngine& outcomes() const noexcept { return outcomes_; }
    HealthMonitor& health() noexcept { return health_; }
    const HealthMonitor& health() const noexcept { return health_; }
    IGuardian* guardian() noexcept { return guardian_.get(); }
    const IGuardian* guardian() const noexcept { return guardian_.get(); }

    // Health summary for the frontend/API layer.
    std::map<std::string, std::string> healthSummary() const;

private:
    // Runs the live decision pipeline for a freshly closed operational bar.
    void runDecisionCycle(const Bar& closedBar, Timestamp now,
                          RuntimeCycleReport& report);

    StartupOptions startupOptions_;
    StartupCoordinator startup_;
    StartupReport startupReport_;
    int candlesPerTimeframe_;
    DataBus dataBus_;
    TimeframeStateStore timeframeStore_;
    std::unique_ptr<MarketDataIngestor> ingestor_;

    std::unique_ptr<IGuardian> guardian_;
    std::unique_ptr<FilePersistenceStore> store_;
    std::unique_ptr<PersistenceEngine> persistence_;
    std::unique_ptr<DecisionPipeline> pipeline_;
    PredictionLedger ledger_;
    PositionSimulator positions_;
    OutcomeEngine outcomes_;
    HealthMonitor health_;

    // Last bar published per timeframe, so an overlapping bridge window does
    // not duplicate closed-bar history.
    std::map<std::string, std::int64_t> lastPublishedBarOpenSec_;

    // Last operational bar evaluated per timeframe, so a re-tick does not
    // re-evaluate an already-processed bar.
    std::map<std::string, std::int64_t> lastDecisionBarOpenSec_;

    bool ready_ = false;
    SystemMode mode_ = SystemMode::STARTING;
};

}  // namespace aura
