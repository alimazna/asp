#pragma once
// Durable news store (SQLite).
//
// Follows the CandleStore pattern exactly: a mutex-guarded sqlite3 handle, WAL
// journaling, a busy timeout, and a single transaction per batch. Identity rules:
//   * an article is unique by (source, article_id);
//   * a story is unique across sources by dedup_key — the first accepted article
//     for a story is canonical, later ones are reported `duplicate_content`;
//   * an analysis is unique by article_id and is superseded (not duplicated) on
//     re-analysis, so history reflects the latest validated result per article.
//
// Reads never fabricate: an absent article yields an empty record, an absent
// analysis yields status "unavailable".

#include "news/NewsModels.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace aura {

class NewsStore {
public:
    NewsStore() = default;
    ~NewsStore();

    NewsStore(const NewsStore&) = delete;
    NewsStore& operator=(const NewsStore&) = delete;

    bool open(const std::string& path, std::string& error);
    bool isOpen() const noexcept { return db_ != nullptr; }

    // Insert a batch of articles under one transaction, with cross-source
    // dedup. Per-article results preserve input order.
    bool insertArticles(const std::vector<NewsArticle>& articles,
                        std::vector<NewsInsertResult>& results,
                        std::string& error);

    // Persist a validated analysis (upsert by article_id).
    bool saveAnalysis(const NewsAnalysis& analysis, std::string& error);

    // True when `table` already has `column`; makes migrations idempotent.
    bool hasColumn(const std::string& table, const std::string& column) const;

    // Most recent articles, newest published first. `category` filters when
    // non-empty; `goldOnly` keeps only gold-relevant rows.
    std::vector<NewsArticle> recentArticles(int limit,
                                            const std::string& category,
                                            bool goldOnly) const;

    // One article by id (canonical or duplicate).
    bool getArticle(const std::string& articleId, NewsArticle& out) const;

    // Articles whose ids appear in `ids`, in an unspecified order. Used by the
    // news-results log to join analyses to their full source metadata in one
    // query instead of a lookup per row. Ids with no stored article are simply
    // absent from the result -- the caller treats that as "input not retained".
    std::vector<NewsArticle> articlesByIds(
        const std::vector<std::string>& ids) const;

    // Latest analysis for an article; returns false when none exists.
    bool getAnalysis(const std::string& articleId, NewsAnalysis& out) const;

    // Recent analyses across all articles (newest first) — the history feed.
    std::vector<NewsAnalysis> recentAnalyses(int limit) const;

    // Recent analyses filtered by direction and/or a minimum analyzed_at.
    // Used by the news-results log so the app can page a specific slice
    // (e.g. only actionable proposals, only the last day) without pulling the
    // whole table. An empty `direction` and a zero `sinceUtc` mean "no filter".
    std::vector<NewsAnalysis> recentAnalysesFiltered(
        int limit, const std::string& direction, std::int64_t sinceUtc) const;

private:
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
};

}  // namespace aura
