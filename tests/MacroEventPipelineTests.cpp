// TST-MACRO-0001 - Scheduled macro-event pipeline.
//
// Scope: src/macro/{CalendarFeed,EventStore,EventWatcher}.* plus the event turn
// added to NewsAnalysisService. This is the automatic news/event half of AYZR:
// the calendar is polled, an expectation is written before release, and the
// result turn fires when the scheduled time passes.
//
// Evidence here:
//   1. Calendar parsing against the real feed shape, including the UTC-offset
//      conversion and the "consensus absent is absent, not zero" contract.
//   2. Store behaviour: idempotent upsert that preserves lifecycle columns, and
//      separate expectation/result rows.
//   3. Watcher behaviour driven by a scripted calendar + the real service with a
//      MockLlmProvider, so the prepare and release turns execute end to end
//      without a network or external quota.
//   4. Honesty: a failed turn is recorded as failed and never presented as ok.

#include "TestHarness.h"

#include "activity/ActivityLog.h"
#include "foundation/OutboundHttpClient.h"
#include "macro/CalendarFeed.h"
#include "macro/EventStore.h"
#include "macro/EventWatcher.h"
#include "news/MockLlmProvider.h"
#include "news/NewsAnalysisService.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace aura {
namespace test {
namespace {


// A scripted calendar: returns whatever the test queued, or an error.
class ScriptedCalendar : public ICalendarSource {
public:
    std::string sourceLabel() const override { return "scripted"; }
    void queue(std::vector<CalendarEvent> events) {
        events_ = std::move(events);
        fail_ = false;
    }
    void failWith(const std::string& error) {
        error_ = error;
        fail_ = true;
    }
    bool fetch(std::vector<CalendarEvent>& events, std::string& error) override {
        if (fail_) {
            error = error_;
            return false;
        }
        events = events_;
        return true;
    }

private:
    std::vector<CalendarEvent> events_;
    std::string error_;
    bool fail_ = false;
};

// A temp sqlite path that is removed on destruction, so a test run leaves no
// state behind for the next one.
struct TempDb {
    std::string path;
    explicit TempDb(const std::string& tag) {
        path = "/tmp/ayzr-test-" + tag + "-" +
               std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".sqlite";
        std::remove(path.c_str());
    }
    ~TempDb() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
};

CalendarEvent makeEvent(const std::string& title, const std::string& country,
                        std::int64_t atUtc, const std::string& impact,
                        const std::string& forecast) {
    CalendarEvent e;
    e.title = title;
    e.country = country;
    e.impact = impact;
    e.scheduledAtUtc = atUtc;
    e.forecast = forecast;
    e.previous = "1.0";
    e.source = "scripted";
    e.eventKey = makeEventKey("scripted", country, title, atUtc);
    return e;
}

// --- Calendar parsing -------------------------------------------------------

TEST_CASE(parses_feed_shape) {
    // Two events: one with a -04:00 offset (the real feed's shape) and one Z.
    const std::string body = R"([
      {"title":"CPI m\/m","country":"USD","date":"2026-10-27T08:30:00-04:00",
       "impact":"High","forecast":"0.3%","previous":"0.2%"},
      {"title":"GDP q\/q","country":"GBP","date":"2026-10-28T07:00:00Z",
       "impact":"Medium","forecast":"","previous":"0.5%"}
    ])";
    std::vector<CalendarEvent> events;
    std::string error;
    CHECK(parseCalendarJson(body, "faireconomy", events, error));
    CHECK(events.size() == 2);

    // 08:30 at -04:00 is 12:30 UTC. A naive parse would store 08:30 and the
    // release turn would fire four hours early.
    CHECK(events[0].scheduledAtUtc == 1793104200);  // 2026-10-27T12:30:00Z
    CHECK(events[0].impact == "high");
    CHECK(events[0].forecast == "0.3%");
    CHECK(events[0].country == "USD");

    // The second event publishes no consensus. It must read back empty, not "0".
    CHECK(events[1].forecast.empty());
    CHECK(events[1].impact == "medium");
    CHECK(events[1].scheduledAtUtc == 1793170800);  // 2026-10-28T07:00:00Z
}

TEST_CASE(drops_unparseable_date) {
    // A malformed date must drop that one event rather than mis-time it.
    const std::string body = R"([
      {"title":"Bad","country":"USD","date":"not-a-date","impact":"High"},
      {"title":"Good","country":"USD","date":"2026-10-27T12:30:00Z",
       "impact":"High","forecast":"1"}
    ])";
    std::vector<CalendarEvent> events;
    std::string error;
    CHECK(parseCalendarJson(body, "faireconomy", events, error));
    CHECK(events.size() == 1);
    CHECK(events[0].title == "Good");
}

TEST_CASE(rejects_non_array) {
    std::vector<CalendarEvent> events;
    std::string error;
    CHECK(!parseCalendarJson("{\"oops\":true}", "x", events, error));
    CHECK(!error.empty());
}

TEST_CASE(gold_relevance_routing) {
    CHECK(isGoldRelevantCurrency("USD"));
    CHECK(isGoldRelevantCurrency("usd"));
    CHECK(isGoldRelevantCurrency("EUR"));
    CHECK(isGoldRelevantCurrency("JPY"));
    // A currency that does not move gold is not routed for analysis.
    CHECK(!isGoldRelevantCurrency("NZD"));
    CHECK(!isGoldRelevantCurrency("AUD"));
}

TEST_CASE(event_key_stable) {
    const std::string a = makeEventKey("ff", "USD", "CPI m/m", 1000);
    const std::string b = makeEventKey("ff", "usd", "CPI m/m", 1000);
    // Case-folding the currency keeps the key stable across feed casing changes.
    CHECK(a == b);
    const std::string c = makeEventKey("ff", "USD", "CPI m/m", 2000);
    CHECK(a != c);  // a rescheduled event is a different occurrence
}

// --- Store ------------------------------------------------------------------

TEST_CASE(upsert_is_idempotent_and_preserves_lifecycle) {
    TempDb db("store");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));

    std::vector<CalendarEvent> events = {makeEvent("CPI m/m", "USD", 1000, "high", "0.3%")};
    int inserted = 0, updated = 0;
    CHECK(store.upsertEvents(events, 100, inserted, updated, error));
    CHECK(inserted == 1);
    CHECK(updated == 0);

    // Mark it prepared, then poll again. The lifecycle must survive the poll.
    CHECK(store.markPrepared(events.begin()->eventKey, "ok", 150, error));
    CHECK(store.upsertEvents(events, 200, inserted, updated, error));
    CHECK(inserted == 0);
    CHECK(updated == 1);

    ScheduledEvent out;
    CHECK(store.getEvent(events.begin()->eventKey, out));
    CHECK(out.stage == "prepared");
    CHECK(out.preparedAtUtc == 150);
    CHECK(out.prepareStatus == "ok");
    CHECK(out.firstSeenUtc == 100);   // first sighting is preserved
    CHECK(out.updatedAtUtc == 200);   // last poll is recorded
}

TEST_CASE(prepare_failure_stays_scheduled) {
    TempDb db("prep-fail");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));
    std::vector<CalendarEvent> events = {makeEvent("NFP", "USD", 5000, "high", "150K")};
    int i = 0, u = 0;
    CHECK(store.upsertEvents(events, 100, i, u, error));

    // A failed preparation must NOT look prepared, or the watcher would skip it
    // forever and the event would never be analysed.
    CHECK(store.markPrepared(events[0].eventKey, "unavailable", 150, error));
    ScheduledEvent out;
    CHECK(store.getEvent(events[0].eventKey, out));
    CHECK(out.stage == "scheduled");
    CHECK(out.prepareStatus == "unavailable");
}

TEST_CASE(release_marks_even_on_failure) {
    TempDb db("rel-fail");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));
    std::vector<CalendarEvent> events = {makeEvent("FOMC", "USD", 900, "high", "")};
    int i = 0, u = 0;
    CHECK(store.upsertEvents(events, 100, i, u, error));

    // The scheduled time has passed; retrying the same instant forever would
    // spam the provider, so the event is marked released with the failure shown.
    CHECK(store.markReleased(events[0].eventKey, "unavailable", 1000, error));
    ScheduledEvent out;
    CHECK(store.getEvent(events[0].eventKey, out));
    CHECK(out.stage == "released");
    CHECK(out.releaseStatus == "unavailable");
}

TEST_CASE(due_queries_select_correct_work) {
    TempDb db("due");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));

    const std::int64_t now = 10'000;
    auto events = {
        // Past due, not released -> release work.
        makeEvent("Past USD", "USD", now - 100, "high", "1"),
        // Ahead, gold-relevant, high -> prepare work.
        makeEvent("Soon USD", "USD", now + 600, "high", "1"),
        // Ahead but low impact -> not prepared (cannot move gold enough).
        makeEvent("Soon low", "USD", now + 600, "low", "1"),
        // Ahead but non-gold currency -> not prepared.
        makeEvent("Soon NZD", "NZD", now + 600, "high", "1"),
        // Beyond the prepare horizon -> not yet.
        makeEvent("Far USD", "USD", now + 100000, "high", "1"),
    };
    int i = 0, u = 0;
    CHECK(store.upsertEvents(events, now, i, u, error));
    CHECK(i == 5);

    const auto release = store.dueForRelease(now, 10);
    CHECK(release.size() == 1);
    CHECK(release[0].title == "Past USD");

    const auto prepare = store.dueForPrepare(now, 3600, 10);
    CHECK(prepare.size() == 1);
    CHECK(prepare[0].title == "Soon USD");
}

TEST_CASE(separate_expectation_and_result_rows) {
    TempDb db("rows");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));
    const std::string key = "k1";

    NewsAnalysis expectation;
    expectation.status = "ok";
    expectation.summaryAr = "توقع التضخم 0.3%";
    expectation.proposedDirection = "no_trade";
    CHECK(store.saveEventAnalysis(key, "expectation", expectation, error));

    NewsAnalysis result;
    result.status = "ok";
    result.summaryAr = "صدر 0.4% أعلى من المتوقع";
    result.proposedDirection = "bearish";
    result.hasEntry = true;
    result.entry = 2650.0;
    CHECK(store.saveEventAnalysis(key, "result", result, error));

    NewsAnalysis back;
    CHECK(store.getEventAnalysis(key, "expectation", back));
    CHECK(back.summaryAr == "توقع التضخم 0.3%");
    CHECK(back.proposedDirection == "no_trade");

    CHECK(store.getEventAnalysis(key, "result", back));
    CHECK(back.summaryAr == "صدر 0.4% أعلى من المتوقع");
    CHECK(back.proposedDirection == "bearish");
    CHECK(back.hasEntry);
    CHECK(back.entry == 2650.0);

    // Re-running the same turn replaces its own row instead of accumulating a
    // second one the UI would have to disambiguate.
    expectation.summaryAr = "نسخة محدّثة";
    CHECK(store.saveEventAnalysis(key, "expectation", expectation, error));
    CHECK(store.getEventAnalysis(key, "expectation", back));
    CHECK(back.summaryAr == "نسخة محدّثة");
}

TEST_CASE(rejects_unknown_analysis_kind) {
    TempDb db("kind");
    EventStore store;
    std::string error;
    CHECK(store.open(db.path, error));
    NewsAnalysis a;
    // Only the two defined turns may be written; a typo must not create a third.
    CHECK(!store.saveEventAnalysis("k", "guess", a, error));
    CHECK(!error.empty());
}

// --- Watcher ----------------------------------------------------------------

struct WatcherFixture {
    TempDb db;
    EventStore store;
    ActivityLog log;
    ScriptedCalendar calendar;
    MockLlmProvider provider;
    NewsAnalysisService service;

    explicit WatcherFixture(const std::string& tag)
        : db(tag), service(&provider, 2, nullptr) {
        std::string error;
        store.open(db.path, error);
        log.open(db.path + ".activity", error);
    }

    std::unique_ptr<EventWatcher> makeWatcher() {
        EventContextProvider ctx;
        ctx.candleSummary = [] { return std::string("M15: 2650.0/2655.0/2648.0/2653.0"); };
        ctx.astraSummary = [] { return std::string("direction=bullish score=3"); };
        ctx.candleAsOfUtc = [] { return static_cast<std::int64_t>(1700000000); };
        return std::make_unique<EventWatcher>(&store, &calendar, &service,
                                              ctx, &log);
    }
};

TEST_CASE(watcher_prepares_then_releases) {
    WatcherFixture f("watch");
    const std::int64_t now = 1'000'000;
    f.calendar.queue({makeEvent("CPI m/m", "USD", now + 1200, "high", "0.3%")});

    auto watcher = f.makeWatcher();
    auto tick = watcher->runOnce(now);
    CHECK(tick.calendarFetched == 1);
    CHECK(tick.eventsInserted == 1);
    CHECK(tick.prepared == 1);   // expectation written ahead of release
    CHECK(tick.released == 0);   // not due yet

    ScheduledEvent ev;
    const std::string key = makeEventKey("scripted", "USD", "CPI m/m", now + 1200);
    CHECK(f.store.getEvent(key, ev));
    CHECK(ev.stage == "prepared");

    NewsAnalysis expectation;
    CHECK(f.store.getEventAnalysis(key, "expectation", expectation));
    CHECK(expectation.status == "ok");
    CHECK(!expectation.summaryAr.empty());
    // The result turn must not exist before release.
    NewsAnalysis result;
    CHECK(!f.store.getEventAnalysis(key, "result", result));

    // Time passes beyond the scheduled instant: the same watcher now releases.
    auto tick2 = watcher->runOnce(now + 1300);
    CHECK(tick2.prepared == 0);
    CHECK(tick2.released == 1);

    CHECK(f.store.getEvent(key, ev));
    CHECK(ev.stage == "released");
    CHECK(ev.releaseStatus == "ok");
    CHECK(f.store.getEventAnalysis(key, "result", result));
    CHECK(result.status == "ok");
}

TEST_CASE(watcher_is_idempotent_across_ticks) {
    WatcherFixture f("idem");
    const std::int64_t now = 2'000'000;
    f.calendar.queue({makeEvent("FOMC", "USD", now + 600, "high", "5.5%")});

    auto watcher = f.makeWatcher();
    CHECK(watcher->runOnce(now).prepared == 1);
    // A second tick before release must not re-prepare: the event is no longer
    // 'scheduled', so no quota is spent twice on the same expectation.
    auto second = watcher->runOnce(now + 10);
    CHECK(second.prepared == 0);
    CHECK(second.released == 0);

    // Polling the calendar again must not duplicate the event.
    CHECK(second.eventsInserted == 0);
    CHECK(second.eventsUpdated == 1);

    const auto events = f.store.recent(10, true, false);
    CHECK(events.size() == 1);
}

TEST_CASE(watcher_survives_calendar_outage) {
    WatcherFixture f("outage");
    const std::int64_t now = 3'000'000;
    // Seed one event while the feed is healthy.
    f.calendar.queue({makeEvent("GDP", "USD", now + 300, "high", "2.0%")});
    auto watcher = f.makeWatcher();
    CHECK(watcher->runOnce(now).eventsInserted == 1);

    // The feed dies. The stored event must survive and still release on time.
    f.calendar.failWith("transport: connection refused");
    auto tick = watcher->runOnce(now + 400);
    CHECK(!tick.calendarError.empty());
    CHECK(tick.calendarFetched == 0);
    CHECK(tick.released == 1);  // release still fired from stored knowledge
}

TEST_CASE(watcher_records_failure_honestly) {
    WatcherFixture f("fail-honest");
    // A provider that always fails: the turn must be recorded as unavailable,
    // never as ok.
    MockLlmProvider failing(MockLlmProvider::Mode::Timeout);
    NewsAnalysisService failingService(&failing, 1, nullptr);
    const std::int64_t now = 4'000'000;
    f.calendar.queue({makeEvent("CPI", "USD", now + 600, "high", "0.3%")});

    EventContextProvider ctx;
    EventWatcher watcher(&f.store, &f.calendar, &failingService, ctx, &f.log);
    auto tick = watcher.runOnce(now);
    CHECK(tick.prepared == 0);  // nothing ok

    ScheduledEvent ev;
    CHECK(f.store.getEvent(
        makeEventKey("scripted", "USD", "CPI", now + 600), ev));
    CHECK(ev.stage == "scheduled");           // stays for retry
    CHECK(ev.prepareStatus == "unavailable"); // and says why

    NewsAnalysis stored;
    CHECK(f.store.getEventAnalysis(
        makeEventKey("scripted", "USD", "CPI", now + 600), "expectation",
        stored));
    CHECK(stored.status == "unavailable");
    CHECK(!stored.statusReason.empty());

    // The activity log carries the real outcome, not a synthesised success.
    const auto entries = f.log.recent(10, "event_prepare");
    CHECK(!entries.empty());
    CHECK(entries[0].outcome == "unavailable");
}

TEST_CASE(event_turn_uses_real_context_only) {
    WatcherFixture f("ctx");
    const std::int64_t now = 5'000'000;
    f.calendar.queue({makeEvent("NFP", "USD", now + 600, "high", "")});

    auto watcher = f.makeWatcher();
    CHECK(watcher->runOnce(now).prepared == 1);

    // The prompt the model received must show the missing consensus as
    // UNAVAILABLE, and must carry the real candle/ASTRA context.
    const auto& messages = f.provider.lastMessages();
    CHECK(messages.size() == 2);
    const std::string& user = messages[1].content;
    CHECK(user.find("UNAVAILABLE") != std::string::npos);
    CHECK(user.find("2650.0") != std::string::npos);
    CHECK(user.find("EXISTING ASTRA ANALYSIS") != std::string::npos);
    CHECK(user.find("EXPECTATION") != std::string::npos);
}

}  // namespace


}  // namespace test
}  // namespace aura
