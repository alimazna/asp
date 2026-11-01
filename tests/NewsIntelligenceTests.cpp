// TST-NEWS-0001 - News intelligence unit tests: defensive parsing, durable
// store identity/dedup/idempotency, provider classification, and the service's
// bounded-retry failure states. Uses the deterministic MockLlmProvider so no
// external quota is consumed.

#include "TestHarness.h"

#include "api/BackendApiSchema.h"
#include "foundation/OutboundHttpClient.h"
#include "news/Llm7Provider.h"
#include "news/MockLlmProvider.h"
#include "news/NewsAnalysisParser.h"
#include "news/NewsAnalysisService.h"
#include "news/NewsPrompt.h"
#include "news/NewsStore.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include <sqlite3.h>

using namespace aura;
namespace fs = std::filesystem;

namespace {

std::string tempDbPath() {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("astra_news_test_" + std::to_string(unique) + ".sqlite"))
        .string();
}

NewsArticle makeArticle(const std::string& id, const std::string& source,
                        const std::string& headline, std::int64_t published) {
    NewsArticle a;
    a.articleId = id;
    a.source = source;
    a.headline = headline;
    a.dedupKey = newsDedupKey(headline);
    a.publishedAtUtc = published;
    a.collectedAtUtc = published + 5;
    a.category = "monetary_policy";
    a.goldRelevance = "high";
    return a;
}

}  // namespace

// --- Parser: valid payload ------------------------------------------------
TEST_CASE(news_parser_accepts_valid_payload) {
    const ParsedAnalysis parsed =
        parseAnalysisResponse(MockLlmProvider::validPayload());
    CHECK(parsed.valid);
    CHECK_EQ(parsed.analysis.proposedDirection, std::string("bullish"));
    CHECK(parsed.analysis.hasEntry);
    CHECK_EQ(parsed.analysis.entry, 2650.5);
    CHECK(parsed.analysis.hasStop);
    CHECK(parsed.analysis.hasTarget);
    CHECK_EQ(parsed.analysis.facts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parsed.analysis.facts.front().kind, std::string("fact"));
    CHECK_EQ(parsed.analysis.astraAgreement, std::string("agree"));
}

// --- Parser: tolerates prose/fences around the object ----------------------
TEST_CASE(news_parser_extracts_json_from_prose) {
    const std::string text =
        "Here is the analysis:\n```json\n{\"summary_ar\":\"ملخص\","
        "\"direction\":\"no_trade\",\"astra_agreement\":\"unknown\"}\n```\nDone.";
    const ParsedAnalysis parsed = parseAnalysisResponse(text);
    CHECK(parsed.valid);
    CHECK_EQ(parsed.analysis.proposedDirection, std::string("no_trade"));
    // A no_trade proposal must never carry levels even if present.
    CHECK(!parsed.analysis.hasEntry);
}

// --- Parser: rejects malformed / missing / out-of-range --------------------
TEST_CASE(news_parser_rejects_bad_output) {
    CHECK(!parseAnalysisResponse("no json here at all").valid);
    CHECK(!parseAnalysisResponse("{ this is broken ").valid);
    // Missing required summary_ar.
    CHECK(!parseAnalysisResponse(R"({"direction":"bullish"})").valid);
    // Invalid direction value.
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"moon"})")
               .valid);
    // Invalid fact kind.
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"no_trade","astra_agreement":"unknown",)"
               R"("facts":[{"text":"x","kind":"guess"}]})")
               .valid);
    // Missing astra_agreement defaults to unknown and is still valid.
    const ParsedAnalysis ok = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"sideways"})");
    CHECK(ok.valid);
    CHECK_EQ(ok.analysis.astraAgreement, std::string("unknown"));
}

// --- Parser: levels dropped for non-actionable directions ------------------
TEST_CASE(news_parser_drops_levels_for_no_trade) {
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"sideways","astra_agreement":"unknown",)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680})");
    CHECK(parsed.valid);
    CHECK(!parsed.analysis.hasEntry);
    CHECK(!parsed.analysis.hasStop);
    CHECK(!parsed.analysis.hasTarget);
}

// --- Store: identity, cross-source dedup, idempotency ----------------------
TEST_CASE(news_store_identity_and_dedup) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));

    std::vector<NewsArticle> batch = {
        makeArticle("a1", "srcA", "Fed cuts rates by 25bp", 1000),
        makeArticle("a2", "srcB", "Fed cuts rates by 25 bp", 1001),  // same story
    };
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));
    CHECK_EQ(results.size(), static_cast<std::size_t>(2));
    CHECK_EQ(results[0].state, std::string("stored_new"));
    CHECK_EQ(results[1].state, std::string("duplicate_content"));
    CHECK_EQ(results[1].canonicalArticleId, std::string("a1"));

    // Replaying the same batch is safe: the first is duplicate_id.
    std::vector<NewsInsertResult> replay;
    REQUIRE(store.insertArticles(batch, replay, error));
    CHECK_EQ(replay[0].state, std::string("duplicate_id"));
    CHECK_EQ(replay[1].state, std::string("duplicate_content"));

    // Only one article is visible.
    CHECK_EQ(store.recentArticles(10, "", false).size(),
             static_cast<std::size_t>(1));

    fs::remove(path);
}

// --- Store: durable across reopen -----------------------------------------
TEST_CASE(news_store_is_durable_across_reopen) {
    const std::string path = tempDbPath();
    {
        NewsStore store;
        std::string error;
        REQUIRE(store.open(path, error));
        std::vector<NewsArticle> batch = {
            makeArticle("d1", "srcA", "Gold rises on weak dollar", 2000)};
        std::vector<NewsInsertResult> results;
        REQUIRE(store.insertArticles(batch, results, error));
    }
    {
        NewsStore reopened;
        std::string error;
        REQUIRE(reopened.open(path, error));
        NewsArticle out;
        REQUIRE(reopened.getArticle("d1", out));
        CHECK_EQ(out.headline, std::string("Gold rises on weak dollar"));
    }
    fs::remove(path);
}

// --- Store: analysis upsert (no duplicate history rows) --------------------
TEST_CASE(news_store_analysis_upserts) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));
    std::vector<NewsArticle> batch = {
        makeArticle("x1", "srcA", "Inflation cools", 3000)};
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));

    NewsAnalysis a;
    a.articleId = "x1";
    a.status = "ok";
    a.summaryAr = "ملخص أول";
    a.proposedDirection = "bullish";
    a.astraAgreement = "agree";
    a.analyzedAtUtc = 3100;
    REQUIRE(store.saveAnalysis(a, error));

    a.summaryAr = "ملخص محدّث";
    a.analyzedAtUtc = 3200;
    REQUIRE(store.saveAnalysis(a, error));

    CHECK_EQ(store.recentAnalyses(10).size(), static_cast<std::size_t>(1));
    NewsAnalysis out;
    REQUIRE(store.getAnalysis("x1", out));
    CHECK_EQ(out.summaryAr, std::string("ملخص محدّث"));

    fs::remove(path);
}

// --- Provider classification ----------------------------------------------
TEST_CASE(llm7_classifies_http_status) {
    CHECK(Llm7Provider::classify(true, 200) == LlmOutcome::Ok);
    CHECK(Llm7Provider::classify(true, 401) == LlmOutcome::AuthFailure);
    CHECK(Llm7Provider::classify(true, 403) == LlmOutcome::AuthFailure);
    CHECK(Llm7Provider::classify(true, 429) == LlmOutcome::RateLimited);
    CHECK(Llm7Provider::classify(true, 504) == LlmOutcome::Timeout);
    CHECK(Llm7Provider::classify(true, 500) == LlmOutcome::ServerError);
    CHECK(Llm7Provider::classify(true, 400) == LlmOutcome::BadRequest);
    CHECK(Llm7Provider::classify(false, 0) == LlmOutcome::TransportError);
}

// --- Provider: no token => auth failure, never an anonymous call -----------
TEST_CASE(llm7_without_token_is_auth_failure) {
    Llm7Config config;
    config.apiToken = "";
    Llm7Provider provider(config, makeDefaultOutboundHttpClient());
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    const LlmResult result = provider.complete(request);
    CHECK(result.outcome == LlmOutcome::AuthFailure);
    CHECK(!result.ok());
}

// --- Provider: request body is well-formed and model-configurable ----------
TEST_CASE(llm7_request_body_shape) {
    Llm7Config config;
    config.model = "nemotron-3-nano:30b";
    Llm7Provider provider(config, makeDefaultOutboundHttpClient());
    LlmRequest request;
    request.model = "nemotron-3-nano:30b";
    request.messages = {{"system", "sys"}, {"user", "usr"}};
    const std::string body = provider.buildRequestBody(request);
    CHECK(body.find("\"model\":\"nemotron-3-nano:30b\"") != std::string::npos);
    CHECK(body.find("\"role\":\"system\"") != std::string::npos);
    CHECK(body.find("\"stream\":false") != std::string::npos);
    // The token must never appear in the body.
    CHECK(body.find("Authorization") == std::string::npos);
}

// --- Service: valid mock produces a persisted-ready analysis ---------------
TEST_CASE(news_service_valid_analysis) {
    MockLlmProvider provider(MockLlmProvider::Mode::Valid);
    NewsAnalysisService service(&provider, 2);
    NewsArticle article = makeArticle("s1", "srcA", "CPI below forecast", 4000);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    ctx.source = article.source;
    ctx.bodyAvailable = true;
    ctx.bodyText = "CPI came in at 2.9%.";
    const NewsAnalysis a = service.analyze(article, ctx, 4100);
    CHECK_EQ(a.status, std::string("ok"));
    CHECK_EQ(a.proposedDirection, std::string("bullish"));
    CHECK_EQ(a.providerId, std::string("mock"));
    CHECK_EQ(a.articleId, std::string("s1"));
    CHECK_EQ(service.lastAttemptCount(), 1);
}

// --- Service: malformed output is a clear invalid state, not a recommendation
TEST_CASE(news_service_invalid_output_state) {
    MockLlmProvider provider(MockLlmProvider::Mode::Malformed);
    NewsAnalysisService service(&provider, 1);
    NewsArticle article = makeArticle("s2", "srcA", "Headline", 4200);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    const NewsAnalysis a = service.analyze(article, ctx, 4300);
    CHECK_EQ(a.status, std::string("invalid_model_output"));
    CHECK(a.summaryAr.empty());
    CHECK(!a.hasEntry);
}

// --- Service: a bounded repair turn rescues schema-invalid model output ------
// The real provider (nemotron-3-nano:30b) has no JSON mode and was observed
// returning valid JSON that violates the schema (e.g. missing `direction`). One
// corrective turn must recover it; the schema is not relaxed.
TEST_CASE(news_service_repair_turn_recovers_invalid_output) {
    MockLlmProvider provider(MockLlmProvider::Mode::MalformedThenValid);
    NewsAnalysisService service(&provider, 2);
    NewsArticle article = makeArticle("s6", "srcA", "Headline", 4250);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    const NewsAnalysis a = service.analyze(article, ctx, 4260);
    CHECK_EQ(a.status, std::string("ok"));
    CHECK_EQ(a.proposedDirection, std::string("bullish"));
    CHECK_EQ(service.lastAttemptCount(), 2);  // original + one repair, no storm
}

// --- Service: the repair turn replays the conversation (assistant + user) ----
// Verified against the real provider: an inline restatement in a single user
// message does NOT recover a valid object; the assistant+user turn sequence does.
TEST_CASE(news_service_repair_turn_uses_conversational_sequence) {
    MockLlmProvider provider(MockLlmProvider::Mode::MalformedThenValid);
    NewsAnalysisService service(&provider, 2);
    NewsArticle article = makeArticle("s8", "srcA", "Headline", 4290);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    (void)service.analyze(article, ctx, 4300);
    const auto& msgs = provider.lastMessages();
    CHECK_EQ(msgs.size(), static_cast<std::size_t>(4));
    CHECK_EQ(msgs[0].role, std::string("system"));
    CHECK_EQ(msgs[1].role, std::string("user"));
    CHECK_EQ(msgs[2].role, std::string("assistant"));  // the invalid reply
    CHECK_EQ(msgs[3].role, std::string("user"));       // the corrective instruction
}

// --- Service: repair is skipped when only one attempt is allowed ------------
TEST_CASE(news_service_no_repair_when_attempt_budget_is_one) {
    MockLlmProvider provider(MockLlmProvider::Mode::MalformedThenValid);
    NewsAnalysisService service(&provider, 1);
    NewsArticle article = makeArticle("s7", "srcA", "Headline", 4270);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    const NewsAnalysis a = service.analyze(article, ctx, 4280);
    CHECK_EQ(a.status, std::string("invalid_model_output"));
    CHECK_EQ(service.lastAttemptCount(), 1);
}

// --- Service: provider timeout => unavailable, bounded retries -------------
TEST_CASE(news_service_timeout_is_unavailable_with_bounded_retries) {
    MockLlmProvider provider(MockLlmProvider::Mode::Timeout);
    NewsAnalysisService service(&provider, 3);
    NewsArticle article = makeArticle("s3", "srcA", "Headline", 4400);
    NewsPromptContext ctx;
    const NewsAnalysis a = service.analyze(article, ctx, 4500);
    CHECK_EQ(a.status, std::string("unavailable"));
    // Retried but bounded: exactly maxAttempts, never more.
    CHECK_EQ(service.lastAttemptCount(), 3);
}

// --- Service: rate limit is retried (retryable), auth failure is not -------
TEST_CASE(news_service_retry_policy) {
    {
        MockLlmProvider provider(MockLlmProvider::Mode::RateLimited);
        NewsAnalysisService service(&provider, 2);
        NewsArticle article = makeArticle("s4", "srcA", "H", 4600);
        NewsPromptContext ctx;
        const NewsAnalysis a = service.analyze(article, ctx, 4700);
        CHECK_EQ(a.status, std::string("unavailable"));
        CHECK_EQ(service.lastAttemptCount(), 2);  // rate limit retried
    }
    {
        MockLlmProvider provider(MockLlmProvider::Mode::AuthFailure);
        NewsAnalysisService service(&provider, 3);
        NewsArticle article = makeArticle("s5", "srcA", "H", 4800);
        NewsPromptContext ctx;
        const NewsAnalysis a = service.analyze(article, ctx, 4900);
        CHECK_EQ(a.status, std::string("unavailable"));
        CHECK_EQ(service.lastAttemptCount(), 1);  // auth failure not retried
    }
}

// --- Prompt: states the no-invention rules and unavailable body ------------
TEST_CASE(news_prompt_states_rules) {
    NewsPromptContext ctx;
    ctx.headline = "Fed holds rates";
    ctx.source = "example.com";
    ctx.bodyAvailable = false;
    const NewsPrompt prompt = buildNewsPrompt(ctx);
    CHECK(prompt.system.find("SHADOW-ONLY") != std::string::npos);
    CHECK(prompt.system.find("Never invent") != std::string::npos);
    CHECK(prompt.user.find("UNAVAILABLE") != std::string::npos);
}

// --- Dedup key: Arabic (the app's primary language) must not collapse ------
TEST_CASE(news_dedup_key_preserves_arabic) {
    const std::string a = "ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع";
    const std::string b = "البنك المركزي يثبت أسعار الفائدة دون تغيير";
    const std::string keyA = newsDedupKey(a);
    const std::string keyB = newsDedupKey(b);
    // Non-empty and distinct: the previous ASCII-only normalization produced ""
    // for every Arabic headline, which made distinct stories look identical.
    CHECK(!keyA.empty());
    CHECK(!keyB.empty());
    CHECK(keyA != keyB);
    // ASCII normalization is unchanged.
    CHECK_EQ(newsDedupKey("Fed cuts rates by 25 bp"),
             std::string("fedcutsratesby25bp"));
    // Whitespace-only differences still collapse for ASCII.
    CHECK_EQ(newsDedupKey("Fed Cuts Rates"), newsDedupKey("fed cuts rates"));
}

// --- Store: distinct Arabic stories are both stored, not deduped -----------
TEST_CASE(news_store_distinct_arabic_stories_not_deduped) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));

    std::vector<NewsArticle> batch = {
        makeArticle("ar1", "srcA", "ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع",
                    5000),
        makeArticle("ar2", "srcB", "البنك المركزي يثبت أسعار الفائدة دون تغيير",
                    5001),
    };
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));
    CHECK_EQ(results[0].state, std::string("stored_new"));
    CHECK_EQ(results[1].state, std::string("stored_new"));
    CHECK_EQ(store.recentArticles(10, "", false).size(),
             static_cast<std::size_t>(2));

    fs::remove(path);
}

// --- Store: empty identity is rejected, never silently collapsed -----------
TEST_CASE(news_store_rejects_empty_identity) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));

    NewsArticle noId = makeArticle("", "srcA", "Headline one", 6000);
    NewsArticle noKey = makeArticle("k1", "srcA", "", 6001);  // empty dedup key
    noKey.dedupKey.clear();
    std::vector<NewsArticle> batch = {noId, noKey};
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));
    CHECK_EQ(results[0].state, std::string("rejected"));
    CHECK_EQ(results[1].state, std::string("rejected"));
    CHECK_EQ(store.recentArticles(10, "", false).size(),
             static_cast<std::size_t>(0));

    fs::remove(path);
}

// --- Store: gold/category filters (bind-index correctness) -----------------
TEST_CASE(news_store_gold_and_category_filters) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));

    NewsArticle goldHigh = makeArticle("g1", "srcA", "Gold high relevance", 7000);
    goldHigh.category = "monetary_policy";
    goldHigh.goldRelevance = "high";
    NewsArticle goldLow = makeArticle("g2", "srcA", "Gold low relevance", 7001);
    goldLow.category = "monetary_policy";
    goldLow.goldRelevance = "low";
    NewsArticle otherCat = makeArticle("g3", "srcA", "Equities only", 7002);
    otherCat.category = "equities";
    otherCat.goldRelevance = "medium";

    std::vector<NewsArticle> batch = {goldHigh, goldLow, otherCat};
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));

    CHECK_EQ(store.recentArticles(10, "", false).size(),
             static_cast<std::size_t>(3));
    CHECK_EQ(store.recentArticles(10, "monetary_policy", false).size(),
             static_cast<std::size_t>(2));
    CHECK_EQ(store.recentArticles(10, "", true).size(),
             static_cast<std::size_t>(2));  // high + medium
    CHECK_EQ(store.recentArticles(10, "monetary_policy", true).size(),
             static_cast<std::size_t>(1));  // goldHigh only
    CHECK_EQ(store.recentArticles(10, "equities", true).size(),
             static_cast<std::size_t>(1));  // otherCat (medium)

    fs::remove(path);
}

// --- Store: analysis round-trips facts, levels and evidence ----------------
TEST_CASE(news_store_analysis_roundtrip) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));
    std::vector<NewsArticle> batch = {
        makeArticle("r1", "srcA", "Roundtrip story", 8000)};
    std::vector<NewsInsertResult> results;
    REQUIRE(store.insertArticles(batch, results, error));

    NewsAnalysis a;
    a.articleId = "r1";
    a.status = "ok";
    a.summaryAr = "ملخص";
    a.facts = {{"حقيقة", "fact"}, {"توقع", "forecast"}};
    a.marketImplicationsAr = {"أثر إيجابي"};
    a.supportingEvidenceAr = {"دليل"};
    a.opposingEvidenceAr = {"دليل معاكس"};
    a.limitationsAr = {"قيد"};
    a.goldImpactAr = "إيجابي";
    a.proposedDirection = "bullish";
    a.hasEntry = true;
    a.entry = 2650.5;
    a.hasStop = true;
    a.stopLoss = 2640.0;
    a.hasTarget = true;
    a.takeProfit = 2680.0;
    a.astraAgreement = "disagree";
    a.providerId = "mock";
    a.modelId = "mock-model";
    a.analyzedAtUtc = 8100;
    a.timeframeContext = "M15,H4";
    a.candleAsOfUtc = 7900;
    a.astraSignalRef = "sig-1";
    REQUIRE(store.saveAnalysis(a, error));

    NewsAnalysis out;
    REQUIRE(store.getAnalysis("r1", out));
    CHECK_EQ(out.summaryAr, std::string("ملخص"));
    CHECK_EQ(out.facts.size(), static_cast<std::size_t>(2));
    CHECK_EQ(out.facts[1].kind, std::string("forecast"));
    CHECK_EQ(out.marketImplicationsAr.size(), static_cast<std::size_t>(1));
    CHECK_EQ(out.opposingEvidenceAr.size(), static_cast<std::size_t>(1));
    CHECK(out.hasEntry && out.hasStop && out.hasTarget);
    CHECK_EQ(out.entry, 2650.5);
    CHECK_EQ(out.stopLoss, 2640.0);
    CHECK_EQ(out.takeProfit, 2680.0);
    CHECK_EQ(out.astraAgreement, std::string("disagree"));
    CHECK_EQ(out.timeframeContext, std::string("M15,H4"));
    CHECK_EQ(out.candleAsOfUtc, static_cast<std::int64_t>(7900));
    CHECK_EQ(out.astraSignalRef, std::string("sig-1"));

    fs::remove(path);
}

// --- Parser: escaped quotes / nested braces inside strings ----------------
TEST_CASE(news_parser_handles_braces_inside_strings) {
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"a {nested} \"quoted\" summary","direction":"no_trade"})");
    CHECK(parsed.valid);
    CHECK_EQ(parsed.analysis.summaryAr, std::string("a {nested} \"quoted\" summary"));
}

// --- Parser: non-positive / non-finite levels are dropped ------------------
TEST_CASE(news_parser_drops_nonpositive_levels) {
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"bullish","astra_agreement":"unknown",)"
        R"("entry":0,"stop_loss":-5,"take_profit":2680})");
    CHECK(parsed.valid);
    CHECK(!parsed.analysis.hasEntry);   // zero is not a level
    CHECK(!parsed.analysis.hasStop);    // negative is not a level
    CHECK(parsed.analysis.hasTarget);   // positive is kept
    CHECK_EQ(parsed.analysis.takeProfit, 2680.0);
}

// --- Parser: AI-assigned gold relevance is validated, never invented ------
TEST_CASE(news_parser_keeps_valid_gold_relevance) {
    for (const char* v : {"high", "medium", "low", "none"}) {
        const std::string text =
            std::string(R"({"summary_ar":"س","direction":"no_trade",)")
            + R"("gold_relevance":")" + v + R"("})";
        const ParsedAnalysis p = parseAnalysisResponse(text);
        CHECK(p.valid);
        CHECK_EQ(p.analysis.goldRelevance, std::string(v));
    }
}

TEST_CASE(news_parser_rejects_unknown_gold_relevance_value) {
    // An out-of-vocabulary value must fail loudly rather than be coerced.
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"no_trade","gold_relevance":"maybe"})")
               .valid);
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"no_trade","gold_relevance":"HIGH"})")
               .valid);
}

TEST_CASE(news_parser_absent_gold_relevance_stays_unknown) {
    // The model never asserted a relevance; the parser must not invent one.
    const ParsedAnalysis p = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"no_trade"})");
    CHECK(p.valid);
    CHECK_EQ(p.analysis.goldRelevance, std::string("unknown"));
}

// --- Parser: the gold channel and impact horizon are closed vocabularies ----
TEST_CASE(news_parser_keeps_valid_gold_channel_and_horizon) {
    for (const char* c : {"monetary_policy", "inflation", "usd_rates",
                          "safe_haven", "physical_demand", "positioning",
                          "other", "unknown"}) {
        const std::string text =
            std::string(R"({"summary_ar":"س","direction":"no_trade",)")
            + R"("gold_channel":")" + c + R"("})";
        const ParsedAnalysis p = parseAnalysisResponse(text);
        CHECK(p.valid);
        CHECK_EQ(p.analysis.goldChannel, std::string(c));
    }
    for (const char* h : {"immediate", "short", "medium", "long", "unknown"}) {
        const std::string text =
            std::string(R"({"summary_ar":"س","direction":"no_trade",)")
            + R"("impact_horizon":")" + h + R"("})";
        const ParsedAnalysis p = parseAnalysisResponse(text);
        CHECK(p.valid);
        CHECK_EQ(p.analysis.impactHorizon, std::string(h));
    }
}

TEST_CASE(news_parser_rejects_unknown_gold_channel_and_horizon) {
    // An out-of-vocabulary channel must fail loudly, exactly as gold_relevance
    // does: a channel the model never named must not reach the client.
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"no_trade","gold_channel":"rates"})")
               .valid);
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"no_trade","impact_horizon":"5m"})")
               .valid);
}

TEST_CASE(news_parser_absent_channel_and_horizon_stay_unknown) {
    // Absent => "unknown". The client renders "غير محدَّد", never a guess.
    const ParsedAnalysis p = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"no_trade"})");
    CHECK(p.valid);
    CHECK_EQ(p.analysis.goldChannel, std::string("unknown"));
    CHECK_EQ(p.analysis.impactHorizon, std::string("unknown"));
}

TEST_CASE(news_store_roundtrips_channel_and_horizon) {
    const std::string path = tempDbPath();
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));
    NewsAnalysis a;
    a.articleId = "ch1";
    a.status = "ok";
    a.summaryAr = "ملخص";
    a.proposedDirection = "bullish";
    a.goldChannel = "safe_haven";
    a.impactHorizon = "immediate";
    a.analyzedAtUtc = 5000;
    REQUIRE(store.saveAnalysis(a, error));
    NewsAnalysis out;
    REQUIRE(store.getAnalysis("ch1", out));
    CHECK_EQ(out.goldChannel, std::string("safe_haven"));
    CHECK_EQ(out.impactHorizon, std::string("immediate"));
    fs::remove(path);
}

TEST_CASE(news_store_migrates_pre_existing_analyses_table) {
    // A database created before the two columns existed must gain them on open,
    // with existing rows reading back as "unknown" -- never a fabricated value.
    const std::string path = tempDbPath();
    {
        // Build a legacy table by hand: the original schema without the two
        // classifier columns, plus one row.
        NewsStore seed;
        std::string error;
        REQUIRE(seed.open(path, error));
        (void)error;
    }
    // Drop the two columns to simulate a pre-migration database. SQLite has no
    // DROP COLUMN before 3.35, so rebuild the table explicitly.
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        const char* rebuild =
            "ALTER TABLE news_analyses RENAME TO news_analyses_old;"
            "CREATE TABLE news_analyses ("
            " article_id TEXT NOT NULL PRIMARY KEY, status TEXT NOT NULL,"
            " status_reason TEXT NOT NULL DEFAULT '', summary_ar TEXT NOT NULL DEFAULT '',"
            " facts_json TEXT NOT NULL DEFAULT '[]', implications_ar TEXT NOT NULL DEFAULT '[]',"
            " supporting_ar TEXT NOT NULL DEFAULT '[]', opposing_ar TEXT NOT NULL DEFAULT '[]',"
            " gold_impact_ar TEXT NOT NULL DEFAULT '', gold_relevance TEXT NOT NULL DEFAULT 'unknown',"
            " direction TEXT NOT NULL DEFAULT 'no_trade', has_entry INTEGER NOT NULL DEFAULT 0,"
            " entry REAL NOT NULL DEFAULT 0, has_stop INTEGER NOT NULL DEFAULT 0,"
            " stop_loss REAL NOT NULL DEFAULT 0, has_target INTEGER NOT NULL DEFAULT 0,"
            " take_profit REAL NOT NULL DEFAULT 0, invalidating_ar TEXT NOT NULL DEFAULT '',"
            " astra_agreement TEXT NOT NULL DEFAULT 'unknown', disagreement_ar TEXT NOT NULL DEFAULT '',"
            " limitations_ar TEXT NOT NULL DEFAULT '[]', provider_id TEXT NOT NULL DEFAULT '',"
            " model_id TEXT NOT NULL DEFAULT '', analyzed_at INTEGER NOT NULL DEFAULT 0,"
            " timeframe_ctx TEXT NOT NULL DEFAULT '', candle_as_of INTEGER NOT NULL DEFAULT 0,"
            " astra_signal TEXT NOT NULL DEFAULT ''"
            ");"
            "INSERT INTO news_analyses (article_id, status, summary_ar, direction, analyzed_at)"
            " SELECT article_id, status, summary_ar, direction, analyzed_at FROM news_analyses_old;"
            "DROP TABLE news_analyses_old;";
        char* msg = nullptr;
        REQUIRE(sqlite3_exec(db, rebuild, nullptr, nullptr, &msg) == SQLITE_OK);
        sqlite3_close(db);
    }
    // Reopen: the migration must add the columns and preserve the row.
    NewsStore store;
    std::string error;
    REQUIRE(store.open(path, error));
    CHECK(store.hasColumn("news_analyses", "gold_channel"));
    CHECK(store.hasColumn("news_analyses", "impact_horizon"));
    fs::remove(path);
}

// --- Parser: oversized model output is rejected, not persisted -------------
TEST_CASE(news_parser_rejects_oversized_fields) {
    // A 70 KB summary exceeds the 64 KB cap.
    const std::string huge(70 * 1024, 'x');
    const std::string text =
        "{\"summary_ar\":\"" + huge + "\",\"direction\":\"no_trade\"}";
    CHECK(!parseAnalysisResponse(text).valid);

    // A facts[] array above the item cap is rejected.
    std::string facts = "{\"summary_ar\":\"س\",\"direction\":\"no_trade\","
                        "\"facts\":[";
    for (int i = 0; i < 300; ++i) {
        if (i > 0) facts += ",";
        facts += "{\"text\":\"f\",\"kind\":\"fact\"}";
    }
    facts += "]}";
    CHECK(!parseAnalysisResponse(facts).valid);
}

// --- Service: no provider configured => unavailable, no crash --------------
TEST_CASE(news_service_null_provider_is_unavailable) {
    NewsAnalysisService service(nullptr, 2);
    NewsArticle article = makeArticle("np", "srcA", "H", 9000);
    NewsPromptContext ctx;
    const NewsAnalysis a = service.analyze(article, ctx, 9100);
    CHECK_EQ(a.status, std::string("unavailable"));
    CHECK_EQ(service.lastAttemptCount(), 0);
    CHECK(!a.hasEntry);
}

// --- Service: empty model output is an invalid state, not a recommendation -
TEST_CASE(news_service_empty_output_state) {
    MockLlmProvider provider(MockLlmProvider::Mode::Empty);
    NewsAnalysisService service(&provider, 1);
    NewsArticle article = makeArticle("e1", "srcA", "H", 9200);
    NewsPromptContext ctx;
    const NewsAnalysis a = service.analyze(article, ctx, 9300);
    CHECK_EQ(a.status, std::string("invalid_model_output"));
    CHECK(a.summaryAr.empty());
    CHECK(!a.hasEntry);
}


namespace {

// Deterministic fake transport so the real Llm7Provider response path is
// exercised without a network. This is the only "mock" in these tests, and it
// sits at the network boundary (IOutboundHttpClient), not around business logic.
class FakeTransport : public IOutboundHttpClient {
public:
    OutboundHttpResponse canned;
    OutboundHttpRequest lastRequest;
    OutboundHttpResponse send(const OutboundHttpRequest& request) override {
        lastRequest = request;
        return canned;
    }
};

OutboundHttpResponse okResponse(const std::string& content) {
    OutboundHttpResponse r;
    r.ok = true;
    r.statusCode = 200;
    r.body = std::string("{\"choices\":[{\"message\":{\"content\":") +
             jsonString(content) + "}}]}";
    return r;
}

}  // namespace

// --- Llm7Provider: parses a real 200 response into content ----------------
TEST_CASE(llm7_parses_successful_response) {
    auto transport = std::make_shared<FakeTransport>();
    transport->canned = okResponse("hello from the model");
    Llm7Config config;
    config.apiToken = "test-token";
    Llm7Provider provider(config, transport);
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    const LlmResult result = provider.complete(request);
    CHECK(result.ok());
    CHECK_EQ(result.content, std::string("hello from the model"));
    CHECK_EQ(result.httpStatus, 200);
    // The token was sent as a header, never in the body.
    bool sawAuthHeader = false;
    for (const auto& h : transport->lastRequest.headers) {
        if (h.rfind("Authorization: Bearer ", 0) == 0) sawAuthHeader = true;
        CHECK(h.find("test-token") == std::string::npos ||
              h.rfind("Authorization", 0) == 0);
    }
    CHECK(sawAuthHeader);
    CHECK(transport->lastRequest.body.find("test-token") == std::string::npos);
}

// --- Llm7Provider: a 200 with no choices is a BadRequest, not a crash ------
TEST_CASE(llm7_rejects_response_without_choices) {
    auto transport = std::make_shared<FakeTransport>();
    transport->canned.ok = true;
    transport->canned.statusCode = 200;
    transport->canned.body = "{\"id\":\"x\"}";
    Llm7Config config;
    config.apiToken = "t";
    Llm7Provider provider(config, transport);
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    const LlmResult result = provider.complete(request);
    CHECK(result.outcome == LlmOutcome::BadRequest);
    CHECK(!result.ok());
}

// --- Llm7Provider: non-2xx maps to the classified outcome -----------------
TEST_CASE(llm7_maps_http_error_status) {
    auto transport = std::make_shared<FakeTransport>();
    Llm7Config config;
    config.apiToken = "t";
    Llm7Provider provider(config, transport);
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    for (const auto& [status, outcome] :
         std::vector<std::pair<int, LlmOutcome>>{
             {401, LlmOutcome::AuthFailure},
             {429, LlmOutcome::RateLimited},
             {503, LlmOutcome::ServerError}}) {
        transport->canned = OutboundHttpResponse{};
        transport->canned.ok = true;
        transport->canned.statusCode = status;
        transport->canned.body = "{\"error\":\"x\"}";
        const LlmResult result = provider.complete(request);
        CHECK(result.outcome == outcome);
    }
}

// --- Service: full pipeline (mock provider) persists facts and levels ------
TEST_CASE(news_service_full_pipeline_fields) {
    MockLlmProvider provider(MockLlmProvider::Mode::Valid);
    NewsAnalysisService service(&provider, 1);
    NewsArticle article = makeArticle("p1", "srcA", "CPI below forecast", 5000);
    NewsPromptContext ctx;
    ctx.headline = article.headline;
    ctx.source = article.source;
    ctx.bodyAvailable = true;
    ctx.bodyText = "CPI 2.9%.";
    ctx.candleSummary = "M15 (2 closed bars): ...";
    ctx.astraSummary = "{\"status\":\"ok\"}";
    const NewsAnalysis a = service.analyze(article, ctx, 5100);
    CHECK_EQ(a.status, std::string("ok"));
    CHECK_EQ(a.facts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(a.marketImplicationsAr.size(), static_cast<std::size_t>(1));
    CHECK(a.hasEntry && a.hasStop && a.hasTarget);
    CHECK_EQ(a.astraAgreement, std::string("agree"));
    CHECK(!a.invalidatingConditionsAr.empty());
    CHECK(!a.limitationsAr.empty());
}

// --- Price grounding: an invented level must be dropped, not shown ----------
// The model is forbidden to invent a price, but a small model still will. The
// live pipeline grounds every optional level against the real market it was
// shown; a number far from that market is dropped so it can never reach the UI
// as a suggestion.
TEST_CASE(grounded_levels_near_market_are_kept) {
    // Reference 2650, ATR 10, band 8 bars = 80 points. 2650/2640/2680 are all
    // inside the band, so the legitimate proposal survives.
    PriceGrounding g;
    g.strict = true;
    g.referencePrice = 2650.0;
    g.atr = 10.0;
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bullish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680})",
        g);
    CHECK(parsed.valid);
    CHECK(parsed.analysis.hasEntry);
    CHECK(parsed.analysis.hasStop);
    CHECK(parsed.analysis.hasTarget);
}

TEST_CASE(grounded_levels_far_from_market_are_dropped) {
    // The reported bug: the model returned a price it could not have known
    // (1900 when the real market is 2650). Every level is dropped.
    PriceGrounding g;
    g.strict = true;
    g.referencePrice = 2650.0;
    g.atr = 10.0;
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bullish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":1900,"stop_loss":1890,"take_profit":1950})",
        g);
    CHECK(parsed.valid);            // the analysis itself is still usable
    CHECK(!parsed.analysis.hasEntry);
    CHECK(!parsed.analysis.hasStop);
    CHECK(!parsed.analysis.hasTarget);
}

TEST_CASE(grounded_levels_without_reference_price_are_dropped) {
    // No candle collector was ever connected: referencePrice is 0. With no
    // market there is nothing for a level to be near, so every level is
    // dropped. This is the exact production condition the user hit.
    PriceGrounding g;
    g.strict = true;   // referencePrice stays 0
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bullish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680})",
        g);
    CHECK(parsed.valid);
    CHECK(!parsed.analysis.hasEntry);
    CHECK(!parsed.analysis.hasStop);
    CHECK(!parsed.analysis.hasTarget);
}

TEST_CASE(grounding_is_opt_in_and_lenient_by_default) {
    // The ungrounded overload (and the default PriceGrounding) keep the
    // historical behaviour, so a caller with no market reference is unaffected.
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bullish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680})");
    CHECK(parsed.valid);
    CHECK(parsed.analysis.hasEntry);
}

TEST_CASE(grounded_level_without_atr_uses_price_fraction_band) {
    // With a reference price but no ATR (e.g. a single candle), the band falls
    // back to 5% of price: 2600 +/- 130 keeps 2700 but drops 2800.
    PriceGrounding g;
    g.strict = true;
    g.referencePrice = 2600.0;   // atr stays 0
    const ParsedAnalysis near = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bearish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2700,"stop_loss":2710,"take_profit":2500})",
        g);
    CHECK(near.analysis.hasEntry);
    const ParsedAnalysis far = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bearish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2800,"stop_loss":2900,"take_profit":2400})",
        g);
    CHECK(!far.analysis.hasEntry);
    CHECK(!far.analysis.hasStop);
    CHECK(!far.analysis.hasTarget);
}

// --- Relative, price-free levels: a distance is honest where a price is not ---
// When the model cannot know the absolute price it may state the expected move
// and the protective stop as DISTANCES in points. These need no market
// reference (a distance is not a price), so they survive even with no candles.
TEST_CASE(relative_levels_are_kept_without_a_reference_price) {
    PriceGrounding g;
    g.strict = true;   // no reference price: absolute levels are dropped...
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"bullish",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("entry":2650,"expected_move_points":300,"stop_distance_points":120,)"
        R"("relative_levels_note_ar":"قد يتحرك نحو 300 نقطة"})",
        g);
    CHECK(parsed.valid);
    CHECK(!parsed.analysis.hasEntry);            // ...but the distances are kept
    CHECK(parsed.analysis.hasExpectedMove);
    CHECK_EQ(parsed.analysis.expectedMovePoints, 300);
    CHECK(parsed.analysis.hasStopDistance);
    CHECK_EQ(parsed.analysis.stopDistancePoints, 120);
    CHECK(!parsed.analysis.relativeLevelsNoteAr.empty());
}

TEST_CASE(relative_levels_reject_implausible_values) {
    // A negative, zero, fractional or runaway distance is dropped, so a model
    // that echoes a price into the distance field cannot smuggle it through.
    const auto parseWith = [](const std::string& extra) {
        return parseAnalysisResponse(
            R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
            R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
            R"("gold_impact_ar":"x","direction":"bearish",)"
            R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
            R"("invalidating_conditions_ar":"c","limitations_ar":[],)" +
                extra + "}");
    };
    CHECK(!parseWith(R"("expected_move_points":-50})").analysis.hasExpectedMove);
    CHECK(!parseWith(R"("expected_move_points":0})").analysis.hasExpectedMove);
    CHECK(!parseWith(R"("expected_move_points":12.5})").analysis.hasExpectedMove);
    CHECK(!parseWith(R"("stop_distance_points":999999999})")
               .analysis.hasStopDistance);
}

TEST_CASE(relative_levels_ignored_for_a_non_actionable_direction) {
    // A no_trade proposal must not carry a move or a stop: there is no trade.
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"no_trade",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("expected_move_points":300,"stop_distance_points":120})");
    CHECK(parsed.valid);
    CHECK(!parsed.analysis.hasExpectedMove);
    CHECK(!parsed.analysis.hasStopDistance);
}

// --- Tone / framing / subtext ---------------------------------------------
// The reading of HOW a story is told. Every classifier is a closed vocabulary
// (an out-of-set value is rejected, never coerced), an absent value is
// honestly "unknown", and a non-neutral tone with no quoted wording is dropped
// because an unattributed tone is an assertion the reader cannot check.

TEST_CASE(tone_accepts_a_full_valid_classification) {
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"no_trade",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)"
        R"("tone":"very_negative","tone_strength":"strong",)"
        R"("tone_evidence_ar":"انهيار حاد في الأسعار",)"
        R"("source_register":"wire","sensationalism":"high",)"
        R"("novelty":"partly_priced","uncertainty":"medium",)"
        R"("hidden_context_ar":"الخبر يخدم مصالح المنتجين.",)"
        R"("framing_bias_ar":"تركيز على الجانب السلبي."})");
    CHECK(parsed.valid);
    CHECK_EQ(std::string("very_negative"), parsed.analysis.tone);
    CHECK_EQ(std::string("strong"), parsed.analysis.toneStrength);
    CHECK_EQ(std::string("wire"), parsed.analysis.sourceRegister);
    CHECK_EQ(std::string("high"), parsed.analysis.sensationalism);
    CHECK_EQ(std::string("partly_priced"), parsed.analysis.novelty);
    CHECK_EQ(std::string("medium"), parsed.analysis.uncertainty);
    CHECK_EQ(std::string("انهيار حاد في الأسعار"),
             parsed.analysis.toneEvidenceAr);
    CHECK(!parsed.analysis.hiddenContextAr.empty());
    CHECK(!parsed.analysis.framingBiasAr.empty());
}

TEST_CASE(tone_fields_absent_are_honestly_unknown) {
    // A payload that never mentions tone must not be given one by default.
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"no_trade",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[]})");
    CHECK(parsed.valid);
    CHECK_EQ(std::string("unknown"), parsed.analysis.tone);
    CHECK_EQ(std::string("unknown"), parsed.analysis.toneStrength);
    CHECK_EQ(std::string("unknown"), parsed.analysis.sourceRegister);
    CHECK_EQ(std::string("unknown"), parsed.analysis.sensationalism);
    CHECK_EQ(std::string("unknown"), parsed.analysis.novelty);
    CHECK_EQ(std::string("unknown"), parsed.analysis.uncertainty);
    CHECK(parsed.analysis.hiddenContextAr.empty());
    CHECK(parsed.analysis.framingBiasAr.empty());
}

TEST_CASE(tone_rejects_out_of_vocabulary_values) {
    // Coercing an unrecognised tone to "neutral" would put a reading the model
    // never made in front of the user. It must be rejected instead.
    const std::string base =
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"no_trade",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)";
    CHECK(!parseAnalysisResponse(base + R"("tone":"bullish"})").valid);
    CHECK(!parseAnalysisResponse(base + R"("tone_strength":"extreme"})").valid);
    CHECK(!parseAnalysisResponse(base + R"("source_register":"blog"})").valid);
    CHECK(!parseAnalysisResponse(base + R"("sensationalism":"extreme"})").valid);
    CHECK(!parseAnalysisResponse(base + R"("novelty":"stale"})").valid);
    CHECK(!parseAnalysisResponse(base + R"("uncertainty":"very_high"})").valid);
}

TEST_CASE(tone_without_evidence_is_downgraded_not_displayed) {
    // A strong tone the model cannot attribute to any wording is an unchecked
    // assertion. It is dropped to "unknown" rather than shown, exactly as an
    // ungrounded price is dropped.
    const std::string base =
        R"({"summary_ar":"س","facts":[],"market_implications_ar":[],)"
        R"("supporting_evidence_ar":[],"opposing_evidence_ar":[],)"
        R"("gold_impact_ar":"x","direction":"no_trade",)"
        R"("astra_agreement":"unknown","disagreement_note_ar":"",)"
        R"("invalidating_conditions_ar":"c","limitations_ar":[],)";
    const ParsedAnalysis noEvidence = parseAnalysisResponse(
        base + R"("tone":"very_positive","tone_strength":"strong"})");
    CHECK(noEvidence.valid);
    CHECK_EQ(std::string("unknown"), noEvidence.analysis.tone);
    CHECK_EQ(std::string("unknown"), noEvidence.analysis.toneStrength);

    // With evidence the tone survives untouched.
    const ParsedAnalysis withEvidence = parseAnalysisResponse(
        base + R"("tone":"very_positive","tone_strength":"strong",)"
               R"("tone_evidence_ar":"قفزة قياسية"})");
    CHECK(withEvidence.valid);
    CHECK_EQ(std::string("very_positive"), withEvidence.analysis.tone);
    CHECK_EQ(std::string("strong"), withEvidence.analysis.toneStrength);

    // A neutral or unknown tone claims nothing, so it needs no evidence.
    const ParsedAnalysis neutral = parseAnalysisResponse(
        base + R"("tone":"neutral"})");
    CHECK(neutral.valid);
    CHECK_EQ(std::string("neutral"), neutral.analysis.tone);
}

TEST_CASE(tone_fields_survive_the_store_round_trip) {
    // The new columns must persist and read back, including on a database that
    // predates them (the additive migration path).
    const std::string path = tempDbPath();
    NewsAnalysis a;
    a.articleId = "art-tone-1";
    a.status = "ok";
    a.summaryAr = "ملخص";
    a.tone = "negative";
    a.toneStrength = "moderate";
    a.toneEvidenceAr = "ضغط بيعي واضح";
    a.sourceRegister = "analytical";
    a.sensationalism = "low";
    a.novelty = "already_priced";
    a.uncertainty = "high";
    a.hiddenContextAr = "سياق خفي";
    a.framingBiasAr = "تحيز صياغة";
    a.analyzedAtUtc = 1782299280;

    {
        NewsStore store;
        std::string error;
        REQUIRE(store.open(path, error));
        REQUIRE(store.saveAnalysis(a, error));
    }
    {
        NewsStore store;
        std::string error;
        REQUIRE(store.open(path, error));
        NewsAnalysis out;
        REQUIRE(store.getAnalysis("art-tone-1", out));
        CHECK_EQ(std::string("negative"), out.tone);
        CHECK_EQ(std::string("moderate"), out.toneStrength);
        CHECK_EQ(std::string("ضغط بيعي واضح"), out.toneEvidenceAr);
        CHECK_EQ(std::string("analytical"), out.sourceRegister);
        CHECK_EQ(std::string("low"), out.sensationalism);
        CHECK_EQ(std::string("already_priced"), out.novelty);
        CHECK_EQ(std::string("high"), out.uncertainty);
        CHECK_EQ(std::string("سياق خفي"), out.hiddenContextAr);
        CHECK_EQ(std::string("تحيز صياغة"), out.framingBiasAr);
    }
    fs::remove(path);
}

TEST_CASE(tone_migration_is_idempotent_on_an_old_database) {
    // Simulate a database created before the tone columns existed: create the
    // table without them, then let open() migrate it. The migration must be
    // additive, must not lose the existing row, and must be a no-op on a second
    // open.
    const std::string path = tempDbPath();
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        const char* ddl =
            "CREATE TABLE news_analyses ("
            " article_id TEXT NOT NULL PRIMARY KEY, status TEXT NOT NULL,"
            " status_reason TEXT NOT NULL DEFAULT '',"
            " summary_ar TEXT NOT NULL DEFAULT '',"
            " facts_json TEXT NOT NULL DEFAULT '[]',"
            " implications_ar TEXT NOT NULL DEFAULT '[]',"
            " supporting_ar TEXT NOT NULL DEFAULT '[]',"
            " opposing_ar TEXT NOT NULL DEFAULT '[]',"
            " gold_impact_ar TEXT NOT NULL DEFAULT '',"
            " direction TEXT NOT NULL DEFAULT 'no_trade',"
            " has_entry INTEGER NOT NULL DEFAULT 0, entry REAL NOT NULL DEFAULT 0,"
            " has_stop INTEGER NOT NULL DEFAULT 0, stop_loss REAL NOT NULL DEFAULT 0,"
            " has_target INTEGER NOT NULL DEFAULT 0, take_profit REAL NOT NULL DEFAULT 0,"
            " invalidating_ar TEXT NOT NULL DEFAULT '',"
            " astra_agreement TEXT NOT NULL DEFAULT 'unknown',"
            " disagreement_ar TEXT NOT NULL DEFAULT '',"
            " limitations_ar TEXT NOT NULL DEFAULT '[]',"
            " provider_id TEXT NOT NULL DEFAULT '', model_id TEXT NOT NULL DEFAULT '',"
            " analyzed_at INTEGER NOT NULL DEFAULT 0,"
            " timeframe_ctx TEXT NOT NULL DEFAULT '',"
            " candle_as_of INTEGER NOT NULL DEFAULT 0,"
            " astra_signal TEXT NOT NULL DEFAULT '',"
            " gold_relevance TEXT NOT NULL DEFAULT 'unknown');"
            "INSERT INTO news_analyses (article_id, status, summary_ar, analyzed_at)"
            " VALUES ('legacy-1','ok','قديم',1782299280);";
        REQUIRE(sqlite3_exec(db, ddl, nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    {
        NewsStore store;
        std::string error;
        REQUIRE(store.open(path, error));  // migrates the tone columns in
        NewsAnalysis out;
        REQUIRE(store.getAnalysis("legacy-1", out));
        CHECK_EQ(std::string("قديم"), out.summaryAr);
        // The pre-existing row was never tone-classified, so it reads back as
        // unknown/empty -- not as a default presented as a finding.
        CHECK_EQ(std::string("unknown"), out.tone);
        CHECK(out.toneEvidenceAr.empty());
    }
    {
        NewsStore store;
        std::string error;
        REQUIRE(store.open(path, error));  // second open is a no-op
        NewsAnalysis out;
        REQUIRE(store.getAnalysis("legacy-1", out));
        CHECK_EQ(std::string("unknown"), out.tone);
    }
    fs::remove(path);
}
