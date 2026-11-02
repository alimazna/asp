#pragma once
// Persistence for scheduled macro events and the analyses derived from them.
//
// Two distinct records are kept, because they answer different questions and
// must never be conflated:
//   * the EXPECTATION written before release (what the market expects and what
//     the model makes of it), and
//   * the RESULT written after release, which reads the outcome.
// Both are stored per event key, so a restart preserves what the system said at
// the time rather than overwriting it.

#include "macro/CalendarFeed.h"
#include "news/NewsModels.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace aura {

// Stage of an event's life. The watcher advances these; the store only records.
enum class EventStage {
    Scheduled,   // known, not yet prepared
    Prepared,    // expectation written, waiting for release
    Released,    // the scheduled time has passed
};

const char* eventStageName(EventStage stage);

struct ScheduledEvent {
    std::string eventKey;
    std::string title;
    std::string country;
    std::string impact;
    std::int64_t scheduledAtUtc = 0;
    std::string forecast;    // published consensus; empty when the feed had none
    std::string previous;
    std::string source;
    std::string stage = "scheduled";
    std::int64_t firstSeenUtc = 0;
    std::int64_t updatedAtUtc = 0;
    // Set once the expectation turn has run (successfully or not).
    std::int64_t preparedAtUtc = 0;
    std::string prepareStatus;   // ok | unavailable | invalid_model_output | ...
    // Set once the release turn has run.
    std::int64_t releasedAtUtc = 0;
    std::string releaseStatus;
    bool goldRelevant = false;
};

class EventStore {
public:
    EventStore() = default;
    ~EventStore();

    EventStore(const EventStore&) = delete;
    EventStore& operator=(const EventStore&) = delete;

    bool open(const std::string& path, std::string& error);
    bool isOpen() const noexcept { return db_ != nullptr; }

    // Upsert a batch of freshly fetched events. An event already known keeps its
    // stage and any analysis it has; only the feed-owned columns refresh. This
    // is what makes a repeated poll safe.
    bool upsertEvents(const std::vector<CalendarEvent>& events,
                      std::int64_t nowUtc, int& inserted, int& updated,
                      std::string& error);

    // Events whose scheduled time has arrived but which have not been released
    // yet -- the watcher's release work list.
    std::vector<ScheduledEvent> dueForRelease(std::int64_t nowUtc,
                                              int limit) const;

    // Gold-relevant events approaching release that still need an expectation.
    std::vector<ScheduledEvent> dueForPrepare(std::int64_t nowUtc,
                                              std::int64_t horizonSeconds,
                                              int limit) const;

    // Most recent events, soonest-first for upcoming, newest-first when
    // `includePast`. Gold-relevant only when `goldOnly`.
    std::vector<ScheduledEvent> recent(int limit, bool includePast,
                                       bool goldOnly) const;

    bool getEvent(const std::string& eventKey, ScheduledEvent& out) const;

    // Persist one analysis turn against an event. `kind` is 'expectation' or
    // 'result'. Upsert by (event_key, kind) so re-running a turn replaces its
    // own row rather than accumulating duplicates.
    bool saveEventAnalysis(const std::string& eventKey, const std::string& kind,
                           const NewsAnalysis& analysis, std::string& error);

    // Reads back a stored turn. Returns false when none exists.
    bool getEventAnalysis(const std::string& eventKey, const std::string& kind,
                          NewsAnalysis& out) const;

    // Stage transitions. Each is idempotent and stamps its own timestamp.
    bool markPrepared(const std::string& eventKey, const std::string& status,
                      std::int64_t nowUtc, std::string& error);
    bool markReleased(const std::string& eventKey, const std::string& status,
                      std::int64_t nowUtc, std::string& error);

    // True when `table` already has `column`; makes migrations idempotent.
    bool hasColumn(const std::string& table, const std::string& column) const;

private:
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
};

}  // namespace aura
