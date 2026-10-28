// Event store implementation: SQLite persistence for scheduled macro events
// and the expectation/result analyses derived from them.

#include "macro/EventStore.h"

#include "api/BackendApiSchema.h"
#include "foundation/Json.h"

#include <sqlite3.h>

#include <cstdlib>
#include <vector>

namespace aura {
namespace {

// Two tables. `scheduled_events` is the feed-owned record; `event_analyses`
// holds the two turns per event, keyed by (event_key, kind) so re-running a
// turn replaces its own row instead of accumulating history the UI would have
// to disambiguate.
constexpr char kSchema[] =
    "CREATE TABLE IF NOT EXISTS scheduled_events ("
    "  event_key       TEXT NOT NULL PRIMARY KEY,"
    "  title           TEXT NOT NULL,"
    "  country         TEXT NOT NULL DEFAULT '',"
    "  impact          TEXT NOT NULL DEFAULT 'unknown',"
    "  scheduled_at    INTEGER NOT NULL DEFAULT 0,"
    "  forecast        TEXT NOT NULL DEFAULT '',"
    "  previous        TEXT NOT NULL DEFAULT '',"
    "  source          TEXT NOT NULL DEFAULT '',"
    "  stage           TEXT NOT NULL DEFAULT 'scheduled',"
    "  gold_relevant   INTEGER NOT NULL DEFAULT 0,"
    "  first_seen      INTEGER NOT NULL DEFAULT 0,"
    "  updated_at      INTEGER NOT NULL DEFAULT 0,"
    "  prepared_at     INTEGER NOT NULL DEFAULT 0,"
    "  prepare_status  TEXT NOT NULL DEFAULT '',"
    "  released_at     INTEGER NOT NULL DEFAULT 0,"
    "  release_status  TEXT NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_events_sched ON scheduled_events(scheduled_at);"
    "CREATE TABLE IF NOT EXISTS event_analyses ("
    "  event_key       TEXT NOT NULL,"
    "  kind            TEXT NOT NULL,"
    "  status          TEXT NOT NULL DEFAULT 'unavailable',"
    "  status_reason   TEXT NOT NULL DEFAULT '',"
    "  summary_ar      TEXT NOT NULL DEFAULT '',"
    "  facts_json      TEXT NOT NULL DEFAULT '[]',"
    "  implications_ar TEXT NOT NULL DEFAULT '[]',"
    "  supporting_ar   TEXT NOT NULL DEFAULT '[]',"
    "  opposing_ar     TEXT NOT NULL DEFAULT '[]',"
    "  gold_impact_ar  TEXT NOT NULL DEFAULT '',"
    "  gold_relevance  TEXT NOT NULL DEFAULT 'unknown',"
    "  direction       TEXT NOT NULL DEFAULT 'no_trade',"
    "  has_entry       INTEGER NOT NULL DEFAULT 0,"
    "  entry           REAL NOT NULL DEFAULT 0,"
    "  has_stop        INTEGER NOT NULL DEFAULT 0,"
    "  stop_loss       REAL NOT NULL DEFAULT 0,"
    "  has_target      INTEGER NOT NULL DEFAULT 0,"
    "  take_profit     REAL NOT NULL DEFAULT 0,"
    "  invalidating_ar TEXT NOT NULL DEFAULT '',"
    "  astra_agreement TEXT NOT NULL DEFAULT 'unknown',"
    "  disagreement_ar TEXT NOT NULL DEFAULT '',"
    "  limitations_ar  TEXT NOT NULL DEFAULT '[]',"
    "  provider_id     TEXT NOT NULL DEFAULT '',"
    "  model_id        TEXT NOT NULL DEFAULT '',"
    "  analyzed_at     INTEGER NOT NULL DEFAULT 0,"
    "  timeframe_ctx   TEXT NOT NULL DEFAULT '',"
    "  candle_as_of    INTEGER NOT NULL DEFAULT 0,"
    "  astra_signal    TEXT NOT NULL DEFAULT '',"
    "  PRIMARY KEY (event_key, kind)"
    ");";

std::string encodeStringArray(const std::vector<std::string>& items) {
    std::vector<std::string> encoded;
    encoded.reserve(items.size());
    for (const auto& s : items) encoded.push_back(jsonString(s));
    return jsonArray(encoded);
}

std::string encodeFacts(const std::vector<NewsFact>& facts) {
    std::vector<std::string> encoded;
    encoded.reserve(facts.size());
    for (const auto& f : facts) {
        encoded.push_back(jsonObject(
            {ApiField{"text", jsonString(f.text), true},
             ApiField{"kind", jsonString(f.kind), true}}));
    }
    return jsonArray(encoded);
}

std::vector<std::string> decodeStringArray(const char* raw) {
    std::vector<std::string> out;
    if (raw == nullptr) return out;
    JsonValue value;
    std::string err;
    if (!JsonValue::parse(raw, value, err) || !value.isArray()) return out;
    for (const auto& item : value.items()) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out;
}

std::vector<NewsFact> decodeFacts(const char* raw) {
    std::vector<NewsFact> out;
    if (raw == nullptr) return out;
    JsonValue value;
    std::string err;
    if (!JsonValue::parse(raw, value, err) || !value.isArray()) return out;
    for (const auto& item : value.items()) {
        if (!item.isObject()) continue;
        NewsFact f;
        f.text = item["text"].asString();
        f.kind = item["kind"].asString();
        out.push_back(std::move(f));
    }
    return out;
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    const unsigned char* p = sqlite3_column_text(stmt, index);
    return p == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(p));
}

ScheduledEvent readEvent(sqlite3_stmt* stmt) {
    ScheduledEvent e;
    e.eventKey = columnText(stmt, 0);
    e.title = columnText(stmt, 1);
    e.country = columnText(stmt, 2);
    e.impact = columnText(stmt, 3);
    e.scheduledAtUtc = sqlite3_column_int64(stmt, 4);
    e.forecast = columnText(stmt, 5);
    e.previous = columnText(stmt, 6);
    e.source = columnText(stmt, 7);
    e.stage = columnText(stmt, 8);
    e.goldRelevant = sqlite3_column_int(stmt, 9) != 0;
    e.firstSeenUtc = sqlite3_column_int64(stmt, 10);
    e.updatedAtUtc = sqlite3_column_int64(stmt, 11);
    e.preparedAtUtc = sqlite3_column_int64(stmt, 12);
    e.prepareStatus = columnText(stmt, 13);
    e.releasedAtUtc = sqlite3_column_int64(stmt, 14);
    e.releaseStatus = columnText(stmt, 15);
    return e;
}

constexpr char kEventColumns[] =
    "event_key, title, country, impact, scheduled_at, forecast, previous,"
    " source, stage, gold_relevant, first_seen, updated_at, prepared_at,"
    " prepare_status, released_at, release_status";

}  // namespace

const char* eventStageName(EventStage stage) {
    switch (stage) {
        case EventStage::Scheduled: return "scheduled";
        case EventStage::Prepared: return "prepared";
        case EventStage::Released: return "released";
    }
    return "scheduled";
}

EventStore::~EventStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

bool EventStore::hasColumn(const std::string& table,
                           const std::string& column) const {
    if (db_ == nullptr) return false;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "PRAGMA table_info(" + table + ");";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name != nullptr && column == reinterpret_cast<const char*>(name)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool EventStore::open(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ != nullptr) {
        error = "EventStore already open";
        return false;
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = std::string("cannot open event store: ") +
                (db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory");
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        error = std::string("cannot prepare event store: ") +
                (errMsg != nullptr ? errMsg : "unknown");
        if (errMsg != nullptr) sqlite3_free(errMsg);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    char* walMsg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &walMsg);
    if (walMsg != nullptr) sqlite3_free(walMsg);
    return true;
}

bool EventStore::upsertEvents(const std::vector<CalendarEvent>& events,
                              std::int64_t nowUtc, int& inserted, int& updated,
                              std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    inserted = 0;
    updated = 0;
    if (db_ == nullptr) {
        error = "event store is not open";
        return false;
    }
    // Feed columns refresh on every poll; lifecycle columns (stage, prepared_*,
    // released_*, first_seen) are deliberately NOT touched, so a repeated poll
    // cannot erase an analysis the watcher already produced.
    const char* sql =
        "INSERT INTO scheduled_events"
        " (event_key, title, country, impact, scheduled_at, forecast, previous,"
        "  source, gold_relevant, first_seen, updated_at)"
        " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)"
        " ON CONFLICT(event_key) DO UPDATE SET"
        "  title=excluded.title, impact=excluded.impact,"
        "  scheduled_at=excluded.scheduled_at, forecast=excluded.forecast,"
        "  previous=excluded.previous, gold_relevant=excluded.gold_relevant,"
        "  updated_at=excluded.updated_at;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("prepare event upsert failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    // `sqlite3_changes` reports 1 for both branches of ON CONFLICT DO UPDATE, so
    // it cannot tell a fresh insert from a refresh. A cheap existence probe per
    // event gives the watcher an honest new/updated split for its logging.
    sqlite3_stmt* exists = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT 1 FROM scheduled_events WHERE event_key = ?1;",
                           -1, &exists, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        error = std::string("prepare event exists probe failed: ") +
                sqlite3_errmsg(db_);
        return false;
    }
    bool ok = true;
    for (const auto& e : events) {
        const int relevant = isGoldRelevantCurrency(e.country) ? 1 : 0;
        sqlite3_bind_text(exists, 1, e.eventKey.c_str(), -1, SQLITE_TRANSIENT);
        const bool present = sqlite3_step(exists) == SQLITE_ROW;
        sqlite3_reset(exists);
        sqlite3_clear_bindings(exists);

        sqlite3_bind_text(stmt, 1, e.eventKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, e.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, e.country.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, e.impact.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, e.scheduledAtUtc);
        sqlite3_bind_text(stmt, 6, e.forecast.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, e.previous.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, e.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, relevant);
        sqlite3_bind_int64(stmt, 10, nowUtc);
        sqlite3_bind_int64(stmt, 11, nowUtc);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_DONE) {
            error = std::string("event upsert step failed: ") + sqlite3_errmsg(db_);
            ok = false;
            break;
        }
        if (present) ++updated; else ++inserted;
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(exists);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<ScheduledEvent> EventStore::dueForRelease(std::int64_t nowUtc,
                                                      int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduledEvent> out;
    if (db_ == nullptr) return out;
    const std::string sql =
        std::string("SELECT ") + kEventColumns +
        " FROM scheduled_events WHERE stage != 'released'"
        " AND scheduled_at <= ?1 ORDER BY scheduled_at ASC LIMIT ?2;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(stmt, 1, nowUtc);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readEvent(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::vector<ScheduledEvent> EventStore::dueForPrepare(
    std::int64_t nowUtc, std::int64_t horizonSeconds, int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduledEvent> out;
    if (db_ == nullptr) return out;
    // Only gold-relevant, high/medium impact events that are still ahead of
    // their release time and have not been prepared. Preparing a holiday or a
    // low-impact note would spend quota on something that cannot move gold.
    const std::string sql =
        std::string("SELECT ") + kEventColumns +
        " FROM scheduled_events WHERE stage = 'scheduled'"
        " AND gold_relevant = 1 AND impact IN ('high','medium')"
        " AND scheduled_at > ?1 AND scheduled_at <= ?2"
        " ORDER BY scheduled_at ASC LIMIT ?3;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(stmt, 1, nowUtc);
    sqlite3_bind_int64(stmt, 2, nowUtc + horizonSeconds);
    sqlite3_bind_int(stmt, 3, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readEvent(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::vector<ScheduledEvent> EventStore::recent(int limit, bool includePast,
                                               bool goldOnly) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduledEvent> out;
    if (db_ == nullptr) return out;
    std::string sql = std::string("SELECT ") + kEventColumns +
                      " FROM scheduled_events WHERE 1=1";
    if (!includePast) sql += " AND stage != 'released'";
    if (goldOnly) sql += " AND gold_relevant = 1";
    sql += " ORDER BY scheduled_at DESC LIMIT ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readEvent(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool EventStore::getEvent(const std::string& eventKey,
                          ScheduledEvent& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return false;
    const std::string sql = std::string("SELECT ") + kEventColumns +
                            " FROM scheduled_events WHERE event_key = ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) out = readEvent(stmt);
    sqlite3_finalize(stmt);
    return found;
}

bool EventStore::saveEventAnalysis(const std::string& eventKey,
                                   const std::string& kind,
                                   const NewsAnalysis& a, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        error = "event store is not open";
        return false;
    }
    if (kind != "expectation" && kind != "result") {
        error = "event analysis kind must be 'expectation' or 'result'";
        return false;
    }
    const char* sql =
        "INSERT INTO event_analyses"
        " (event_key, kind, status, status_reason, summary_ar, facts_json,"
        "  implications_ar, supporting_ar, opposing_ar, gold_impact_ar,"
        "  gold_relevance, direction, has_entry, entry, has_stop, stop_loss,"
        "  has_target, take_profit, invalidating_ar, astra_agreement,"
        "  disagreement_ar, limitations_ar, provider_id, model_id, analyzed_at,"
        "  timeframe_ctx, candle_as_of, astra_signal)"
        " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,"
        "         ?18,?19,?20,?21,?22,?23,?24,?25,?26,?27,?28)"
        " ON CONFLICT(event_key, kind) DO UPDATE SET"
        "  status=excluded.status, status_reason=excluded.status_reason,"
        "  summary_ar=excluded.summary_ar, facts_json=excluded.facts_json,"
        "  implications_ar=excluded.implications_ar,"
        "  supporting_ar=excluded.supporting_ar, opposing_ar=excluded.opposing_ar,"
        "  gold_impact_ar=excluded.gold_impact_ar,"
        "  gold_relevance=excluded.gold_relevance, direction=excluded.direction,"
        "  has_entry=excluded.has_entry, entry=excluded.entry,"
        "  has_stop=excluded.has_stop, stop_loss=excluded.stop_loss,"
        "  has_target=excluded.has_target, take_profit=excluded.take_profit,"
        "  invalidating_ar=excluded.invalidating_ar,"
        "  astra_agreement=excluded.astra_agreement,"
        "  disagreement_ar=excluded.disagreement_ar,"
        "  limitations_ar=excluded.limitations_ar,"
        "  provider_id=excluded.provider_id, model_id=excluded.model_id,"
        "  analyzed_at=excluded.analyzed_at,"
        "  timeframe_ctx=excluded.timeframe_ctx,"
        "  candle_as_of=excluded.candle_as_of, astra_signal=excluded.astra_signal;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("prepare event analysis failed: ") +
                sqlite3_errmsg(db_);
        return false;
    }
    const std::string facts = encodeFacts(a.facts);
    const std::string impl = encodeStringArray(a.marketImplicationsAr);
    const std::string supp = encodeStringArray(a.supportingEvidenceAr);
    const std::string opp = encodeStringArray(a.opposingEvidenceAr);
    const std::string lim = encodeStringArray(a.limitationsAr);

    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, a.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, a.statusReason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, a.summaryAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, facts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, impl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, supp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, opp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, a.goldImpactAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, a.goldRelevance.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, a.proposedDirection.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, a.hasEntry ? 1 : 0);
    sqlite3_bind_double(stmt, 14, a.entry);
    sqlite3_bind_int(stmt, 15, a.hasStop ? 1 : 0);
    sqlite3_bind_double(stmt, 16, a.stopLoss);
    sqlite3_bind_int(stmt, 17, a.hasTarget ? 1 : 0);
    sqlite3_bind_double(stmt, 18, a.takeProfit);
    sqlite3_bind_text(stmt, 19, a.invalidatingConditionsAr.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, a.astraAgreement.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 21, a.disagreementNoteAr.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 22, lim.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 23, a.providerId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 24, a.modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 25, a.analyzedAtUtc);
    sqlite3_bind_text(stmt, 26, a.timeframeContext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 27, a.candleAsOfUtc);
    sqlite3_bind_text(stmt, 28, a.astraSignalRef.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) error = std::string("event analysis step failed: ") + sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::getEventAnalysis(const std::string& eventKey,
                                  const std::string& kind,
                                  NewsAnalysis& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return false;
    const char* sql =
        "SELECT event_key, status, status_reason, summary_ar, facts_json,"
        " implications_ar, supporting_ar, opposing_ar, gold_impact_ar,"
        " gold_relevance, direction, has_entry, entry, has_stop, stop_loss,"
        " has_target, take_profit, invalidating_ar, astra_agreement,"
        " disagreement_ar, limitations_ar, provider_id, model_id, analyzed_at,"
        " timeframe_ctx, candle_as_of, astra_signal"
        " FROM event_analyses WHERE event_key = ?1 AND kind = ?2;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }
    out = NewsAnalysis{};
    out.articleId = columnText(stmt, 0);
    out.status = columnText(stmt, 1);
    out.statusReason = columnText(stmt, 2);
    out.summaryAr = columnText(stmt, 3);
    out.facts = decodeFacts(columnText(stmt, 4).c_str());
    out.marketImplicationsAr = decodeStringArray(columnText(stmt, 5).c_str());
    out.supportingEvidenceAr = decodeStringArray(columnText(stmt, 6).c_str());
    out.opposingEvidenceAr = decodeStringArray(columnText(stmt, 7).c_str());
    out.goldImpactAr = columnText(stmt, 8);
    out.goldRelevance = columnText(stmt, 9);
    out.proposedDirection = columnText(stmt, 10);
    out.hasEntry = sqlite3_column_int(stmt, 11) != 0;
    out.entry = sqlite3_column_double(stmt, 12);
    out.hasStop = sqlite3_column_int(stmt, 13) != 0;
    out.stopLoss = sqlite3_column_double(stmt, 14);
    out.hasTarget = sqlite3_column_int(stmt, 15) != 0;
    out.takeProfit = sqlite3_column_double(stmt, 16);
    out.invalidatingConditionsAr = columnText(stmt, 17);
    out.astraAgreement = columnText(stmt, 18);
    out.disagreementNoteAr = columnText(stmt, 19);
    out.limitationsAr = decodeStringArray(columnText(stmt, 20).c_str());
    out.providerId = columnText(stmt, 21);
    out.modelId = columnText(stmt, 22);
    out.analyzedAtUtc = sqlite3_column_int64(stmt, 23);
    out.timeframeContext = columnText(stmt, 24);
    out.candleAsOfUtc = sqlite3_column_int64(stmt, 25);
    out.astraSignalRef = columnText(stmt, 26);
    sqlite3_finalize(stmt);
    return true;
}

bool EventStore::markPrepared(const std::string& eventKey,
                              const std::string& status, std::int64_t nowUtc,
                              std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        error = "event store is not open";
        return false;
    }
    // A successful preparation moves the event to 'prepared'. A failed one stays
    // 'scheduled' so the next tick retries it, while recording why -- the event
    // must not silently appear prepared when no expectation was produced.
    const bool okStatus = (status == "ok");
    const char* sql =
        "UPDATE scheduled_events SET prepared_at = ?2, prepare_status = ?3,"
        " stage = CASE WHEN ?4 = 1 THEN 'prepared' ELSE stage END"
        " WHERE event_key = ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("prepare mark failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, nowUtc);
    sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, okStatus ? 1 : 0);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) error = std::string("prepare mark step failed: ") + sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::markReleased(const std::string& eventKey,
                              const std::string& status, std::int64_t nowUtc,
                              std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        error = "event store is not open";
        return false;
    }
    // Unlike prepare, a failed release still marks the event released: the
    // scheduled time has passed and retrying the same instant forever would
    // both spam the provider and hide the failure. The reason is recorded.
    const char* sql =
        "UPDATE scheduled_events SET released_at = ?2, release_status = ?3,"
        " stage = 'released' WHERE event_key = ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("release mark failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, nowUtc);
    sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) error = std::string("release mark step failed: ") + sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

}  // namespace aura
