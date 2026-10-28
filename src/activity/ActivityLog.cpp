// Durable activity log implementation (SQLite).

#include "activity/ActivityLog.h"

#include <sqlite3.h>

namespace aura {
namespace {

constexpr char kSchema[] =
    "CREATE TABLE IF NOT EXISTS activity_log ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  at_utc     INTEGER NOT NULL DEFAULT 0,"
    "  kind       TEXT NOT NULL DEFAULT '',"
    "  outcome    TEXT NOT NULL DEFAULT '',"
    "  reason     TEXT NOT NULL DEFAULT '',"
    "  source     TEXT NOT NULL DEFAULT '',"
    "  detail     TEXT NOT NULL DEFAULT '',"
    "  item_count INTEGER NOT NULL DEFAULT 0,"
    "  duration_ms INTEGER NOT NULL DEFAULT -1"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_activity_at ON activity_log(at_utc);"
    "CREATE INDEX IF NOT EXISTS idx_activity_kind ON activity_log(kind);";

// The log is diagnostic, so a detail string is bounded rather than rejected:
// truncation keeps the row small without losing the fact that it happened.
std::string bounded(std::string s, std::size_t max = 400) {
    if (s.size() <= max) return s;
    return s.substr(0, max) + "...";
}

}  // namespace

ActivityLog::~ActivityLog() {
    if (db_ != nullptr) sqlite3_close(db_);
}

bool ActivityLog::open(const std::string& path, std::string& error) {
    if (db_ != nullptr) {
        error = "activity log already open";
        return false;
    }
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        error = db != nullptr ? sqlite3_errmsg(db) : "cannot open database";
        if (db != nullptr) sqlite3_close(db);
        return false;
    }
    // WAL keeps a reader (the API) from blocking on the writer (a request).
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);
    char* err = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        error = err != nullptr ? err : "cannot create activity schema";
        sqlite3_free(err);
        sqlite3_close(db);
        return false;
    }
    db_ = db;
    return true;
}

bool ActivityLog::record(const ActivityEntry& entry, std::string& error) {
    if (db_ == nullptr) {
        error = "activity log is not open";
        return false;
    }
    constexpr char kSql[] =
        "INSERT INTO activity_log"
        " (at_utc, kind, outcome, reason, source, detail, item_count,"
        "  duration_ms) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    const std::string detail = bounded(entry.detail);
    sqlite3_bind_int64(stmt, 1, entry.atUtc);
    sqlite3_bind_text(stmt, 2, entry.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, detail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, entry.itemCount);
    sqlite3_bind_int(stmt, 8, entry.durationMs);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::vector<ActivityEntry> ActivityLog::recent(int limit,
                                               const std::string& kind) const {
    std::vector<ActivityEntry> out;
    if (db_ == nullptr) return out;
    if (limit < 1 || limit > 1000) limit = 200;
    const bool filtered = !kind.empty();
    const char* sql = filtered
        ? "SELECT id, at_utc, kind, outcome, reason, source, detail,"
          " item_count, duration_ms FROM activity_log WHERE kind=?1"
          " ORDER BY id DESC LIMIT ?2;"
        : "SELECT id, at_utc, kind, outcome, reason, source, detail,"
          " item_count, duration_ms FROM activity_log ORDER BY id DESC"
          " LIMIT ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    if (filtered) {
        sqlite3_bind_text(stmt, 1, kind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
    } else {
        sqlite3_bind_int(stmt, 1, limit);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityEntry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.atUtc = sqlite3_column_int64(stmt, 1);
        auto text = [&](int i) {
            const unsigned char* t = sqlite3_column_text(stmt, i);
            return t != nullptr ? std::string(reinterpret_cast<const char*>(t))
                                : std::string();
        };
        e.kind = text(2);
        e.outcome = text(3);
        e.reason = text(4);
        e.source = text(5);
        e.detail = text(6);
        e.itemCount = sqlite3_column_int(stmt, 7);
        e.durationMs = sqlite3_column_int(stmt, 8);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return out;
}

ActivityLog::Tally ActivityLog::tally(std::int64_t sinceUtc) const {
    Tally t;
    if (db_ == nullptr) return t;
    constexpr char kSql[] =
        "SELECT outcome, COUNT(*) FROM activity_log WHERE at_utc >= ?1"
        " GROUP BY outcome;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return t;
    }
    sqlite3_bind_int64(stmt, 1, sinceUtc);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* o = sqlite3_column_text(stmt, 0);
        const std::string outcome =
            o != nullptr ? std::string(reinterpret_cast<const char*>(o)) : "";
        const std::int64_t n = sqlite3_column_int64(stmt, 1);
        if (outcome == "ok") t.ok += n;
        else if (outcome == "empty") t.empty += n;
        else if (outcome == "rejected") t.rejected += n;
        else if (outcome == "unavailable") t.unavailable += n;
        else t.error += n;
        t.total += n;
    }
    sqlite3_finalize(stmt);
    return t;
}

int ActivityLog::pruneBefore(std::int64_t cutoffUtc) {
    if (db_ == nullptr) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM activity_log WHERE at_utc < ?1;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, cutoffUtc);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;
    return sqlite3_changes(db_);
}

}  // namespace aura
