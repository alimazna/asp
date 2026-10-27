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
