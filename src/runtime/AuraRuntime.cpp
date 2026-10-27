// HOST-0010 - Backend runtime implementation.

#include "runtime/AuraRuntime.h"

#include "data/BarFinalizer.h"

namespace aura {

namespace {
// Structural bias of one timeframe's closed-bar stream, from its own structural
// trend (-1..1). A neutral or degenerate trend is RANGE; only a clear sign is a
// directional bias. Never returns a direction the data does not support.
StructureBias trendBiasOf(const TimeframeFeatures& tf) {
    if (!tf.valid) return StructureBias::UNKNOWN;
    if (tf.structureTrend > 0.0) return StructureBias::BULLISH;
    if (tf.structureTrend < 0.0) return StructureBias::BEARISH;
    return StructureBias::RANGE;
}
}  // namespace

AuraRuntime::AuraRuntime(StartupOptions startupOptions, int candlesPerTimeframe)
    : startupOptions_(std::move(startupOptions)),
      startup_(startupOptions_),
      candlesPerTimeframe_(candlesPerTimeframe),
      guardian_(makeGuardian()) {}

void AuraRuntime::publishStartupReport(int slot) {
    StartupReport& snapshot = publishedStartupSnapshot_[slot & 1];
    snapshot = startupReport_;
    publishedStartupReport_.store(&snapshot, std::memory_order_release);
}

bool AuraRuntime::resolveAsync(std::string& error) {
    ready_.store(false, std::memory_order_relaxed);
    mode_.store(SystemMode::STARTING, std::memory_order_relaxed);

    // Phase 0: resolve paths and locate the bridge bundle, then construct every
    // object the API reads (ingestor, persistence, pipeline). No process spawn
    // and no network, so this returns in milliseconds. The host binds its
    // loopback API immediately after this call and before the bridge spawn, so
    // /health answers within the 10s budget even when spawning the bridge blocks
    // for a long time (AV scanning a fresh bridge.exe).
    const bool resolved = startup_.resolve(startupReport_);
    publishStartupReport(0);
    // Build everything the API reads (store, persistence, pipeline) now, before
    // the caller binds the API. The ingestor is deliberately NOT built here: it
    // needs the bridge client, which only exists after the spawn, and creating it
    // is a pointer assignment the API never reads.
    buildRuntimeComponents();
    if (!resolved) {
        finalizeState(error);
        return false;
    }
    error.clear();
    return true;
}

bool AuraRuntime::startAsync(std::string& error) {
    // Phase 1: spawn the bridge. This returns without waiting for the handshake
    // so the host can answer STARTING while MT5 cold-starts. Requires
    // resolveAsync(); if the caller skipped it, resolve now (still before the
    // spawn) so the report is populated.
    if (startupReport_.stage == StartupStage::NOT_STARTED) {
        if (!resolveAsync(error)) return false;
    }
    const bool begun = startup_.launch(startupReport_);
    publishStartupReport(1);
    if (!begun) {
        finalizeState(error);
        return false;
    }
    // The bridge client now exists. Attach the ingestor (idempotent; only the
    // runtime thread touches it, so this cannot race an API read).
    if (!ingestor_) {
        if (auto client = startup_.bridgeClient()) {
            ingestor_ = std::make_unique<MarketDataIngestor>(client);
        }
    }
    error.clear();
    return true;
}

bool AuraRuntime::finishStartup(std::string& error) {
    // Phase 2: block for the handshake, then publish the final report and flip
    // the atomics the API reads. No shared object is (re)constructed here, so an
    // API request served during this wait is safe.
    if (startup_.bridgeClient()) {
        startup_.completeHandshake(startupReport_);
    }
    publishStartupReport(2);
    return finalizeState(error);
}

bool AuraRuntime::start(std::string& error) {
    if (!resolveAsync(error)) return false;
    if (!startAsync(error)) return false;
    return finishStartup(error);
}

void AuraRuntime::buildRuntimeComponents() {
    // Durable state lives under the resolved data directory. When paths are
    // unavailable (unresolved executable) the pipeline still runs, but nothing
    // is persisted and that is reported rather than faked.
    if (!startupReport_.paths.dataDir.empty()) {
        store_ = std::make_unique<FilePersistenceStore>(startupReport_.paths.dataDir);
        if (store_->isAvailable()) {
            persistence_ = std::make_unique<PersistenceEngine>(store_.get());
        } else {
            store_.reset();
        }
    }
    // Research memory is durable and derived from outcomes/failures only. It
    // grants no execution authority.
    experiments_ = ExperimentLedger(store_.get());
    failures_ = FailureMemory(store_.get());
    pipeline_ = std::make_unique<DecisionPipeline>(
        guardian_.get(), &ledger_, persistence_.get(), &positions_, &outcomes_);
    pipeline_->setSymbol(startupReport_.resolvedSymbol.empty()
                             ? startupOptions_.preferredSymbol
                             : startupReport_.resolvedSymbol);
}

bool AuraRuntime::finalizeState(std::string& error) {
    const bool started = startupReport_.ready;
    // The handshake may have resolved a symbol the phase-1 wiring could not
    // know yet; refine it (idempotent when unchanged).
    if (pipeline_ && !startupReport_.resolvedSymbol.empty()) {
        pipeline_->setSymbol(startupReport_.resolvedSymbol);
    }

    if (!started) {
        // The runtime is allowed to exist in DEGRADED mode without the bridge;
        // it simply cannot ingest. This is explicit, not silent.
        ready_.store(false, std::memory_order_release);
        mode_.store(startupReport_.stage == StartupStage::FAILED
                        ? SystemMode::EMERGENCY
                        : SystemMode::DEGRADED,
                    std::memory_order_release);
        error = startupReport_.error;
        return false;
    }

    ready_.store(true, std::memory_order_release);
    mode_.store(SystemMode::SHADOW, std::memory_order_release);  // shadow-first mode
    error.clear();
    return true;
}

RuntimeCycleReport AuraRuntime::tick(Timestamp now) {
    RuntimeCycleReport report;
    report.at = now;

    if (!ingestor_) {
        report.issues.push_back("no ingestor (bridge unavailable)");
        if (ready_) {
            mode_ = SystemMode::DEGRADED;
        }
        return report;
    }

    // Refresh the broker clock offset from bridge health, then shift the host's
    // UTC clock to broker server time for every bar-time comparison below. Bars
    // are stamped in server time; the raw UTC clock made recent bars appear
    // future-dated, so they were rejected by validation and the tracker only
    // kept the oldest covering timeframe. On a UTC-aligned feed the offset is 0
    // and this is a no-op.
    if (auto client = bridge()) {
        const auto bridgeHealth = client->health();
        if (bridgeHealth.ok) {
            serverClockOffsetSec_.store(bridgeHealth.value.serverClockOffsetSeconds,
                                        std::memory_order_release);
        }
    }
    const std::int64_t clockOffsetSec =
        serverClockOffsetSec_.load(std::memory_order_acquire);
    const Timestamp effectiveNow =
        clockOffsetSec == 0
            ? now
            : Timestamp::fromEpochMillis(now.epochMillis() + clockOffsetSec * 1000);

    for (Timeframe timeframe : allTimeframes()) {
        IngestRequest request;
        request.symbol = startupReport_.resolvedSymbol.empty()
                             ? startupOptions_.preferredSymbol
                             : startupReport_.resolvedSymbol;
        request.timeframe = timeframe;
        request.count = candlesPerTimeframe_;
        request.closedOnly = true;
        // allow_stale: when the market is closed the newest real closed bars
        // come back (validation flags them STALE) instead of an error, so the
        // timeframe tracker and the decision context are fed with real data
        // rather than staying empty. A stale bar is never decision-grade, so
        // no decision is fabricated from it.
        request.allowStale = true;

        IngestResult result = ingestor_->ingest(request, effectiveNow);
        report.qualityByTimeframe[toString(timeframe)] = result.quality;
        if (!result.ok) {
            ++report.timeframesFailed;
            for (const auto& issue : result.issues) {
                report.issues.push_back(std::string(toString(timeframe)) + ": " + issue);
            }
            timeframeStore_.refreshFreshness(timeframe, effectiveNow);
            continue;
        }

        // Only fully-closed bars advance timeframe state.
        BarFinalizer finalizer;
        FinalizedBars finalized = finalizer.finalize(result.bars, effectiveNow);
        if (finalized.closedBars.empty()) {
            ++report.timeframesFailed;
            report.issues.push_back(std::string(toString(timeframe)) +
                                    ": no closed bars available");
            timeframeStore_.refreshFreshness(timeframe, effectiveNow);
            continue;
        }

        // The bridge returns a rolling window, so each tick overlaps the last.
        // Publish only bars strictly newer than the last published bar for the
        // timeframe; re-publishing a closed bar would duplicate history.
        const std::string key = toString(timeframe);
        const std::int64_t lastPublished = lastPublishedBarOpenSec_[key];
        std::uint64_t lastSequence = dataBus_.sequence(timeframe);
        const Bar* newest = nullptr;
        for (const auto& bar : finalized.closedBars) {
            if (bar.openTimeSec <= lastPublished) continue;
            lastSequence = dataBus_.publish(bar, "mt5-python-bridge", now);
            lastPublishedBarOpenSec_[key] = bar.openTimeSec;
            newest = &bar;
        }
        if (newest == nullptr) {
            // No new closed bar this cycle; refresh freshness and move on.
            timeframeStore_.refreshFreshness(timeframe, effectiveNow);
            ++report.timeframesIngested;
            continue;
        }
        timeframeStore_.updateFromClosedBar(*newest, lastSequence);
        timeframeStore_.refreshFreshness(timeframe, effectiveNow);
        ++report.timeframesIngested;

        // The operational timeframe drives the live decision pipeline. Other
        // timeframes only advance open-position simulation when relevant.
        if (timeframe == pipeline_->config().operationalTimeframe) {
            runDecisionCycle(*newest, now, report);
        }
        if (positions_.openCount() > 0) {
            std::vector<EntityId> closedOutcomes;
            pipeline_->advancePositions(*newest, now, report.issues,
                                        &closedOutcomes);
            for (const auto& outcomeId : closedOutcomes) {
                appendAudit(AuditAction::OUTCOME_RECORDED, outcomeId.value(),
                            "outcome recorded from closed shadow position", now);
            }
        }
    }

    // Refresh the health view so the API layer reports the current state
    // rather than a stale one. Health observation is non-fatal.
    health_.observeTimeframes(timeframeStore_, now);
    if (auto client = bridge()) {
        health_.observeBridge(*client, now);
        const auto bridgeHealth = client->health();
        std::lock_guard<std::mutex> lock(bridgeHealthMutex_);
        if (bridgeHealth.ok) {
            lastBridgeHealth_ = bridgeHealth.value;
            lastBridgeHealthValid_ = true;
        } else {
            lastBridgeHealthValid_ = false;
        }
    }

    // Publish a cached health snapshot for the API layer. /health reads this
    // copy instead of calling snapshot() live, so a health probe never blocks
    // on the engine lock this tick writes through — and never serializes
    // behind the slowest part of the cycle (the bridge proxy). One consistent
    // observation is captured under the lock, then released.
    health_.refreshCache(now);

    // Operating mode reflects data availability. M15 is the primary
    // operational timeframe; if it is not fresh, the system is not SHADOW.
    const DataQualityState m15 = timeframeStore_.qualityOf(Timeframe::M15);
    if (!isDecisionGrade(m15)) {
        mode_ = SystemMode::DEGRADED;
    } else if (report.timeframesFailed == 0) {
        mode_ = SystemMode::SHADOW;
    } else {
        mode_ = SystemMode::DEGRADED;
    }
    return report;
}

// Seed the analysis bus with persisted closed history. See the header for why
// this exists: the bus is in-memory, the store is durable, and the multi-
// timeframe gate needs three closed bars per stream before it will evaluate.
void AuraRuntime::seedHistory(Timeframe timeframe, const std::vector<Bar>& bars,
                              const std::string& symbol, Timestamp now) {
    if (bars.empty()) return;
    const std::string key = toString(timeframe);
    // Only publish bars strictly newer than whatever is already on the bus, and
    // only in ascending order, so a seed never rewinds or duplicates a stream.
    std::int64_t lastOpen = lastPublishedBarOpenSec_[key];
    for (const Bar& bar : bars) {
        if (bar.openTimeSec <= lastOpen) continue;
        const std::uint64_t seq = dataBus_.publish(bar, "candle-store", now);
        lastPublishedBarOpenSec_[key] = bar.openTimeSec;
        lastOpen = bar.openTimeSec;
        timeframeStore_.updateFromClosedBar(bar, seq);
    }
    const std::string sym = symbol.empty() ? "XAUUSD" : symbol;
    pipeline_->setSymbol(sym);
    // Publishing bars must refresh the health view, exactly as the ingest and
    // tick paths do. Seeding only the bus left /health reporting STARTING with
    // observed_at -1 on a remote-fed backend (no tick thread on Railway), so the
    // operator saw "no state observed" for every timeframe while /analysis/latest
    // simultaneously reported the gate open on the same seeded data.
    health_.observeTimeframes(timeframeStore_, now);
    health_.refreshCache(now);
}

// Evaluate the objective implied by the history on the bus. Reuses the exact
// causal decision path the ingest route uses, so seeding cannot produce a
// decision the live path would not.
void AuraRuntime::evaluateFromHistory(Timestamp now, RuntimeCycleReport& report) {
    if (!pipeline_) return;
    const Timeframe op = pipeline_->config().operationalTimeframe;
    const std::vector<Bar> history = dataBus_.recent(op, pipeline_->config().historyBars);
    if (history.empty()) return;
    runDecisionCycle(history.back(), now, report);
}

RuntimeCycleReport AuraRuntime::ingestExternalBars(
    const std::string& source, const std::string& symbol, Timeframe timeframe,
    const std::vector<Bar>& bars, Timestamp now) {
    RuntimeCycleReport report;
    report.at = now;

    if (bars.empty()) {
        report.issues.push_back("no bars in external ingestion batch");
        return report;
    }
    // Validate every bar (defense in depth; the sender already validates).
    // Per-bar validation mirrors the bridge-fed tick path.
    BarFinalizer finalizer;
    DataValidator validator;
    DataQualityState worst = DataQualityState::VALID;
    std::vector<Bar> closedBars;
    for (const auto& bar : bars) {
        const BarValidation validation = validator.validateBar(bar, now);
        if (!validation.ok) {
            worst = DataQualityState::INVALID;
            report.issues.push_back(std::string(toString(timeframe)) + ": " +
                                    validation.detail);
            continue;
        }
        if (!finalizer.isClosed(bar, now)) {
            report.issues.push_back(std::string(toString(timeframe)) +
                                    ": rejecting not-yet-closed bar open=" +
                                    std::to_string(bar.openTimeSec));
            continue;
        }
        // Grid alignment: a bar whose open time is not on its timeframe grid
        // is not real data for that timeframe and must never advance state.
        if (!openTimeAligned(timeframe, bar.openTimeSec)) {
            worst = DataQualityState::INVALID;
            report.issues.push_back(std::string(toString(timeframe)) +
                                    ": rejecting misaligned bar open=" +
                                    std::to_string(bar.openTimeSec));
            continue;
        }
        closedBars.push_back(bar);
    }
    if (closedBars.empty()) {
        ++report.timeframesFailed;
        report.qualityByTimeframe[toString(timeframe)] = DataQualityState::INVALID;
        timeframeStore_.refreshFreshness(timeframe, now);
        report.issues.push_back(std::string(toString(timeframe)) +
                                ": no valid closed bars");
        return report;
    }

    // Publish only bars strictly newer than the last published bar (dedup).
    const std::string key = toString(timeframe);
    const std::int64_t lastPublished = lastPublishedBarOpenSec_[key];
    std::uint64_t lastSequence = dataBus_.sequence(timeframe);
    const Bar* newest = nullptr;
    for (const auto& bar : closedBars) {
        if (bar.openTimeSec <= lastPublished) continue;
        lastSequence = dataBus_.publish(bar, source, now);
        lastPublishedBarOpenSec_[key] = bar.openTimeSec;
        newest = &bar;
    }
    if (newest == nullptr) {
        // No genuinely new closed bar; only freshness advances.
        timeframeStore_.refreshFreshness(timeframe, now);
        ++report.timeframesIngested;
        return report;
    }

    timeframeStore_.updateFromClosedBar(*newest, lastSequence);
    timeframeStore_.refreshFreshness(timeframe, now);
    ++report.timeframesIngested;
    report.qualityByTimeframe[toString(timeframe)] = DataQualityState::VALID;

    // Remote-fed mode has no local MT5 handshake, so nothing else will ever
    // flip readiness. The first closed bar that actually reaches the store is
    // the evidence that the ingestion path works end to end; that is the point
    // at which the service is genuinely ready to serve. Readiness is never
    // granted before real data exists.
    if (!startupOptions_.launchBridgeProcess && !ready_.load(std::memory_order_acquire)) {
        ready_.store(true, std::memory_order_release);
    }

    // The operational timeframe drives the live decision pipeline, exactly as
    // in tick().
    const std::string sym = symbol.empty() ? "XAUUSD" : symbol;
    pipeline_->setSymbol(sym);
    if (timeframe == pipeline_->config().operationalTimeframe) {
        runDecisionCycle(*newest, now, report);
    }
    if (positions_.openCount() > 0) {
        std::vector<EntityId> closedOutcomes;
        pipeline_->advancePositions(*newest, now, report.issues, &closedOutcomes);
        for (const auto& outcomeId : closedOutcomes) {
            appendAudit(AuditAction::OUTCOME_RECORDED, outcomeId.value(),
                        "outcome recorded from closed shadow position", now);
        }
    }

    // Refresh the health view so the API layer reports ingested state.
    health_.observeTimeframes(timeframeStore_, now);
    health_.refreshCache(now);

    const DataQualityState m15 = timeframeStore_.qualityOf(Timeframe::M15);
    if (!isDecisionGrade(m15)) {
        mode_ = SystemMode::DEGRADED;
    } else {
        mode_ = SystemMode::SHADOW;
    }
    return report;
}

void AuraRuntime::runDecisionCycle(const Bar& closedBar, Timestamp now,
                                   RuntimeCycleReport& report) {
    if (!pipeline_) return;
    const std::string key = toString(closedBar.timeframe);
    if (closedBar.openTimeSec <= lastDecisionBarOpenSec_[key]) {
        return;   // this bar was already evaluated
    }

    const std::vector<Bar> history = dataBus_.recent(
        closedBar.timeframe, pipeline_->config().historyBars);
    // Cross-timeframe objective for the same decision instant, built from the
    // closed-bar streams already on the bus. When M15/H4 are not both present
    // the core gate fails and the chain suppresses the decision honestly.
    const MtfContext mtf = buildMtfContext(closedBar.openTimeSec);
    const PipelineCycleReport decision =
        pipeline_->onClosedBar(closedBar, history, now, mtf);
    lastDecisionBarOpenSec_[key] = closedBar.openTimeSec;

    if (decision.decisionProduced && !decision.decisions.empty()) {
        appendAudit(AuditAction::DECISION_PRODUCED,
                    decision.decisions.front().decisionId.value(),
                    "decision evaluated on closed bar", now);
    }
    if (decision.shadowIssued) {
        report.shadowCommandsIssued += 1;
        if (!decision.decisions.empty()) {
            appendAudit(AuditAction::SHADOW_COMMAND_ISSUED,
                        decision.decisions.front().decisionId.value(),
                        "shadow command issued", now);
        }
    }
    for (const auto& issue : decision.issues) {
        report.issues.push_back(key + ": " + issue);
    }
}

// Cross-timeframe context for one decision instant. Deterministic and causal:
// only bars with open time <= asOf are read (the analytical engine enforces the
// cutoff). Returns available=false when the operational (M15) stream has no
// closed history yet, so the caller does not run a cross analysis on nothing.
MtfContext AuraRuntime::buildMtfContext(std::int64_t asOfBarOpenSec) const {
    MtfContext out;
    if (!pipeline_) return out;

    const Timeframe op = pipeline_->config().operationalTimeframe;
    const std::size_t bars = 300;   // ~3 days of M15; enough for H4/D1 context

    std::map<Timeframe, std::vector<Bar>> byTimeframe;
    byTimeframe[op] = dataBus_.recent(op, bars);
    byTimeframe[Timeframe::H4] = dataBus_.recent(Timeframe::H4, bars);
    byTimeframe[Timeframe::D1] = dataBus_.recent(Timeframe::D1, bars);

    if (byTimeframe[op].empty()) {
        // No operational history at all: nothing to verify. Not an error, just
        // an unavailable objective.
        return out;
    }

    static const AnalyticalFeatureEngine engine{};
    const AnalyticalFeatureSet set =
        engine.computeAll(byTimeframe, asOfBarOpenSec);
    const CrossTimeframeFeatures& cross = set.cross;

    out.available = true;
    out.h4Available = cross.h4Available;
    out.d1Available = cross.d1Available;
    out.h4M15Agreement = cross.h4M15Agreement;
    out.h4D1Agreement = cross.h4D1Agreement;
    out.mtfConflictScore = cross.mtfConflictScore;
    out.h4StructuralAuthority = cross.h4StructuralAuthority;
    out.m15TriggerState = cross.m15TriggerState;

    // H4 objective from its own structural trend (independent of the M15-only
    // structure the chain computes on the operational timeframe).
    for (const auto& tf : set.perTimeframe) {
        if (tf.timeframe == Timeframe::H4 && tf.valid) {
            out.h4Bias = trendBiasOf(tf);
        }
    }

    // Core gate: the operational trigger and the structural authority must both
    // be present and decision-grade. This mirrors the research gate
    // `cross.m15Available && cross.h4Available` and is the honest blocker.
    const bool m15Ok = cross.m15Available;
    const bool h4Ok = cross.h4Available;
    out.coreGatePassed = m15Ok && h4Ok;
    if (!out.coreGatePassed) {
        if (!m15Ok && !h4Ok) {
            out.blockReason =
                "multi-timeframe core gate: M15 and H4 streams unavailable";
        } else if (!h4Ok) {
            out.blockReason =
                "multi-timeframe core gate: H4 structural authority unavailable";
        } else {
            out.blockReason =
                "multi-timeframe core gate: M15 operational stream unavailable";
        }
    }
    return out;
}

void AuraRuntime::appendAudit(AuditAction action, const std::string& subject,
                              const std::string& details, Timestamp now) {
    AuditRecord record;
    record.eventId = EntityId(std::string(toString(action)) + "-" +
                              std::to_string(audit_.size() + 1));
    record.action = action;
    record.outcome = AuditOutcome::SUCCESS;
    record.serviceState = ServiceState::ONLINE;
    record.occurredAt = now;
    record.actor = "aura-runtime";
    record.subject = subject;
    record.details = details;
    audit_.append(record);
}

void AuraRuntime::stop() {
    startup_.shutdown();
    ready_ = false;
    mode_ = SystemMode::HALTED;
}

bool AuraRuntime::bridgeHealth(BridgeHealth& out) const {
    auto client = const_cast<StartupCoordinator&>(startup_).bridgeClient();
    if (!client) return false;
    const auto result = client->health();
    if (!result.ok) return false;
    out = result.value;
    return true;
}

ServiceState AuraRuntime::bridgeState() const noexcept {
    ProcessSupervisor* supervisor = const_cast<StartupCoordinator&>(startup_).supervisor();
    if (supervisor == nullptr) return ServiceState::OFFLINE;
    return supervisor->state();
}

std::map<std::string, std::string> AuraRuntime::healthSummary() const {
    std::map<std::string, std::string> summary;
    summary["mode"] = toString(mode_);
    summary["ready"] = ready_ ? "true" : "false";
    const StartupReport& report = startupReport();
    summary["startup_stage"] = toString(report.stage);
    summary["bridge_process"] = toString(bridgeState());
    summary["bridge_handshake"] = report.handshakeOk ? "OK" : "NOT_OK";
    summary["mt5_ready"] = report.mt5Ready ? "true" : "false";
    summary["resolved_symbol"] = report.resolvedSymbol;
    for (Timeframe timeframe : allTimeframes()) {
        summary[std::string("quality_") + toString(timeframe)] =
            toString(timeframeStore_.qualityOf(timeframe));
    }
    return summary;
}

}  // namespace aura
