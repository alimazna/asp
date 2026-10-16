// Server-side idempotent candle store (SQLite).

#include "persistence/CandleStore.h"

#include "mt5/Mt5BridgeContract.h"

#include <sqlite3.h>

#include <mutex>

namespace aura {

namespace {
// A candle series is trustworthy only when none of its rows is misaligned to the
// timeframe grid. A fabricated row (for example one written before ingest
// enforced alignment) contaminates the whole series, so reads withhold the
// entire series rather than serving a plausible-looking sibling as real data.
// Timeframes without a fixed grid (D1/W1/MN1) and unknown labels are never
// judged. Expressed as a correlated NOT EXISTS predicate so LIMIT and MAX()
// operate on trustworthy rows only. `candles` is the outer table alias.
std::string misalignedExpr(const std::string& a) {
    return "(" + a + ".timeframe IN ('M1','M5','M15','M30','H1','H4') AND ("
           + a + ".open_time % (CASE " + a + ".timeframe"
           "   WHEN 'M1' THEN 60 WHEN 'M5' THEN 300 WHEN 'M15' THEN 900"
           "   WHEN 'M30' THEN 1800 WHEN 'H1' THEN 3600"
           "   WHEN 'H4' THEN 14400 END))<>0)";
}
// Series keyed by (symbol, timeframe) across all sources.
std::string cleanSeriesExpr() {
    return "NOT EXISTS (SELECT 1 FROM candles bad WHERE"
           " bad.symbol=candles.symbol AND bad.timeframe=candles.timeframe AND "
           + misalignedExpr("bad") + ")";
}
// Series keyed by (source_id, symbol, timeframe).
std::string cleanSourceSeriesExpr() {
    return "NOT EXISTS (SELECT 1 FROM candles bad WHERE"
           " bad.source_id=candles.source_id AND bad.symbol=candles.symbol"
           " AND bad.timeframe=candles.timeframe AND "
           + misalignedExpr("bad") + ")";
}
}  // namespace

namespace {
constexpr char kSchema[] =
    "CREATE TABLE IF NOT EXISTS candles ("
    "  source_id    TEXT NOT NULL,"
    "  symbol       TEXT NOT NULL,"
    "  timeframe    TEXT NOT NULL,"
    "  open_time    INTEGER NOT NULL,"
    "  open         REAL NOT NULL,"
    "  high         REAL NOT NULL,"
    "  low          REAL NOT NULL,"
    "  close        REAL NOT NULL,"
    "  tick_volume  INTEGER NOT NULL DEFAULT 0,"
    "  spread       INTEGER NOT NULL DEFAULT 0,"
    "  real_volume  INTEGER NOT NULL DEFAULT 0,"
    "  received_at  INTEGER NOT NULL,"
    "  PRIMARY KEY (source_id, symbol, timeframe, open_time)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_candles_lookup "
    "  ON candles(symbol, timeframe, open_time);";
}  // namespace

CandleStore::~CandleStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

bool CandleStore::open(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ != nullptr) {
        error = "CandleStore already open";
        return false;
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        if (db_ != nullptr) {
            error = std::string("cannot open candle store: ") +
                    sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        } else {
            error = "cannot open candle store: out of memory";
        }
        return false;
    }

    int timeoutResult = sqlite3_busy_timeout(db_, 5000);
    (void)timeoutResult;

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        error = std::string("cannot prepare candle store: ") +
                (errMsg != nullptr ? errMsg : "unknown");
        if (errMsg != nullptr) sqlite3_free(errMsg);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    // WAL makes reads non-blocking against writes on the same DB file.
    char* walMsg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &walMsg);
    if (walMsg != nullptr) sqlite3_free(walMsg);
    return true;
}

bool CandleStore::exec(const std::string& sql, std::string& error) const {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        error = std::string("candle store error: ") +
                (errMsg != nullptr ? errMsg : "unknown");
        if (errMsg != nullptr) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool CandleStore::insertBatch(const std::vector<StoredCandle>& candles,
                              std::vector<CandleInsertResult>& results,
                              std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    results.clear();
    if (db_ == nullptr) {
        error = "candle store is not open";
        return false;
    }

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    std::string txError;
    bool ok = true;
    for (const auto& c : candles) {
        CandleInsertResult result;
        result.symbol = c.symbol;
        result.timeframe = c.timeframe;
        result.openTimeUtc = c.openTimeUtc;

        // Existence check first: INSERT OR IGNORE would swallow the distinction
        // between "inserted" and "ignored", so we disambiguate up front.
        bool exists = false;
        {
            sqlite3_stmt* check = nullptr;
            const char* csql =
                "SELECT 1 FROM candles"
                " WHERE source_id=?1 AND symbol=?2 AND timeframe=?3"
                "   AND open_time=?4 LIMIT 1;";
            if (sqlite3_prepare_v2(db_, csql, -1, &check, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(check, 1, c.sourceId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(check, 2, c.symbol.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(check, 3, c.timeframe.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(check, 4, c.openTimeUtc);
                if (sqlite3_step(check) == SQLITE_ROW) exists = true;
            }
            if (check != nullptr) sqlite3_finalize(check);
        }

        if (exists) {
            result.state = "already_stored";
            results.push_back(std::move(result));
            continue;
        }

        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO candles"
            " (source_id, symbol, timeframe, open_time, open, high, low, close,"
            "  tick_volume, spread, real_volume, received_at)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            txError = std::string("prepare failed: ") + sqlite3_errmsg(db_);
            ok = false;
            break;
        }
        sqlite3_bind_text(stmt, 1, c.sourceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, c.symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, c.timeframe.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, c.openTimeUtc);
        sqlite3_bind_double(stmt, 5, c.open);
        sqlite3_bind_double(stmt, 6, c.high);
        sqlite3_bind_double(stmt, 7, c.low);
        sqlite3_bind_double(stmt, 8, c.close);
        sqlite3_bind_int64(stmt, 9, c.tickVolume);
        sqlite3_bind_int64(stmt, 10, c.spread);
        sqlite3_bind_int64(stmt, 11, c.realVolume);
        sqlite3_bind_int64(stmt, 12, c.receivedAtUtc);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            result.state = "stored_new";
        } else if (rc == SQLITE_CONSTRAINT) {
            // Race with a concurrent insert of the same candle; treat as a
            // duplicate, not an error.
            result.state = "already_stored";
        } else {
            txError = std::string("insert failed: ") + sqlite3_errmsg(db_);
            ok = false;
            result.state = "rejected";
            result.reason = txError;
            break;
        }
        results.push_back(std::move(result));
    }

    if (ok) {
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = txError;
        return false;
    }
    return true;
}

std::vector<StoredCandle> CandleStore::recent(const std::string& symbol,
                                              const std::string& timeframe,
                                              int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredCandle> out;
    if (db_ == nullptr) return out;

    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
        std::string("SELECT source_id, symbol, timeframe, open_time, open, high,"
                    "       low, close, tick_volume, spread, real_volume,"
                    "       received_at"
                    " FROM candles WHERE symbol=?1 AND timeframe=?2 AND ") +
        cleanSeriesExpr() + " ORDER BY open_time ASC LIMIT ?3;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, timeframe.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StoredCandle c;
        c.sourceId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        c.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.timeframe = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.openTimeUtc = sqlite3_column_int64(stmt, 3);
        c.open = sqlite3_column_double(stmt, 4);
        c.high = sqlite3_column_double(stmt, 5);
        c.low = sqlite3_column_double(stmt, 6);
        c.close = sqlite3_column_double(stmt, 7);
        c.tickVolume = sqlite3_column_int64(stmt, 8);
        c.spread = sqlite3_column_int64(stmt, 9);
        c.realVolume = sqlite3_column_int64(stmt, 10);
        c.receivedAtUtc = sqlite3_column_int64(stmt, 11);
        out.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<CandleSyncPoint> CandleStore::syncPoints(
    const std::string& sourceId, const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CandleSyncPoint> out;
    if (db_ == nullptr) return out;

    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
        std::string("SELECT symbol, timeframe, MAX(open_time), COUNT(*)"
                    " FROM candles WHERE source_id=?1 AND symbol=?2 AND ") +
        cleanSourceSeriesExpr() + " GROUP BY symbol, timeframe;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_text(stmt, 1, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, symbol.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CandleSyncPoint sp;
        sp.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sp.timeframe = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sp.lastOpenTimeUtc = sqlite3_column_int64(stmt, 2);
        sp.count = sqlite3_column_int64(stmt, 3);
        out.push_back(std::move(sp));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<CandleSyncPoint> CandleStore::syncPointsForSource(
    const std::string& sourceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CandleSyncPoint> out;
    if (db_ == nullptr) return out;

    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
        std::string("SELECT symbol, timeframe, MAX(open_time), COUNT(*)"
                    " FROM candles WHERE source_id=?1 AND ") +
        cleanSourceSeriesExpr() + " GROUP BY symbol, timeframe;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_text(stmt, 1, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CandleSyncPoint sp;
        sp.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sp.timeframe = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sp.lastOpenTimeUtc = sqlite3_column_int64(stmt, 2);
        sp.count = sqlite3_column_int64(stmt, 3);
        out.push_back(std::move(sp));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<std::string> CandleStore::timeframesWithData(
    const std::string& sourceId, const std::string& symbol) const {
    std::vector<std::string> out;
    const auto points = syncPoints(sourceId, symbol);
    for (const auto& p : points) out.push_back(p.timeframe);
    return out;
}

std::vector<std::string> CandleStore::distinctSymbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    if (db_ == nullptr) return out;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT DISTINCT symbol FROM candles ORDER BY symbol;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.emplace_back(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::int64_t CandleStore::latestOpenTime(const std::string& sourceId,
                                         const std::string& symbol,
                                         const std::string& timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return 0;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT COALESCE(MAX(open_time),0) FROM candles"
        " WHERE source_id=?1 AND symbol=?2 AND timeframe=?3;";
    std::int64_t out = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_text(stmt, 1, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, timeframe.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::int64_t CandleStore::latestOpenTimeAnySource(
    const std::string& symbol, const std::string& timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return 0;
    sqlite3_stmt* stmt = nullptr;

    // Only grid-aligned rows count as a real watermark for the timeframe, so a
    // leftover misaligned row cannot masquerade as the newest real candle.
    const std::string sql =
        std::string("SELECT COALESCE(MAX(open_time),0) FROM candles"
                    " WHERE symbol=?1 AND timeframe=?2 AND ") +
        cleanSeriesExpr() + ";";
    std::int64_t out = 0;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, timeframe.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool CandleStore::deleteCandle(const std::string& sourceId,
                               const std::string& symbol,
                               const std::string& timeframe,
                               std::int64_t openTimeUtc, int& deleted,
                               std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    deleted = 0;
    if (db_ == nullptr) {
        error = "candle store is not open";
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "DELETE FROM candles WHERE source_id=?1 AND symbol=?2"
        " AND timeframe=?3 AND open_time=?4;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("cannot prepare delete: ") + sqlite3_errmsg(db_);
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, timeframe.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, openTimeUtc);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = std::string("delete failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    deleted = sqlite3_changes(db_);
    return true;
}

}  // namespace aura