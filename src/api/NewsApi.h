#pragma once
// News / history API response builders (v1 envelope).
//
// Pure formatting: given store rows, emit the versioned envelope. No I/O and no
// analysis happen here, so the shapes are unit-testable without a database.

#include "api/BackendApiSchema.h"
#include "macro/EventStore.h"
#include "news/LlmQuotaMonitor.h"
#include "news/NewsModels.h"

#include <string>
#include <vector>

namespace aura {

class NewsApi {
public:
    // GET /api/v1/news — a list of articles, newest published first. Each
    // element carries the latest analysis status so the list can show whether an
    // article has been analysed without a second round-trip.
    static ApiResponse list(const std::vector<NewsArticle>& articles,
                            const std::vector<std::string>& analysisStatus);

    // GET /api/v1/news/analyze?id=... — one article plus its latest analysis.
    // `hasAnalysis` distinguishes "no analysis yet" from "analysis unavailable".
    static ApiResponse detail(const NewsArticle& article,
                              const NewsAnalysis* analysis, bool hasAnalysis);

    // GET /api/v1/news/quota — the LLM provider's availability snapshot. Lets
    // the Android app show an honest Arabic banner when the daily AI quota is
    // spent, instead of appearing broken. Secret-free by construction.
    static ApiResponse quota(const LlmQuotaState& state);

    // GET /api/v1/history — persisted analyses (newest first), each joined with
    // the article headline/source it belongs to.
    static ApiResponse history(const std::vector<NewsAnalysis>& analyses,
                               const std::vector<NewsArticle>& articles);

    // POST /api/v1/news/ingest — acknowledgement with per-article outcomes.
    static ApiResponse ingestAck(
        int received, const std::vector<NewsInsertResult>& results);

    // GET /api/v1/events — scheduled macro events. `scope=upcoming` (default)
    // lists gold-relevant events from now on; `scope=past` lists released ones.
    // Each element carries whether an expectation/result turn exists, so the app
    // can show "expectation ready" without a second round-trip.
    static ApiResponse events(const std::vector<ScheduledEvent>& events,
                              const std::vector<std::string>& stages);

    // GET /api/v1/events/detail?key=... — one event with its expectation turn,
    // its result turn, or both. A missing turn is reported as absent, never as
    // an empty analysis.
    static ApiResponse eventDetail(const ScheduledEvent& event,
                                   const NewsAnalysis* expectation,
                                   bool hasExpectation,
                                   const NewsAnalysis* result,
                                   bool hasResult);

    // The `data` object for one scheduled event.
    static std::string eventJson(const ScheduledEvent& e);

    // The `data` object for an analysis, shared by detail and history.
    static std::string analysisJson(const NewsAnalysis& analysis);
    static std::string articleJson(const NewsArticle& article, bool includeBody);
};

}  // namespace aura
