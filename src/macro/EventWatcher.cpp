// Event watcher implementation.

#include "macro/EventWatcher.h"

#include "api/BackendApiSchema.h"
#include "foundation/Timestamp.h"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace aura {
namespace {

// The two turn names as they appear in the store and the activity log.
constexpr char kExpectation[] = "expectation";
constexpr char kResult[] = "result";

std::int64_t nowSeconds() {
    return Timestamp::now().epochMillis() / 1000;
}

}  // namespace

EventWatcher::EventWatcher(EventStore* store, ICalendarSource* calendar,
                           NewsAnalysisService* service,
                           EventContextProvider context, ActivityLog* activityLog)
    : store_(store),
      calendar_(calendar),
      service_(service),
      context_(std::move(context)),
      activityLog_(activityLog) {}

EventWatcher::~EventWatcher() { stop(); }

void EventWatcher::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
}

void EventWatcher::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EventWatcher::loop() {
    // Consecutive calendar failures drive a bounded exponential backoff. A
    // persistently failing feed must not be polled every tick: that is a request
    // storm against a third party and it will not fix itself any faster.
    int consecutiveFailures = 0;
    while (running_.load()) {
        const TickResult tick = runOnce(nowSeconds());
        int waitSeconds = options_.tickSeconds;
        if (!tick.calendarError.empty()) {
            // A calendar outage is reported, not hidden. The loop keeps running
            // so recovery is automatic when the feed returns.
            std::cerr << "  WARN: event watcher calendar: " << tick.calendarError
                      << std::endl;
            ++consecutiveFailures;
            // 2x per failure, capped. The cap keeps recovery bounded: once the
            // feed returns, the next probe is at most one cap away.
            const int shift = std::min(consecutiveFailures, 6);
            waitSeconds = std::min(options_.tickSeconds << shift,
                                   options_.maxBackoffSeconds);
        } else {
            consecutiveFailures = 0;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(waitSeconds),
                     [this] { return !running_.load(); });
    }
}

EventWatcher::TickResult EventWatcher::runOnce(std::int64_t nowUtc) {
    TickResult out;
    if (store_ == nullptr || !store_->isOpen()) {
        out.calendarError = "event store is not open";
        return out;
    }

    // 1. Refresh the calendar. A failure leaves the previously stored events in
    //    place: stale knowledge beats no knowledge, and the release turn will
    //    still fire for anything already known.
    if (calendar_ != nullptr) {
        std::vector<CalendarEvent> events;
        std::string error;
        if (calendar_->fetch(events, error)) {
            out.calendarFetched = static_cast<int>(events.size());
            std::string upsertError;
            if (!store_->upsertEvents(events, nowUtc, out.eventsInserted,
                                      out.eventsUpdated, upsertError)) {
                out.calendarError = "store: " + upsertError;
            }
            record("calendar_fetch", events.empty() ? "empty" : "ok", "",
                   "fetched " + std::to_string(events.size()) +
                       " events (new=" + std::to_string(out.eventsInserted) +
                       " updated=" + std::to_string(out.eventsUpdated) + ")",
                   -1);
        } else {
            out.calendarError = error;
            record("calendar_fetch", "unavailable", error, "calendar feed", -1);
        }
    }

    // 2. Prepare expectations for events approaching release.
    const std::vector<ScheduledEvent> toPrepare = store_->dueForPrepare(
        nowUtc, options_.prepareHorizonSeconds, options_.maxPreparePerTick);
    for (const auto& event : toPrepare) {
        if (!running_.load() && !toPrepare.empty() && thread_.joinable()) {
            // Shutdown requested mid-tick: stop before spending provider quota.
            break;
        }
        prepareEvent(event, nowUtc, out);
    }

    // 3. Release events whose time has arrived.
    const std::vector<ScheduledEvent> toRelease =
        store_->dueForRelease(nowUtc, options_.maxReleasePerTick);
    for (const auto& event : toRelease) {
        releaseEvent(event, nowUtc, out);
    }

    return out;
}

EventPromptContext EventWatcher::buildContext(const ScheduledEvent& event) const {
    EventPromptContext ctx;
    ctx.title = event.title;
    ctx.country = event.country;
    ctx.impact = event.impact;
    ctx.scheduledAtUtc = event.scheduledAtUtc;
    ctx.scheduledAtIso = isoUtcSeconds(event.scheduledAtUtc);
    ctx.forecast = event.forecast;
    ctx.previous = event.previous;
    ctx.timeframeContext = context_.timeframeContext;
    if (context_.candleSummary) ctx.candleSummary = context_.candleSummary();
    if (context_.astraSummary) ctx.astraSummary = context_.astraSummary();
    return ctx;
}

void EventWatcher::prepareEvent(const ScheduledEvent& event,
                                std::int64_t nowUtc, TickResult& out) {
    const auto started = std::chrono::steady_clock::now();
    NewsAnalysis analysis =
        service_->analyzeEvent(event.eventKey, EventTurn::Expectation,
                               buildContext(event), nowUtc);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());

    std::string error;
    if (!store_->saveEventAnalysis(event.eventKey, kExpectation, analysis,
                                   error)) {
        // A storage failure means the expectation is not durable; record it and
        // do not mark the event prepared, so the next tick retries.
        std::cerr << "  WARN: cannot persist expectation: " << error << std::endl;
        record("event_prepare", "error", error,
               "event=" + event.eventKey, ms);
        return;
    }
    store_->markPrepared(event.eventKey, analysis.status, nowUtc, error);
    if (analysis.status == "ok") ++out.prepared;

    std::string outcome = "ok";
    if (analysis.status == "unavailable") outcome = "unavailable";
    else if (analysis.status == "insufficient_data") outcome = "empty";
    else if (analysis.status == "invalid_model_output") outcome = "rejected";
    record("event_prepare", outcome, analysis.statusReason,
           "event=" + event.title + " country=" + event.country +
               " status=" + analysis.status +
               " direction=" + analysis.proposedDirection +
               " model=" + analysis.modelId,
           ms);
}

void EventWatcher::releaseEvent(const ScheduledEvent& event,
                                std::int64_t nowUtc, TickResult& out) {
    const auto started = std::chrono::steady_clock::now();
    NewsAnalysis analysis = service_->analyzeEvent(
        event.eventKey, EventTurn::Result, buildContext(event), nowUtc);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());

    std::string error;
    if (!store_->saveEventAnalysis(event.eventKey, kResult, analysis, error)) {
        std::cerr << "  WARN: cannot persist result: " << error << std::endl;
        record("event_release", "error", error, "event=" + event.eventKey, ms);
        return;
    }
    // The release is marked even on a failed turn: the scheduled time has
    // passed, so retrying this same instant forever would spam the provider.
    // The failure is visible in release_status and the activity log.
    store_->markReleased(event.eventKey, analysis.status, nowUtc, error);
    if (analysis.status == "ok") ++out.released;

    std::string outcome = "ok";
    if (analysis.status == "unavailable") outcome = "unavailable";
    else if (analysis.status == "insufficient_data") outcome = "empty";
    else if (analysis.status == "invalid_model_output") outcome = "rejected";
    record("event_release", outcome, analysis.statusReason,
           "event=" + event.title + " country=" + event.country +
               " status=" + analysis.status +
               " direction=" + analysis.proposedDirection +
               " model=" + analysis.modelId,
           ms);
}

void EventWatcher::record(const std::string& kind, const std::string& outcome,
                          const std::string& reason, const std::string& detail,
                          int durationMs) {
    if (activityLog_ == nullptr) return;
    ActivityEntry entry;
    entry.atUtc = nowSeconds();
    entry.kind = kind;
    entry.outcome = outcome;
    entry.reason = reason;
    entry.source = "watcher";
    entry.detail = detail;
    entry.itemCount = 1;
    entry.durationMs = durationMs;
    std::string error;
    activityLog_->record(entry, error);  // best-effort: never fails the tick
}

}  // namespace aura
