// Durable news store implementation (SQLite).

#include "news/NewsStore.h"

#include "foundation/Json.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>

namespace aura {
namespace {

constexpr char kSchema[] =
    "CREATE TABLE IF NOT EXISTS news_articles ("
    "  article_id     TEXT NOT NULL,"
    "  source         TEXT NOT NULL,"
    "  dedup_key      TEXT NOT NULL,"
    "  headline       TEXT NOT NULL,"
    "  url            TEXT NOT NULL DEFAULT '',"
    "  category       TEXT NOT NULL DEFAULT 'other',"
    "  published_at   INTEGER NOT NULL DEFAULT 0,"
    "  collected_at   INTEGER NOT NULL DEFAULT 0,"
    "  body_text      TEXT NOT NULL DEFAULT '',"
    "  body_available INTEGER NOT NULL DEFAULT 0,"
    "  language       TEXT NOT NULL DEFAULT '',"
    "  gold_relevance TEXT NOT NULL DEFAULT 'unknown',"
    "  PRIMARY KEY (source, article_id)"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_news_dedup ON news_articles(dedup_key);"
    "CREATE INDEX IF NOT EXISTS idx_news_published ON news_articles(published_at);"
    "CREATE TABLE IF NOT EXISTS news_analyses ("
    "  article_id      TEXT NOT NULL PRIMARY KEY,"
    "  status          TEXT NOT NULL,"
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
    "  astra_signal    TEXT NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_news_analysis_at "
    "  ON news_analyses(analyzed_at);";

std::string encodeStringArray(const std::vector<std::string>& items) {
    JsonValue arr = JsonValue::array();
    for (const auto& s : items) arr.push(JsonValue(s));
    return arr.dump();
}

std::vector<std::string> decodeStringArray(const std::string& text) {
    std::vector<std::string> out;
    JsonValue parsed;
    std::string error;
    if (!JsonValue::parse(text, parsed, error) || !parsed.isArray()) return out;
    for (const auto& item : parsed.items()) out.push_back(item.asString());
    return out;
}

std::string encodeFacts(const std::vector<NewsFact>& facts) {
    JsonValue arr = JsonValue::array();
    for (const auto& f : facts) {
        JsonValue obj = JsonValue::object();
        obj.set("text", JsonValue(f.text));
        obj.set("kind", JsonValue(f.kind));
        arr.push(std::move(obj));
    }
    return arr.dump();
}

std::vector<NewsFact> decodeFacts(const std::string& text) {
    std::vector<NewsFact> out;
    JsonValue parsed;
    std::string error;
    if (!JsonValue::parse(text, parsed, error) || !parsed.isArray()) return out;
    for (const auto& item : parsed.items()) {
        NewsFact f;
        f.text = item["text"].asString();
        f.kind = item["kind"].asString();
        out.push_back(std::move(f));
    }
    return out;
}

std::string columnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* p = sqlite3_column_text(stmt, col);
    return p != nullptr ? reinterpret_cast<const char*>(p) : std::string();
}

}  // namespace

std::string newsDedupKey(const std::string& headline) {
    std::string out;
    out.reserve(headline.size());
    for (char c : headline) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x80) {
            // ASCII: case-fold and keep alphanumerics, drop whitespace and
            // punctuation so "25 bp" and "25bp" share a key.
            if (std::isalnum(uc)) {
                out.push_back(static_cast<char>(std::tolower(uc)));
            }
        } else {
            // Multibyte UTF-8 (Arabic is the primary language) must be kept.
            // `std::isalnum` is false for every high byte, so dropping them
            // collapses every non-Latin headline to the empty key and makes
            // distinct stories look like duplicates of one another.
            out.push_back(c);
        }
    }
    return out;
}

std::string deriveArticleId(const std::string& source,
                            const std::string& url,
                            const std::string& headline) {
    if (!url.empty()) return url;
    return source + ":" + newsDedupKey(headline);
}

NewsStore::~NewsStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

bool NewsStore::open(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ != nullptr) {
        error = "NewsStore already open";
        return false;
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = std::string("cannot open news store: ") +
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
        error = std::string("cannot prepare news store: ") +
                (errMsg != nullptr ? errMsg : "unknown");
        if (errMsg != nullptr) sqlite3_free(errMsg);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    char* walMsg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &walMsg);
    if (walMsg != nullptr) sqlite3_free(walMsg);

    // Additive migration: a database created before gold_relevance existed
    // gains the column here. ADD COLUMN is safe and non-destructive; the
    // existence probe makes it idempotent so restart is a no-op. Existing rows
    // read back as 'unknown', which is the honest value: they were never
    // classified.
    if (!hasColumn("news_analyses", "gold_relevance")) {
        char* migMsg = nullptr;
        if (sqlite3_exec(db_,
                         "ALTER TABLE news_analyses ADD COLUMN "
                         "gold_relevance TEXT NOT NULL DEFAULT 'unknown';",
                         nullptr, nullptr, &migMsg) != SQLITE_OK) {
            error = std::string("cannot migrate news store: ") +
                    (migMsg != nullptr ? migMsg : "unknown");
            if (migMsg != nullptr) sqlite3_free(migMsg);
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }
    }
    return true;
}

// True when `table` already has `column`. Used to make additive migrations
// idempotent across restarts.
bool NewsStore::hasColumn(const std::string& table,
                          const std::string& column) const {
    const std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* stmt = nullptr;
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

bool NewsStore::insertArticles(const std::vector<NewsArticle>& articles,
                               std::vector<NewsInsertResult>& results,
                               std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    results.clear();
    if (db_ == nullptr) {
        error = "news store is not open";
        return false;
    }

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    std::string txError;
    bool ok = true;

    for (const auto& a : articles) {
        NewsInsertResult result;
        result.articleId = a.articleId;

        // Identity guard: an empty article id or dedup key cannot be stored
        // safely. The dedup key has a UNIQUE index, so every empty key would
        // collide with the first one; an empty id cannot be looked up. Reject
        // explicitly rather than let the constraint silently mask it.
        if (a.articleId.empty() || a.dedupKey.empty()) {
            result.state = "rejected";
            result.reason = a.articleId.empty()
                                ? "missing article id"
                                : "headline has no dedup key (empty after normalization)";
            results.push_back(std::move(result));
            continue;
        }

        // 1) Exact identity already present?
        bool idExists = false;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql =
                "SELECT 1 FROM news_articles WHERE source=?1 AND article_id=?2"
                " LIMIT 1;";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, a.source.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, a.articleId.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) idExists = true;
            }
            if (stmt != nullptr) sqlite3_finalize(stmt);
        }
        if (idExists) {
            result.state = "duplicate_id";
            results.push_back(std::move(result));
            continue;
        }

        // 2) Same story already carried by another source?
        std::string canonicalSource;
        std::string canonicalId;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql =
                "SELECT source, article_id FROM news_articles WHERE dedup_key=?1"
                " LIMIT 1;";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, a.dedupKey.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    canonicalSource = columnText(stmt, 0);
                    canonicalId = columnText(stmt, 1);
                }
            }
            if (stmt != nullptr) sqlite3_finalize(stmt);
        }
        if (!canonicalId.empty()) {
            result.state = "duplicate_content";
            result.canonicalArticleId = canonicalId;
            result.reason = "story already carried by " + canonicalSource;
            results.push_back(std::move(result));
            continue;
        }

        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO news_articles"
            " (article_id, source, dedup_key, headline, url, category,"
            "  published_at, collected_at, body_text, body_available, language,"
            "  gold_relevance)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            txError = std::string("prepare failed: ") + sqlite3_errmsg(db_);
            ok = false;
            break;
        }
        sqlite3_bind_text(stmt, 1, a.articleId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, a.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, a.dedupKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, a.headline.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, a.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, a.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, a.publishedAtUtc);
        sqlite3_bind_int64(stmt, 8, a.collectedAtUtc);
        sqlite3_bind_text(stmt, 9, a.bodyText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, a.bodyAvailable ? 1 : 0);
        sqlite3_bind_text(stmt, 11, a.language.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, a.goldRelevance.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            result.state = "stored_new";
        } else if (rc == SQLITE_CONSTRAINT) {
            // Concurrent insert of the same identity or dedup key.
            result.state = "duplicate_content";
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

bool NewsStore::saveAnalysis(const NewsAnalysis& a, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        error = "news store is not open";
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO news_analyses"
        " (article_id, status, status_reason, summary_ar, facts_json,"
        "  implications_ar, supporting_ar, opposing_ar, gold_impact_ar,"
        "  direction, has_entry, entry, has_stop, stop_loss, has_target,"
        "  take_profit, invalidating_ar, astra_agreement, disagreement_ar,"
        "  limitations_ar, provider_id, model_id, analyzed_at, timeframe_ctx,"
        "  candle_as_of, astra_signal, gold_relevance)"
        " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,"
        "         ?18,?19,?20,?21,?22,?23,?24,?25,?26,?27)"
        " ON CONFLICT(article_id) DO UPDATE SET"
        "  status=excluded.status, status_reason=excluded.status_reason,"
        "  summary_ar=excluded.summary_ar, facts_json=excluded.facts_json,"
        "  implications_ar=excluded.implications_ar,"
        "  supporting_ar=excluded.supporting_ar, opposing_ar=excluded.opposing_ar,"
        "  gold_impact_ar=excluded.gold_impact_ar, direction=excluded.direction,"
        "  has_entry=excluded.has_entry, entry=excluded.entry,"
        "  has_stop=excluded.has_stop, stop_loss=excluded.stop_loss,"
        "  has_target=excluded.has_target, take_profit=excluded.take_profit,"
        "  invalidating_ar=excluded.invalidating_ar,"
        "  astra_agreement=excluded.astra_agreement,"
        "  disagreement_ar=excluded.disagreement_ar,"
        "  limitations_ar=excluded.limitations_ar, provider_id=excluded.provider_id,"
        "  model_id=excluded.model_id, analyzed_at=excluded.analyzed_at,"
        "  timeframe_ctx=excluded.timeframe_ctx, candle_as_of=excluded.candle_as_of,"
        "  astra_signal=excluded.astra_signal,"
        "  gold_relevance=excluded.gold_relevance;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("prepare analysis failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    const std::string facts = encodeFacts(a.facts);
    const std::string impl = encodeStringArray(a.marketImplicationsAr);
    const std::string supp = encodeStringArray(a.supportingEvidenceAr);
    const std::string opp = encodeStringArray(a.opposingEvidenceAr);
    const std::string lim = encodeStringArray(a.limitationsAr);

    sqlite3_bind_text(stmt, 1, a.articleId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, a.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, a.statusReason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, a.summaryAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, facts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, impl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, supp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, opp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, a.goldImpactAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, a.proposedDirection.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, a.hasEntry ? 1 : 0);
    sqlite3_bind_double(stmt, 12, a.entry);
    sqlite3_bind_int(stmt, 13, a.hasStop ? 1 : 0);
    sqlite3_bind_double(stmt, 14, a.stopLoss);
    sqlite3_bind_int(stmt, 15, a.hasTarget ? 1 : 0);
    sqlite3_bind_double(stmt, 16, a.takeProfit);
    sqlite3_bind_text(stmt, 17, a.invalidatingConditionsAr.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, a.astraAgreement.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, a.disagreementNoteAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, lim.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 21, a.providerId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 22, a.modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 23, a.analyzedAtUtc);
    sqlite3_bind_text(stmt, 24, a.timeframeContext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 25, a.candleAsOfUtc);
    sqlite3_bind_text(stmt, 26, a.astraSignalRef.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 27, a.goldRelevance.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = std::string("save analysis failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::vector<NewsArticle> NewsStore::recentArticles(int limit,
                                                   const std::string& category,
                                                   bool goldOnly) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NewsArticle> out;
    if (db_ == nullptr) return out;

    std::string sql =
        "SELECT article_id, source, dedup_key, headline, url, category,"
        "       published_at, collected_at, body_text, body_available, language,"
        "       gold_relevance FROM news_articles WHERE 1=1";
    if (!category.empty()) sql += " AND category=?1";
    if (goldOnly) sql += " AND gold_relevance IN ('high','medium')";
    sql += " ORDER BY published_at DESC LIMIT ?";
    // Bind the limit with the correct index depending on the optional filter.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    int bindIndex = 1;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bindIndex++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bindIndex, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NewsArticle a;
        a.articleId = columnText(stmt, 0);
        a.source = columnText(stmt, 1);
        a.dedupKey = columnText(stmt, 2);
        a.headline = columnText(stmt, 3);
        a.url = columnText(stmt, 4);
        a.category = columnText(stmt, 5);
        a.publishedAtUtc = sqlite3_column_int64(stmt, 6);
        a.collectedAtUtc = sqlite3_column_int64(stmt, 7);
        a.bodyText = columnText(stmt, 8);
        a.bodyAvailable = sqlite3_column_int(stmt, 9) != 0;
        a.language = columnText(stmt, 10);
        a.goldRelevance = columnText(stmt, 11);
        out.push_back(std::move(a));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool NewsStore::getArticle(const std::string& articleId, NewsArticle& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT article_id, source, dedup_key, headline, url, category,"
        "       published_at, collected_at, body_text, body_available, language,"
        "       gold_relevance FROM news_articles WHERE article_id=?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, articleId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        out.articleId = columnText(stmt, 0);
        out.source = columnText(stmt, 1);
        out.dedupKey = columnText(stmt, 2);
        out.headline = columnText(stmt, 3);
        out.url = columnText(stmt, 4);
        out.category = columnText(stmt, 5);
        out.publishedAtUtc = sqlite3_column_int64(stmt, 6);
        out.collectedAtUtc = sqlite3_column_int64(stmt, 7);
        out.bodyText = columnText(stmt, 8);
        out.bodyAvailable = sqlite3_column_int(stmt, 9) != 0;
        out.language = columnText(stmt, 10);
        out.goldRelevance = columnText(stmt, 11);
    }
    sqlite3_finalize(stmt);
    return found;
}

namespace {
NewsAnalysis readAnalysisRow(sqlite3_stmt* stmt) {
    NewsAnalysis a;
    a.articleId = columnText(stmt, 0);
    a.status = columnText(stmt, 1);
    a.statusReason = columnText(stmt, 2);
    a.summaryAr = columnText(stmt, 3);
    a.facts = decodeFacts(columnText(stmt, 4));
    a.marketImplicationsAr = decodeStringArray(columnText(stmt, 5));
    a.supportingEvidenceAr = decodeStringArray(columnText(stmt, 6));
    a.opposingEvidenceAr = decodeStringArray(columnText(stmt, 7));
    a.goldImpactAr = columnText(stmt, 8);
    a.proposedDirection = columnText(stmt, 9);
    a.hasEntry = sqlite3_column_int(stmt, 10) != 0;
    a.entry = sqlite3_column_double(stmt, 11);
    a.hasStop = sqlite3_column_int(stmt, 12) != 0;
    a.stopLoss = sqlite3_column_double(stmt, 13);
    a.hasTarget = sqlite3_column_int(stmt, 14) != 0;
    a.takeProfit = sqlite3_column_double(stmt, 15);
    a.invalidatingConditionsAr = columnText(stmt, 16);
    a.astraAgreement = columnText(stmt, 17);
    a.disagreementNoteAr = columnText(stmt, 18);
    a.limitationsAr = decodeStringArray(columnText(stmt, 19));
    a.providerId = columnText(stmt, 20);
    a.modelId = columnText(stmt, 21);
    a.analyzedAtUtc = sqlite3_column_int64(stmt, 22);
    a.timeframeContext = columnText(stmt, 23);
    a.candleAsOfUtc = sqlite3_column_int64(stmt, 24);
    a.astraSignalRef = columnText(stmt, 25);
    a.goldRelevance = columnText(stmt, 26);
    return a;
}

const char* kAnalysisColumns =
    "article_id, status, status_reason, summary_ar, facts_json,"
    " implications_ar, supporting_ar, opposing_ar, gold_impact_ar, direction,"
    " has_entry, entry, has_stop, stop_loss, has_target, take_profit,"
    " invalidating_ar, astra_agreement, disagreement_ar, limitations_ar,"
    " provider_id, model_id, analyzed_at, timeframe_ctx, candle_as_of,"
    " astra_signal, gold_relevance";
}  // namespace

bool NewsStore::getAnalysis(const std::string& articleId, NewsAnalysis& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) return false;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + kAnalysisColumns +
                            " FROM news_analyses WHERE article_id=?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, articleId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        out = readAnalysisRow(stmt);
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<NewsAnalysis> NewsStore::recentAnalyses(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NewsAnalysis> out;
    if (db_ == nullptr) return out;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + kAnalysisColumns +
                            " FROM news_analyses ORDER BY analyzed_at DESC"
                            " LIMIT ?1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readAnalysisRow(stmt));
    sqlite3_finalize(stmt);
    return out;
}

}  // namespace aura
