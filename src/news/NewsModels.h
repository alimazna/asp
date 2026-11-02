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

// The real market a proposed entry/stop/target is checked against, so a small
// model that ignores the prompt and invents a level has that level dropped
// rather than displayed. `strict=false` disables the proximity check and keeps
// the historical "any finite positive number" behaviour.
struct PriceGrounding {
    bool strict = false;
    double referencePrice = 0.0;  // newest real closed price; 0 = none
    double atr = 0.0;             // realised per-bar range; 0 = none
    double maxDeviationBars = 8.0;
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
    // Relative, price-free guidance. When the model cannot know the absolute
    // price (no candle collector connected, stale feed, thin data) it must not
    // invent one; instead it may state the expected move and the protective
    // stop as DISTANCES in points. A distance is honest where a price would be
    // fabricated, and it is what the user asked for ("قد يصل إلى X نقطة").
    // Zero means the model gave none. Validated as a small positive integer.
    bool hasExpectedMove = false;
    int expectedMovePoints = 0;
    bool hasStopDistance = false;
    int stopDistancePoints = 0;
    std::string relativeLevelsNoteAr;
    std::string invalidatingConditionsAr;
    // AI-assigned gold relevance, distinct from the feed's routing tag. The
    // feed tag says which query found the article; this is the model's own
    // judgement of whether the news actually moves gold. Empty means the model
    // did not classify it, and the UI must not present an unclassified article
    // as gold-relevant.
    std::string goldRelevance = "unknown";   // high | medium | low | none
    // The transmission channel through which this news moves gold. A closed
    // vocabulary, so the client can group and filter without parsing prose and
    // the model cannot smuggle a free-form claim in. `unknown` is the honest
    // value when the model did not classify it.
    std::string goldChannel = "unknown";
    // How soon the effect is expected to be felt. Also a closed vocabulary: a
    // qualitative horizon is honest, a made-up number of minutes is not.
    std::string impactHorizon = "unknown";

    // ---- Tone, framing and subtext -------------------------------------
    //
    // The reading of HOW the story is told, as distinct from what it says.
    // Every classifier below is a closed vocabulary for the same reason
    // gold_channel is: the client groups and filters on the value, and the
    // model cannot smuggle a free-form claim in through a classifier field.
    // `unknown` is the honest value whenever the model did not classify it,
    // and an out-of-set value is rejected rather than coerced.
    //
    // `toneEvidenceAr` is what keeps the tone grounded rather than asserted:
    // the model must name the wording that carries the tone, so a tone with no
    // quotable basis is visible as such instead of being taken on trust.
    std::string tone = "unknown";          // very_negative..very_positive
    std::string toneStrength = "unknown";  // weak | moderate | strong
    std::string toneEvidenceAr;            // the wording that carries the tone
    std::string sourceRegister = "unknown";  // official | wire | analytical | ...
    std::string sensationalism = "unknown";  // none | low | medium | high
    // Whether the article reports something genuinely new or re-tells known
    // information. A rehash is not a market event, and saying so prevents the
    // UI from presenting old news as a fresh catalyst.
    std::string novelty = "unknown";       // new | partly_priced | already_priced
    // How much the article itself hedges. High uncertainty in the SOURCE is a
    // property of the story, not of our analysis, and must not be flattened.
    std::string uncertainty = "unknown";   // low | medium | high

    // The subtext layer: what the story is really about once framing is set
    // aside, and whose interest the framing serves. Free Arabic prose, because
    // this is genuinely open-ended reading rather than a classification, but
    // bounded and required to be evidence-linked by the prompt.
    std::string hiddenContextAr;           // what is behind the headline
    std::string framingBiasAr;             // how it is framed, and for whom
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
