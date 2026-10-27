#pragma once
// AYZR news-intelligence domain model.
//
// Every field is either sourced from the provider or explicitly absent. The
// honesty contract binds here too: nothing is inferred, and `status` gates
// whether a proposal may be shown at all.

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// Canonical categories the news surface understands. A category outside this
// set is stored as "other" rather than rejected, so a provider taxonomy change
// cannot drop an article.
struct NewsArticle {
    std::string articleId;      // stable identity supplied by the client
    std::string dedupKey;       // normalized headline, for cross-source dedup
    std::string headline;
    std::string source;
    std::string url;
    std::string category = "other";
    std::int64_t publishedAtUtc = 0;
    std::int64_t collectedAtUtc = 0;
    std::string bodyText;
    bool bodyAvailable = false;
    std::string language;
    std::string goldRelevance = "unknown";  // high|medium|low|unknown
};

// Per-article result of an insert attempt. `duplicate_content` means a different
// source already carried the same story; `canonicalArticleId` is that story's id.
struct NewsInsertResult {
    std::string articleId;
    std::string state;   // stored_new | duplicate_id | duplicate_content | rejected
    std::string reason;
    std::string canonicalArticleId;
};

// One classified statement drawn from the article.
struct NewsFact {
    std::string text;
    std::string kind;   // fact | forecast | commentary | unverified
};

// Validated analysis of one article. `status` is the gate:
//   ok                   - a proposal is present and evidence-supported
//   insufficient_data    - evidence contradictory or missing; no proposal
//   unavailable          - the provider could not be reached
//   invalid_model_output - the model replied but failed schema validation
struct NewsAnalysis {
    std::string articleId;
    std::string status = "unavailable";
    std::string statusReason;
    std::string summaryAr;
    std::vector<NewsFact> facts;
    std::vector<std::string> marketImplicationsAr;
    std::vector<std::string> supportingEvidenceAr;
    std::vector<std::string> opposingEvidenceAr;
    std::string goldImpactAr;
    std::string proposedDirection = "no_trade";  // bullish|bearish|sideways|no_trade
    bool hasEntry = false;
    double entry = 0.0;
    bool hasStop = false;
    double stopLoss = 0.0;
    bool hasTarget = false;
    double takeProfit = 0.0;
    std::string invalidatingConditionsAr;
    std::string astraAgreement = "unknown";   // agree | disagree | unknown
    std::string disagreementNoteAr;
    std::vector<std::string> limitationsAr;
    std::string providerId;
    std::string modelId;
    std::int64_t analyzedAtUtc = 0;
    // Data versions the analysis was derived from, for auditability.
    std::string timeframeContext;   // e.g. "M15,H4"
    std::int64_t candleAsOfUtc = 0;
    std::string astraSignalRef;
};

// Normalize a headline into a dedup key: ASCII is lowercased and reduced to
// alphanumerics; non-ASCII (e.g. Arabic UTF-8) bytes are preserved verbatim so
// non-Latin headlines keep a stable, distinct key.
std::string newsDedupKey(const std::string& headline);

// Derive a stable article id from (source, url) or, failing that, the dedup key.
std::string deriveArticleId(const std::string& source, const std::string& url,
                            const std::string& headline);

}  // namespace aura
