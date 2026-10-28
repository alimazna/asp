// News / history API response builders (v1 envelope).

#include "api/NewsApi.h"

namespace aura {
namespace {

ApiField rawField(const std::string& name, const std::string& json) {
    return ApiField{name, json, true};
}

ApiField stringField(const std::string& name, const std::string& value) {
    return ApiField{name, jsonString(value), true};
}

ApiField intField(const std::string& name, std::int64_t value) {
    return ApiField{name, jsonInteger(value), true};
}

ApiField boolField(const std::string& name, bool value) {
    return ApiField{name, jsonBool(value), true};
}

std::string stringArray(const std::vector<std::string>& items) {
    std::vector<std::string> encoded;
    encoded.reserve(items.size());
    for (const auto& s : items) encoded.push_back(jsonString(s));
    return jsonArray(encoded);
}

}  // namespace

std::string NewsApi::articleJson(const NewsArticle& a, bool includeBody) {
    std::vector<ApiField> fields;
    fields.push_back(stringField("article_id", a.articleId));
    fields.push_back(stringField("headline", a.headline));
    fields.push_back(stringField("source", a.source));
    fields.push_back(stringField("url", a.url));
    fields.push_back(stringField("category", a.category));
    // An absent publish time is unknown, not epoch 0. Emitting 0 (and the
    // derived 1970-01-01 ISO string) would present a fabricated reading the
    // client renders as a real date; null is the honest representation.
    if (a.publishedAtUtc > 0) {
        fields.push_back(intField("published_at", a.publishedAtUtc));
        const std::string iso = isoUtcSeconds(a.publishedAtUtc);
        fields.push_back(iso.empty() ? rawField("published_at_iso", "null")
                                     : stringField("published_at_iso", iso));
    } else {
        fields.push_back(rawField("published_at", "null"));
        fields.push_back(rawField("published_at_iso", "null"));
    }
    fields.push_back(intField("collected_at", a.collectedAtUtc));
    fields.push_back(stringField("gold_relevance", a.goldRelevance));
    fields.push_back(boolField("body_available", a.bodyAvailable));
    if (includeBody) {
        // Body text is only returned for an explicit article view; a list stays
        // light. When absent it is null, never a fabricated summary.
        fields.push_back(a.bodyAvailable ? stringField("body_text", a.bodyText)
                                         : rawField("body_text", "null"));
    }
    return jsonObject(fields);
}

std::string NewsApi::analysisJson(const NewsAnalysis& a) {
    std::vector<ApiField> fields;
    fields.push_back(stringField("article_id", a.articleId));
    fields.push_back(stringField("status", a.status));
    fields.push_back(stringField("status_reason", a.statusReason));
    fields.push_back(stringField("summary_ar", a.summaryAr));

    std::vector<std::string> facts;
    for (const auto& f : a.facts) {
        facts.push_back(jsonObject({stringField("text", f.text),
                                    stringField("kind", f.kind)}));
    }
    fields.push_back(rawField("facts", jsonArray(facts)));
    fields.push_back(rawField("market_implications_ar",
                              stringArray(a.marketImplicationsAr)));
    fields.push_back(rawField("supporting_evidence_ar",
                              stringArray(a.supportingEvidenceAr)));
    fields.push_back(rawField("opposing_evidence_ar",
                              stringArray(a.opposingEvidenceAr)));
    fields.push_back(stringField("gold_impact_ar", a.goldImpactAr));
    fields.push_back(stringField("ai_gold_relevance", a.goldRelevance));
    fields.push_back(stringField("direction", a.proposedDirection));

    // Levels: each is independently optional. An absent level is null, never 0,
    // so the client cannot mistake "no level" for "level at zero".
    std::vector<ApiField> levels;
    levels.push_back(a.hasEntry ? rawField("entry", jsonNumber(a.entry))
                                : rawField("entry", "null"));
    levels.push_back(a.hasStop ? rawField("stop_loss", jsonNumber(a.stopLoss))
                               : rawField("stop_loss", "null"));
    levels.push_back(a.hasTarget ? rawField("take_profit", jsonNumber(a.takeProfit))
                                 : rawField("take_profit", "null"));
    fields.push_back(rawField("levels", jsonObject(levels)));

    fields.push_back(stringField("astra_agreement", a.astraAgreement));
    fields.push_back(stringField("disagreement_note_ar", a.disagreementNoteAr));
    fields.push_back(stringField("invalidating_conditions_ar",
                                 a.invalidatingConditionsAr));
    fields.push_back(rawField("limitations_ar", stringArray(a.limitationsAr)));
    fields.push_back(stringField("provider_id", a.providerId));
    fields.push_back(stringField("model_id", a.modelId));
    fields.push_back(intField("analyzed_at", a.analyzedAtUtc));
    fields.push_back(stringField("timeframe_context", a.timeframeContext));
    // As with published_at: no candle evidence means the data version is
    // unknown, so it is null rather than a fabricated epoch 0.
    fields.push_back(a.candleAsOfUtc > 0 ? intField("candle_as_of", a.candleAsOfUtc)
                                         : rawField("candle_as_of", "null"));
    fields.push_back(stringField("astra_signal_ref", a.astraSignalRef));
    return jsonObject(fields);
}

ApiResponse NewsApi::list(const std::vector<NewsArticle>& articles,
                          const std::vector<std::string>& analysisStatus) {
    std::vector<std::string> elements;
    for (std::size_t i = 0; i < articles.size(); ++i) {
        const std::string status =
            i < analysisStatus.size() ? analysisStatus[i] : "none";
        elements.push_back(jsonObject({
            rawField("article", articleJson(articles[i], false)),
            stringField("analysis_status", status),
        }));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonObject({rawField("news", jsonArray(elements))})),
                       true, ""};
}

ApiResponse NewsApi::detail(const NewsArticle& article,
                            const NewsAnalysis* analysis, bool hasAnalysis) {
    std::vector<ApiField> fields;
    fields.push_back(rawField("article", articleJson(article, true)));
    fields.push_back(boolField("has_analysis", hasAnalysis));
    if (hasAnalysis && analysis != nullptr) {
        fields.push_back(rawField("analysis", analysisJson(*analysis)));
    } else {
        fields.push_back(rawField("analysis", "null"));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true, ""};
}

ApiResponse NewsApi::quota(const LlmQuotaState& s) {
    std::vector<ApiField> fields;
    fields.push_back(stringField("category", s.category));
    // Null, never a fabricated 0: a missing reset time means "unknown".
    fields.push_back(s.retryAfterKnown()
                         ? intField("retry_after_seconds", s.retryAfterSeconds)
                         : rawField("retry_after_seconds", "null"));
    fields.push_back(intField("last_change_at", s.lastChangeUtc));
    fields.push_back(intField("last_success_at", s.lastSuccessUtc));
    fields.push_back(boolField("cooling_down", s.coolingDown));
    fields.push_back(intField("cooldown_until", s.cooldownUntilUtc));
    fields.push_back(boolField("quota_exhausted", s.quotaExhausted()));
    fields.push_back(boolField("rate_limited", s.rateLimited()));
    return ApiResponse{200, "application/json",
                       envelope(jsonObject({rawField("quota", jsonObject(fields))})),
                       true, ""};
}

ApiResponse NewsApi::history(const std::vector<NewsAnalysis>& analyses,
                             const std::vector<NewsArticle>& articles) {
    std::vector<std::string> elements;
    for (const auto& a : analyses) {
        std::string headline;
        std::string source;
        for (const auto& art : articles) {
            if (art.articleId == a.articleId) {
                headline = art.headline;
                source = art.source;
                break;
            }
        }
        elements.push_back(jsonObject({
            stringField("headline", headline),
            stringField("source", source),
            rawField("analysis", analysisJson(a)),
        }));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonObject({rawField("history", jsonArray(elements))})),
                       true, ""};
}

ApiResponse NewsApi::ingestAck(int received,
                               const std::vector<NewsInsertResult>& results) {
    int stored = 0, duplicates = 0, rejected = 0;
    std::vector<std::string> elements;
    for (const auto& r : results) {
        std::vector<ApiField> f;
        f.push_back(stringField("article_id", r.articleId));
        f.push_back(stringField("state", r.state));
        f.push_back(stringField("reason", r.reason));
        f.push_back(stringField("canonical_article_id", r.canonicalArticleId));
        elements.push_back(jsonObject(f));
        if (r.state == "stored_new") ++stored;
        else if (r.state == "rejected") ++rejected;
        else ++duplicates;
    }
    std::vector<ApiField> fields;
    fields.push_back(intField("received", received));
    fields.push_back(intField("stored", stored));
    fields.push_back(intField("duplicates", duplicates));
    fields.push_back(intField("rejected", rejected));
    fields.push_back(rawField("results", jsonArray(elements)));
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true, ""};
}

std::string NewsApi::eventJson(const ScheduledEvent& e) {
    std::vector<ApiField> fields;
    fields.push_back(stringField("event_key", e.eventKey));
    fields.push_back(stringField("title", e.title));
    fields.push_back(stringField("country", e.country));
    fields.push_back(stringField("impact", e.impact));
    fields.push_back(intField("scheduled_at", e.scheduledAtUtc));
    const std::string iso = isoUtcSeconds(e.scheduledAtUtc);
    fields.push_back(iso.empty() ? rawField("scheduled_at_iso", "null")
                                 : stringField("scheduled_at_iso", iso));
    // Consensus/previous are whatever the calendar published. An event the feed
    // carried without a forecast stays null, never "0" or "" which the UI would
    // render as a real number or a real string.
    fields.push_back(e.forecast.empty() ? rawField("forecast", "null")
                                        : stringField("forecast", e.forecast));
    fields.push_back(e.previous.empty() ? rawField("previous", "null")
                                        : stringField("previous", e.previous));
    fields.push_back(stringField("source", e.source));
    fields.push_back(stringField("stage", e.stage));
    fields.push_back(boolField("gold_relevant", e.goldRelevant));
    fields.push_back(intField("first_seen", e.firstSeenUtc));
    fields.push_back(intField("updated_at", e.updatedAtUtc));
    fields.push_back(e.preparedAtUtc > 0 ? intField("prepared_at", e.preparedAtUtc)
                                         : rawField("prepared_at", "null"));
    fields.push_back(stringField("prepare_status", e.prepareStatus));
    fields.push_back(e.releasedAtUtc > 0 ? intField("released_at", e.releasedAtUtc)
                                         : rawField("released_at", "null"));
    fields.push_back(stringField("release_status", e.releaseStatus));
    return jsonObject(fields);
}

ApiResponse NewsApi::events(const std::vector<ScheduledEvent>& events,
                            const std::vector<std::string>& stages) {
    std::vector<std::string> elements;
    elements.reserve(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const std::string stage = i < stages.size() ? stages[i] : events[i].stage;
        elements.push_back(jsonObject({
            rawField("event", eventJson(events[i])),
            stringField("stage", stage),
            boolField("has_expectation", events[i].preparedAtUtc > 0),
            boolField("has_result", events[i].releasedAtUtc > 0),
        }));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonObject({rawField("events", jsonArray(elements))})),
                       true, ""};
}

ApiResponse NewsApi::eventDetail(const ScheduledEvent& event,
                                 const NewsAnalysis* expectation,
                                 bool hasExpectation, const NewsAnalysis* result,
                                 bool hasResult) {
    std::vector<ApiField> fields;
    fields.push_back(rawField("event", eventJson(event)));
    // A turn that has not run is reported as absent, distinct from a turn that
    // ran and came back unavailable. The client must not read "not yet" as
    // "analysed and found empty".
    fields.push_back(boolField("has_expectation", hasExpectation));
    fields.push_back(hasExpectation && expectation != nullptr
                         ? rawField("expectation", analysisJson(*expectation))
                         : rawField("expectation", "null"));
    fields.push_back(boolField("has_result", hasResult));
    fields.push_back(hasResult && result != nullptr
                         ? rawField("result", analysisJson(*result))
                         : rawField("result", "null"));
    return ApiResponse{200, "application/json", envelope(jsonObject(fields)), true,
                       ""};
}

}  // namespace aura
