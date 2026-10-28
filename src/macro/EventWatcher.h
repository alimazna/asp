#pragma once
// Event watcher: the automatic half of the macro pipeline.
//
// Runs on its own thread and does two things on a fixed tick:
//   1. PREPARE - for each gold-relevant high/medium-impact event approaching its
//      release, ask the model for the expectation (what the market is positioned
//      for and what would confirm or contradict it) and persist it.
//   2. RELEASE - for each event whose scheduled time has passed, ask the model
//      for the result turn and persist it.
//
// Both turns run through NewsAnalysisService, so they share its quota handling,
// bounded retry and schema repair. The watcher never touches ASTRA: it only
// reads the latest analysis to supply context, exactly as the article path does.
//
// Failure policy: a failed turn is recorded with its classified status and the
// loop continues. One bad event never stops the watcher, and a spent quota
// simply parks the work until the monitor's cooldown expires.

#include "activity/ActivityLog.h"
#include "macro/CalendarFeed.h"
#include "macro/EventStore.h"
#include "news/NewsAnalysisService.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace aura {

// Supplies the real market context for an event turn. Implemented by the server
// so the watcher stays free of store/DB knowledge and stays unit-testable.
struct EventContextProvider {
    // Compact closed-candle summary (already quarantined by the store).
    std::function<std::string()> candleSummary;
    // Existing ASTRA analysis, verbatim. Empty means unavailable.
    std::function<std::string()> astraSummary;
    // Newest closed candle open time, for the data-version stamp.
    std::function<std::int64_t()> candleAsOfUtc;
    std::string timeframeContext = "M15,H4";
};

class EventWatcher {
public:
    EventWatcher(EventStore* store, ICalendarSource* calendar,
                 NewsAnalysisService* service, EventContextProvider context,
                 ActivityLog* activityLog);
    ~EventWatcher();

    EventWatcher(const EventWatcher&) = delete;
    EventWatcher& operator=(const EventWatcher&) = delete;

    // Poll interval for the calendar, and the look-ahead for preparing an
    // expectation. Both are operator-tunable through the environment.
    struct Options {
        int tickSeconds = 60;
        std::int64_t prepareHorizonSeconds = 3 * 3600;  // 3 hours ahead
        int maxPreparePerTick = 2;
        int maxReleasePerTick = 2;
        // Upper bound for the failure backoff. 15 minutes keeps the system
        // responsive to a recovered feed while never hammering a failing one.
        int maxBackoffSeconds = 900;
    };

    void start();
    void stop();

    // Applies operator tunables (tick interval, prepare horizon, per-tick
    // caps). Must be called before start(); the loop reads them each tick.
    void setOptions(const Options& options) { options_ = options; }

    // Runs one tick synchronously. Exposed so tests drive the pipeline
    // deterministically instead of sleeping on a real clock.
    struct TickResult {
        int calendarFetched = 0;
        int eventsInserted = 0;
        int eventsUpdated = 0;
        int prepared = 0;
        int released = 0;
        std::string calendarError;
    };
    TickResult runOnce(std::int64_t nowUtc);

private:
    void loop();
    void prepareEvent(const ScheduledEvent& event, std::int64_t nowUtc,
                      TickResult& out);
    void releaseEvent(const ScheduledEvent& event, std::int64_t nowUtc,
                      TickResult& out);
    EventPromptContext buildContext(const ScheduledEvent& event) const;
    void record(const std::string& kind, const std::string& outcome,
                const std::string& reason, const std::string& detail,
                int durationMs);

    EventStore* store_;
    ICalendarSource* calendar_;
    NewsAnalysisService* service_;
    EventContextProvider context_;
    ActivityLog* activityLog_;
    Options options_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace aura
