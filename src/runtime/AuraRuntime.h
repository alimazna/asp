#pragma once
// HOST-0009 - Top-level backend runtime.
//
// Owns the wiring between startup, ingestion, the data bus, and per-timeframe
// state. It exposes a single tick() that ingests closed bars for all
// timeframes; a failure in one timeframe never aborts the others.

#include "audit/AuditLog.h"
#include "data/DataBus.h"
#include "data/MarketDataIngestor.h"
#include "data/TimeframeStateStore.h"
#include "foundation/ServiceState.h"
#include "foundation/SystemMode.h"
#include "foundation/Timestamp.h"
#include "research/ExperimentLedger.h"
#include "research/FailureMemory.h"
#include "guardian/IGuardian.h"
#include "health/HealthMonitor.h"
#include "ledger/PredictionLedger.h"
#include "outcomes/OutcomeEngine.h"
#include "persistence/FilePersistenceStore.h"
#include "persistence/PersistenceEngine.h"
#include "platform/windows/StartupCoordinator.h"
#include "runtime/DecisionPipeline.h"
#include "shadow/PositionSimulator.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
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

    // Run startup synchronously. Returns true when the runtime reached READY.
    bool start(std::string& error);

    // Phase-0 startup: resolve paths, locate the bridge bundle, and construct the
    // runtime components. No process spawn and no network, so it returns in
    // milliseconds. The host binds its loopback API after this and before the
    // (possibly slow) bridge spawn, so the frontend always reaches /health
    // promptly even when spawning the bridge blocks for tens of seconds.
    // Returns false when paths/bundle are unavailable (reason in the report).
    bool resolveAsync(std::string& error);

    // Phase-1 startup that returns as soon as the bridge process is launched,
    // WITHOUT waiting for the handshake. Lets the host answer the frontend
    // immediately (aggregate=STARTING) instead of staying silent while MT5
    // cold-starts for up to a minute. Requires resolveAsync() first. Returns true
    // when the bridge is on its way up; false when the spawn failed or the bundle
    // was missing (the reason is in startupReport().error). Call finishStartup()
    // afterwards.
    bool startAsync(std::string& error);

    // Phase 2 of startAsync(): block for the handshake and finalize the runtime
    // (ingestor, persistence, pipeline, ready/mode). Returns true when READY.
    // Safe to call on a background thread; the API reads the phase-1 report
    // concurrently, which is why the host publishes the report through an
    // atomic pointer rather than in place.
    bool finishStartup(std::string& error);

    // Ingest one cycle across all canonical timeframes. Never throws; reports.
    RuntimeCycleReport tick(Timestamp now);

    void stop();

    // Atomic so the API thread can read them while the startup thread finalizes.
    bool isReady() const noexcept { return ready_.load(std::memory_order_acquire); }
    SystemMode mode() const noexcept { return mode_.load(std::memory_order_acquire); }
    ServiceState bridgeState() const noexcept;

    // The startup report published by the startup thread. Read through an atomic
    // pointer so the API thread can safely observe the phase-1 report (STARTING)
    // while the handshake is still in progress on another thread; without this
    // the API read a report being written concurrently (a data race).
    const StartupReport& startupReport() const noexcept {
        const StartupReport* r = publishedStartupReport_.load(std::memory_order_acquire);
        return r != nullptr ? *r : startupReport_;
    }
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
    AuditLog& audit() noexcept { return audit_; }
    const AuditLog& audit() const noexcept { return audit_; }
    ExperimentLedger& experiments() noexcept { return experiments_; }
    const ExperimentLedger& experiments() const noexcept { return experiments_; }
    FailureMemory& failures() noexcept { return failures_; }
    const FailureMemory& failures() const noexcept { return failures_; }
    HealthMonitor& health() noexcept { return health_; }
    const HealthMonitor& health() const noexcept { return health_; }
    IGuardian* guardian() noexcept { return guardian_.get(); }
    const IGuardian* guardian() const noexcept { return guardian_.get(); }

    // Metadata for the most recent decision (structure/regime/eligibility and
    // risk proposal). Available == false until a decision is produced.
    DecisionContext lastDecisionContext() const {
        return pipeline_ != nullptr ? pipeline_->lastDecisionContext()
                                    : DecisionContext{};
    }
    std::string resolvedSymbol() const { return startupReport().resolvedSymbol; }
    bool mt5Ready() const noexcept { return startupReport().mt5Ready; }
    bool bridgeHandshakeOk() const noexcept { return startupReport().handshakeOk; }
    StartupStage startupStage() const noexcept { return startupReport().stage; }

    // Bridge identity/health, queried live from the loopback bridge client.
    // Returns false (with `out` left default) when the bridge is unavailable;
    // it never fabricates broker/MT5 detail.
    bool bridgeHealth(BridgeHealth& out) const;

    // Last bridge health observed during a tick. Read-only and non-blocking,
    // for the API layer. Returns false when no observation has succeeded. The
    // cache is guarded because /bridge/status reads it on an API worker thread
    // while tick() writes it on the runtime thread.
    bool lastBridgeHealth(BridgeHealth& out) const {
        std::lock_guard<std::mutex> lock(bridgeHealthMutex_);
        if (!lastBridgeHealthValid_) return false;
        out = lastBridgeHealth_;
        return true;
    }

    // Current (broker server clock) - (local UTC) offset in seconds. Applied to
    // the host's UTC clock so server-time-stamped bars are judged correctly.
    std::int64_t serverClockOffsetSeconds() const noexcept {
        return serverClockOffsetSec_.load(std::memory_order_acquire);
    }

    // Health summary for the frontend/API layer.
    std::map<std::string, std::string> healthSummary() const;

private:
    // Runs the live decision pipeline for a freshly closed operational bar.
    void runDecisionCycle(const Bar& closedBar, Timestamp now,
                          RuntimeCycleReport& report);

    // Append an audit record. Best-effort: audit failure never aborts a cycle.
    void appendAudit(AuditAction action, const std::string& subject,
                     const std::string& details, Timestamp now);

    // Snapshot the phase-1 (or final) report into an unused buffer, then publish
    // its address. The startup thread and the API thread never touch the same
    // buffer, so the API can read STARTING while the handshake is still running.
    void publishStartupReport(int slot);

    // Construct the ingestor/persistence/pipeline from the current report. Called
    // before the API binds so handler threads never see a half-built runtime.
    void buildRuntimeComponents();

    // Set ready_/mode_ from the current report. Shared by the synchronous and
    // split startup paths.
    bool finalizeState(std::string& error);

    StartupOptions startupOptions_;
    StartupCoordinator startup_;
    StartupReport startupReport_;
    StartupReport publishedStartupSnapshot_[2];
    // Address of the currently published snapshot, or nullptr before startup.
    std::atomic<const StartupReport*> publishedStartupReport_{nullptr};
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
    AuditLog audit_;
    ExperimentLedger experiments_;
    FailureMemory failures_;
    BridgeHealth lastBridgeHealth_;
    bool lastBridgeHealthValid_ = false;
    mutable std::mutex bridgeHealthMutex_;

    // (broker server clock) - (local UTC), seconds. Bars arrive stamped in the
    // broker's server timezone (commonly UTC+2/+3); the host shifts its own UTC
    // clock by this offset before judging bar closure or validating bar times,
    // so a live bar is never rejected as future-dated. Atomic because the tick
    // thread writes it while API workers read it.
    std::atomic<std::int64_t> serverClockOffsetSec_{0};

    // Last bar published per timeframe, so an overlapping bridge window does
    // not duplicate closed-bar history.
    std::map<std::string, std::int64_t> lastPublishedBarOpenSec_;

    // Last operational bar evaluated per timeframe, so a re-tick does not
    // re-evaluate an already-processed bar.
    std::map<std::string, std::int64_t> lastDecisionBarOpenSec_;

    std::atomic<bool> ready_{false};
    std::atomic<SystemMode> mode_{SystemMode::STARTING};
};

}  // namespace aura
