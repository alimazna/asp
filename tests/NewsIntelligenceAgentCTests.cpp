// TST-NEWS-0003 - AYZR Agent-C: LLM7 integration verification + failure
// injection for the news-intelligence analysis path.
//
// Scope: src/news/NewsAnalysisService.*, NewsPrompt.*, NewsAnalysisParser.*
// and the LLM7 adapter they depend on. Read-only with respect to ASTRA: the
// service reads a prompt context and returns a validated analysis; it never
// modifies a decision and never executes a trade.
//
// Two kinds of evidence here:
//   1. Contract verification against the *official* LLM7 surface
//      (base URL https://api.llm7.io/v1, model nemotron-3-nano:30b,
//      OpenAI-compatible POST /v1/chat/completions). Live probes on
//      2026-10-27 confirmed: the model id is advertised by GET /v1/models,
//      and `response_format: {"type":"json_object"}` is rejected with HTTP 400
//      `unsupported_model_feature` ("does not support JSON mode"). The adapter
//      therefore must NOT send response_format, and output must be parsed
//      defensively. These tests pin that contract so it cannot silently drift.
//   2. Failure injection at the network boundary via a deterministic scripted
//      transport (auth failure, rate limit, timeout, server error, malformed
//      output, empty content) plus missing-input and conflicting-evidence
//      cases. No external quota is consumed.
//
// The scripted transport is the only test double; it sits at the HTTP boundary
// (IOutboundHttpClient), so the real Llm7Provider classification, the real
// parser and the real service retry policy all execute.

#include "TestHarness.h"

#include "api/BackendApiSchema.h"  // jsonString (build realistic wire bodies)
#include "foundation/Json.h"
#include "foundation/OutboundHttpClient.h"
#include "news/Llm7Provider.h"
#include "news/MockLlmProvider.h"
#include "news/NewsAnalysisParser.h"
#include "news/NewsAnalysisService.h"
#include "news/NewsPrompt.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aura;

namespace {

// Deterministic transport. Returns the scripted responses in order; once the
// script is exhausted the final entry repeats, so a single-entry script models
// "every attempt fails the same way".
class ScriptedTransport : public IOutboundHttpClient {
public:
    std::vector<OutboundHttpResponse> responses;
    std::vector<OutboundHttpRequest> requests;

    OutboundHttpResponse send(const OutboundHttpRequest& request) override {
        requests.push_back(request);
        if (responses.empty()) return OutboundHttpResponse{};
        const std::size_t i =
            index_ < responses.size() ? index_ : responses.size() - 1;
        ++index_;
        return responses[i];
    }

private:
    std::size_t index_ = 0;
};

OutboundHttpResponse httpStatus(int status, const std::string& body) {
    OutboundHttpResponse r;
    r.ok = true;
    r.statusCode = status;
    r.body = body;
    return r;
}

OutboundHttpResponse transportFailure(const std::string& error) {
    OutboundHttpResponse r;
    r.ok = false;
    r.error = error;
    return r;
}

// A realistic OpenAI-compatible 200 body wrapping `content`.
OutboundHttpResponse chatCompletion(const std::string& content) {
    OutboundHttpResponse r;
    r.ok = true;
    r.statusCode = 200;
    r.body = std::string("{\"id\":\"chatcmpl_x\",\"choices\":[{\"index\":0,") +
             "\"message\":{\"role\":\"assistant\",\"content\":" +
             jsonString(content) + "}}]}";
    return r;
}

Llm7Provider makeProvider(std::shared_ptr<ScriptedTransport> transport,
                          const std::string& token = "test-token") {
    Llm7Config config;
    config.apiToken = token;
    return Llm7Provider(config, transport);
}

LlmRequest oneShotRequest() {
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    return request;
}

NewsArticle article(const std::string& id, const std::string& headline) {
    NewsArticle a;
    a.articleId = id;
    a.source = "example.com";
    a.headline = headline;
    a.dedupKey = newsDedupKey(headline);
    a.publishedAtUtc = 1792045800;
    a.collectedAtUtc = 1792045805;
    return a;
}

NewsPromptContext contextFor(const NewsArticle& a) {
    NewsPromptContext ctx;
    ctx.headline = a.headline;
    ctx.source = a.source;
    ctx.publishedAtIso = "2026-10-27T12:00:00Z";
    ctx.timeframeContext = "M15,H4";
    return ctx;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ===========================================================================
// Section 1 - LLM7 integration contract (official surface)
// ===========================================================================

// Base URL and model are the documented ones, and the request targets the
// OpenAI-compatible /chat/completions endpoint.
TEST_CASE(llm7_defaults_match_official_surface) {
    Llm7Config config;
    CHECK_EQ(config.baseUrl, std::string("https://api.llm7.io/v1"));
    CHECK_EQ(config.model, std::string("nemotron-3-nano:30b"));

    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion("ok"));
    Llm7Provider provider = makeProvider(transport);
    CHECK_EQ(provider.providerId(), std::string("llm7"));
    CHECK_EQ(provider.modelId(), std::string("nemotron-3-nano:30b"));

    const LlmResult result = provider.complete(oneShotRequest());
    REQUIRE(result.ok());
    REQUIRE(transport->requests.size() == static_cast<std::size_t>(1));
    CHECK_EQ(transport->requests[0].url,
             std::string("https://api.llm7.io/v1/chat/completions"));
    CHECK_EQ(transport->requests[0].method, std::string("POST"));
}

// The body is OpenAI-compatible and NEVER enables JSON mode: the documented
// model rejects response_format, so the adapter must not send it.
TEST_CASE(llm7_request_body_is_openai_compatible_without_json_mode) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion("ok"));
    Llm7Provider provider = makeProvider(transport);
    LlmRequest request = oneShotRequest();
    request.messages = {{"system", "sys"}, {"user", "usr"}};
    REQUIRE(provider.complete(request).ok());

    const std::string body = transport->requests[0].body;
    CHECK(contains(body, "\"model\":\"nemotron-3-nano:30b\""));
    CHECK(contains(body, "\"messages\""));
    CHECK(contains(body, "\"role\":\"system\""));
    CHECK(contains(body, "\"stream\":false"));
    // JSON mode is unsupported by the model; requesting it would be a 400.
    CHECK(!contains(body, "response_format"));
    CHECK(!contains(body, "json_object"));
    CHECK(!contains(body, "json_schema"));
    // The credential is a header, never the body.
    CHECK(!contains(body, "test-token"));
    CHECK(!contains(body, "Authorization"));
}

// The bearer credential is presented as an Authorization header.
TEST_CASE(llm7_sends_bearer_authorization_header) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion("ok"));
    Llm7Provider provider = makeProvider(transport, "secret-token-value");
    REQUIRE(provider.complete(oneShotRequest()).ok());

    bool sawBearer = false;
    for (const auto& header : transport->requests[0].headers) {
        if (header == "Authorization: Bearer secret-token-value") sawBearer = true;
    }
    CHECK(sawBearer);
    // The credential never leaks into the body.
    CHECK(!contains(transport->requests[0].body, "secret-token-value"));
}

// ===========================================================================
// Section 2 - HTTP status classification (failure classes)
// ===========================================================================

TEST_CASE(llm7_auth_failures_are_not_retryable) {
    for (const int status : {401, 403}) {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(httpStatus(status, "{\"error\":\"nope\"}"));
        Llm7Provider provider = makeProvider(transport);
        const LlmResult result = provider.complete(oneShotRequest());
        CHECK(result.outcome == LlmOutcome::AuthFailure);
        CHECK_EQ(result.httpStatus, status);
        CHECK(!result.retryable());
        CHECK(!result.ok());
        CHECK(!result.error.empty());
    }
}

TEST_CASE(llm7_rate_limit_is_retryable) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(429, "{\"error\":\"slow down\"}"));
    Llm7Provider provider = makeProvider(transport);
    const LlmResult result = provider.complete(oneShotRequest());
    CHECK(result.outcome == LlmOutcome::RateLimited);
    CHECK(result.retryable());
}

TEST_CASE(llm7_server_errors_are_retryable) {
    for (const int status : {500, 502, 503}) {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(httpStatus(status, "{\"error\":\"oops\"}"));
        Llm7Provider provider = makeProvider(transport);
        const LlmResult result = provider.complete(oneShotRequest());
        CHECK(result.outcome == LlmOutcome::ServerError);
        CHECK(result.retryable());
    }
}

TEST_CASE(llm7_timeout_status_and_transport_failure_are_retryable) {
    // 408/504 classify as Timeout (retryable).
    for (const int status : {408, 504}) {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(httpStatus(status, ""));
        Llm7Provider provider = makeProvider(transport);
        const LlmResult result = provider.complete(oneShotRequest());
        CHECK(result.outcome == LlmOutcome::Timeout);
        CHECK(result.retryable());
    }
    // A transport-level timeout (curl error, no HTTP response) is also Timeout.
    {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(transportFailure("Operation timed out"));
        Llm7Provider provider = makeProvider(transport);
        const LlmResult result = provider.complete(oneShotRequest());
        CHECK(result.outcome == LlmOutcome::TransportError);
        CHECK(result.retryable());
        CHECK_EQ(result.error, std::string("Operation timed out"));
    }
}

// A missing credential fails closed WITHOUT contacting the provider.
TEST_CASE(llm7_missing_token_never_calls_transport) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion("should not be used"));
    Llm7Provider provider = makeProvider(transport, "");
    const LlmResult result = provider.complete(oneShotRequest());
    CHECK(result.outcome == LlmOutcome::AuthFailure);
    CHECK(!result.retryable());
    CHECK_EQ(transport->requests.size(), static_cast<std::size_t>(0));
}

// ===========================================================================
// Section 3 - Malformed / structural provider responses
// ===========================================================================

TEST_CASE(llm7_malformed_200_body_is_bad_request) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(200, "{ this is not json"));
    Llm7Provider provider = makeProvider(transport);
    const LlmResult result = provider.complete(oneShotRequest());
    CHECK(result.outcome == LlmOutcome::BadRequest);
    CHECK(!result.ok());
    CHECK(!result.retryable());
}

TEST_CASE(llm7_missing_or_empty_choices_is_bad_request) {
    for (const std::string& body : {std::string("{\"id\":\"x\"}"),
                                    std::string("{\"choices\":[]}"),
                                    std::string("{\"choices\":\"nope\"}")}) {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(httpStatus(200, body));
        Llm7Provider provider = makeProvider(transport);
        const LlmResult result = provider.complete(oneShotRequest());
        CHECK(result.outcome == LlmOutcome::BadRequest);
    }
}

TEST_CASE(llm7_empty_message_content_is_bad_request) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion(""));
    Llm7Provider provider = makeProvider(transport);
    const LlmResult result = provider.complete(oneShotRequest());
    CHECK(result.outcome == LlmOutcome::BadRequest);
    CHECK(!result.ok());
}

// An error diagnostic must never echo the credential.
TEST_CASE(llm7_error_never_leaks_credential) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(
        httpStatus(401, "{\"error\":\"invalid api key\"}"));
    Llm7Provider provider = makeProvider(transport, "top-secret-token");
    const LlmResult result = provider.complete(oneShotRequest());
    CHECK(result.outcome == LlmOutcome::AuthFailure);
    CHECK(!contains(result.error, "top-secret-token"));
    CHECK(!contains(result.error, "Authorization"));
}

// The configured default model is used when the request carries none.
TEST_CASE(llm7_default_model_used_when_request_model_empty) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion("ok"));
    Llm7Provider provider = makeProvider(transport);
    LlmRequest request;  // request.model empty
    request.messages = {{"user", "hi"}};
    REQUIRE(provider.complete(request).ok());
    CHECK(contains(transport->requests[0].body,
                   "\"model\":\"nemotron-3-nano:30b\""));
}

// ===========================================================================
// Section 4 - Service failure states and bounded retries
// ===========================================================================

TEST_CASE(service_null_provider_is_unavailable_not_a_crash) {
    NewsAnalysisService service(nullptr, 2);
    const NewsArticle a = article("n1", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("unavailable"));
    CHECK_EQ(out.statusReason, std::string("no provider configured"));
    CHECK_EQ(service.lastAttemptCount(), 0);
    CHECK(out.summaryAr.empty());
    CHECK(!out.hasEntry);
}

TEST_CASE(service_auth_failure_is_unavailable_without_retry) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(401, "{\"error\":\"bad key\"}"));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 3);
    const NewsArticle a = article("n2", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("unavailable"));
    CHECK_EQ(service.lastAttemptCount(), 1);  // auth is not retried
    CHECK_EQ(transport->requests.size(), static_cast<std::size_t>(1));
    CHECK(!out.limitationsAr.empty());
    CHECK(out.summaryAr.empty());
}

TEST_CASE(service_rate_limit_is_retried_then_succeeds) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(429, "{\"error\":\"slow\"}"));
    transport->responses.push_back(chatCompletion(MockLlmProvider::validPayload()));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 2);
    const NewsArticle a = article("n3", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("ok"));
    CHECK_EQ(service.lastAttemptCount(), 2);
    CHECK_EQ(out.proposedDirection, std::string("bullish"));
    CHECK_EQ(out.providerId, std::string("llm7"));
    CHECK_EQ(out.modelId, std::string("nemotron-3-nano:30b"));
    CHECK_EQ(out.articleId, std::string("n3"));
    CHECK_EQ(out.analyzedAtUtc, static_cast<std::int64_t>(100));
}

TEST_CASE(service_timeout_exhausts_bounded_attempts) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(transportFailure("Operation timed out"));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 3);
    const NewsArticle a = article("n4", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("unavailable"));
    CHECK_EQ(service.lastAttemptCount(), 3);  // exactly maxAttempts, no storm
    CHECK_EQ(transport->requests.size(), static_cast<std::size_t>(3));
    CHECK(!out.limitationsAr.empty());
}

TEST_CASE(service_malformed_model_output_is_invalid_not_a_recommendation) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(
        chatCompletion("Sure! Here is the analysis: { not valid json"));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 1);
    const NewsArticle a = article("n5", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("invalid_model_output"));
    CHECK(out.summaryAr.empty());
    CHECK(!out.hasEntry);
    CHECK(!out.hasStop);
    CHECK(!out.hasTarget);
    CHECK_EQ(out.proposedDirection, std::string("no_trade"));
    CHECK(!out.limitationsAr.empty());
}

TEST_CASE(service_empty_model_content_is_unavailable) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion(""));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 2);
    const NewsArticle a = article("n6", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(out.status, std::string("unavailable"));
    CHECK(out.summaryAr.empty());
}

// ===========================================================================
// Section 5 - Missing inputs are declared, never invented
// ===========================================================================

TEST_CASE(prompt_declares_missing_body_and_missing_market_data) {
    const NewsArticle a = article("n7", "Fed holds rates");
    NewsPromptContext ctx = contextFor(a);
    ctx.bodyAvailable = false;  // no article body
    ctx.candleSummary.clear();  // no candles available
    ctx.astraSummary.clear();   // no ASTRA analysis available
    const NewsPrompt prompt = buildNewsPrompt(ctx);

    CHECK(contains(prompt.system, "SHADOW-ONLY"));
    CHECK(contains(prompt.system, "Never invent"));
    CHECK(contains(prompt.system, "no_trade"));
    // The user message must flag each missing input explicitly.
    CHECK(contains(prompt.user, "Body: UNAVAILABLE"));
    CHECK(contains(prompt.user, "UNAVAILABLE"));  // candles
    CHECK(contains(prompt.user, "EXISTING ASTRA ANALYSIS"));
    CHECK(contains(prompt.user, "Fed holds rates"));
    CHECK(contains(prompt.user, "example.com"));
}

TEST_CASE(prompt_includes_real_candle_and_astra_context_when_present) {
    const NewsArticle a = article("n8", "CPI cools");
    NewsPromptContext ctx = contextFor(a);
    ctx.bodyAvailable = true;
    ctx.bodyText = "CPI came in at 2.9%.";
    ctx.candleSummary = "M15 (2 closed bars): t=1 O=2650 H=2655 L=2648 C=2653";
    ctx.astraSummary = "{\"status\":\"ok\",\"regime\":\"QUIET\"}";
    const NewsPrompt prompt = buildNewsPrompt(ctx);
    CHECK(contains(prompt.user, "CPI came in at 2.9%."));
    CHECK(contains(prompt.user, "M15 (2 closed bars)"));
    CHECK(contains(prompt.user, "regime"));
    CHECK(contains(prompt.user, "M15,H4"));
}

// Missing candles/body must not stop a valid analysis from being produced; the
// model is still instructed to state the limitation.
TEST_CASE(service_missing_inputs_still_yields_valid_analysis) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion(MockLlmProvider::validPayload()));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 1);
    const NewsArticle a = article("n9", "Headline only");
    NewsPromptContext ctx = contextFor(a);  // no body, no candles, no ASTRA
    const NewsAnalysis out = service.analyze(a, ctx, 100);
    CHECK_EQ(out.status, std::string("ok"));
    // The prompt sent to the provider flagged the missing inputs.
    REQUIRE(transport->requests.size() == static_cast<std::size_t>(1));
    CHECK(contains(transport->requests[0].body, "UNAVAILABLE"));
}

// ===========================================================================
// Section 6 - Conflicting evidence
// ===========================================================================

// Contradictory evidence is preserved on both sides and the proposal stays
// no_trade; levels are dropped because no order exists.
TEST_CASE(service_conflicting_evidence_stays_no_trade) {
    const std::string payload =
        R"({"summary_ar":"إشارات متعارضة.",)"
        R"("direction":"no_trade","astra_agreement":"disagree",)"
        R"("supporting_evidence_ar":["بيانات ضعيفة تدعم الذهب"],)"
        R"("opposing_evidence_ar":["اتجاه H4 هابط"],)"
        R"("entry":2650,"stop_loss":2640,"take_profit":2680,)"
        R"("disagreement_note_ar":"تحليل أسترا يخالف الخبر."})";
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(chatCompletion(payload));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 1);
    const NewsArticle a = article("n10", "Mixed signals");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);

    CHECK_EQ(out.status, std::string("ok"));
    CHECK_EQ(out.proposedDirection, std::string("no_trade"));
    CHECK_EQ(out.astraAgreement, std::string("disagree"));
    CHECK_EQ(out.supportingEvidenceAr.size(), static_cast<std::size_t>(1));
    CHECK_EQ(out.opposingEvidenceAr.size(), static_cast<std::size_t>(1));
    // A no_trade proposal must never carry trade levels.
    CHECK(!out.hasEntry);
    CHECK(!out.hasStop);
    CHECK(!out.hasTarget);
    CHECK(!out.disagreementNoteAr.empty());
}

// The parser keeps contradictory arrays intact rather than collapsing them.
TEST_CASE(parser_preserves_conflicting_evidence_arrays) {
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"sideways","astra_agreement":"unknown",)"
        R"("supporting_evidence_ar":["دعم"],"opposing_evidence_ar":["معارضة"]})");
    REQUIRE(parsed.valid);
    CHECK_EQ(parsed.analysis.supportingEvidenceAr.size(),
             static_cast<std::size_t>(1));
    CHECK_EQ(parsed.analysis.opposingEvidenceAr.size(),
             static_cast<std::size_t>(1));
}

// ===========================================================================
// Section 7 - Parser defensive edge cases
// ===========================================================================

TEST_CASE(parser_rejects_non_object_and_prose_only_output) {
    CHECK(!parseAnalysisResponse("hello world, no json here").valid);
    CHECK(!parseAnalysisResponse("[1, 2, 3]").valid);          // array, not object
    CHECK(!parseAnalysisResponse("{\"a\": 1}").valid);         // missing summary_ar
    CHECK(!parseAnalysisResponse("").valid);
}

TEST_CASE(parser_rejects_non_numeric_json_number) {
    // `NaN`/`Infinity` are not valid JSON numbers; the parser must reject the
    // object rather than coerce a level from it.
    CHECK(!parseAnalysisResponse(
               R"({"summary_ar":"س","direction":"bullish","entry":NaN})")
               .valid);
}

TEST_CASE(parser_drops_non_finite_level_even_when_json_valid) {
    // 1e400 parses as a JSON number but overflows to infinity: the finite guard
    // must drop the level, and the analysis stays valid without a level.
    const ParsedAnalysis parsed = parseAnalysisResponse(
        R"({"summary_ar":"س","direction":"bullish","astra_agreement":"unknown",)"
        R"("entry":1e400})");
    REQUIRE(parsed.valid);
    CHECK(!parsed.analysis.hasEntry);
}

TEST_CASE(parser_drops_levels_for_non_actionable_direction) {
    for (const std::string& direction : {std::string("no_trade"),
                                         std::string("sideways")}) {
        const ParsedAnalysis parsed = parseAnalysisResponse(
            "{\"summary_ar\":\"س\",\"direction\":\"" + direction +
            "\",\"astra_agreement\":\"unknown\",\"entry\":2650,"
            "\"stop_loss\":2640,\"take_profit\":2680}");
        REQUIRE(parsed.valid);
        CHECK(!parsed.analysis.hasEntry);
        CHECK(!parsed.analysis.hasStop);
        CHECK(!parsed.analysis.hasTarget);
    }
}

// ===========================================================================
// Section 8 - Read-only contract with respect to ASTRA
// ===========================================================================

// analyze() must not mutate its inputs and must return a fresh analysis per
// call. The service has no handle on a decision, a candle store or a broker;
// this pins that it cannot reach through its arguments to change ASTRA state.
TEST_CASE(service_is_read_only_and_returns_fresh_analyses) {
    MockLlmProvider provider(MockLlmProvider::Mode::Valid);
    NewsAnalysisService service(&provider, 1);
    const NewsArticle a = article("n11", "CPI cools");
    const NewsPromptContext ctx = contextFor(a);

    const NewsAnalysis first = service.analyze(a, ctx, 100);
    const NewsAnalysis second = service.analyze(a, ctx, 200);
    CHECK_EQ(first.status, std::string("ok"));
    CHECK_EQ(second.status, std::string("ok"));
    // Independent results: a later call must not overwrite an earlier one.
    CHECK_EQ(first.analyzedAtUtc, static_cast<std::int64_t>(100));
    CHECK_EQ(second.analyzedAtUtc, static_cast<std::int64_t>(200));
    // Inputs are unchanged (article identity + prompt context are const refs).
    CHECK_EQ(a.articleId, std::string("n11"));
    CHECK_EQ(ctx.timeframeContext, std::string("M15,H4"));
    CHECK_EQ(ctx.bodyAvailable, false);
}

// A run of mixed retryable failures is still bounded by maxAttempts.
TEST_CASE(service_mixed_retryable_failures_are_bounded) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(429, "{\"error\":\"slow\"}"));
    transport->responses.push_back(httpStatus(503, "{\"error\":\"oops\"}"));
    transport->responses.push_back(transportFailure("Operation timed out"));
    transport->responses.push_back(chatCompletion(MockLlmProvider::validPayload()));
    Llm7Provider provider = makeProvider(transport);
    NewsAnalysisService service(&provider, 2);  // cap below the failure count
    const NewsArticle a = article("n12", "Headline");
    const NewsAnalysis out = service.analyze(a, contextFor(a), 100);
    CHECK_EQ(service.lastAttemptCount(), 2);
    CHECK_EQ(transport->requests.size(), static_cast<std::size_t>(2));
    // Capped out before the success: honest `unavailable`, no partial analysis.
    CHECK_EQ(out.status, std::string("unavailable"));
    CHECK(out.summaryAr.empty());
}

// The extractor takes the FIRST balanced object. If the model emits prose
// containing braces before the real object, extraction fails and the result is
// an honest `invalid` rather than a mis-parsed analysis. This pins that the
// parser never falls through to a later object by guessing.
TEST_CASE(parser_fails_honestly_when_prose_brace_precedes_object) {
    const std::string text =
        "Thinking {about rates} ... {\"summary_ar\":\"س\",\"direction\":"
        "\"bullish\",\"entry\":2650}";
    std::string extracted;
    REQUIRE(extractFirstJsonObject(text, extracted));
    CHECK_EQ(extracted, std::string("{about rates}"));
    const ParsedAnalysis parsed = parseAnalysisResponse(text);
    CHECK(!parsed.valid);
    CHECK(parsed.analysis.summaryAr.empty());
    CHECK(!parsed.analysis.hasEntry);
}

// ===========================================================================
// Section 9 - Configured read timeout is honoured (not shadowed)
// ===========================================================================

namespace {

// Records the exact LlmRequest the service builds.
class CapturingProvider : public ILlmProvider {
public:
    std::vector<LlmRequest> requests;
    std::string providerId() const override { return "capture"; }
    std::string modelId() const override { return "capture-model"; }
    LlmResult complete(const LlmRequest& request) override {
        requests.push_back(request);
        LlmResult r;
        r.outcome = LlmOutcome::Ok;
        r.httpStatus = 200;
        r.content = MockLlmProvider::validPayload();
        return r;
    }
};

}  // namespace

// The service must not send a per-request timeout that shadows the adapter's
// configured read timeout (LLM7_READ_TIMEOUT_MS). A struct-default 30000 here
// silently capped every live call at 30s regardless of configuration.
TEST_CASE(service_does_not_shadow_configured_read_timeout) {
    CapturingProvider provider;
    NewsAnalysisService service(&provider, 1);
    const NewsArticle a = article("n13", "Headline");
    REQUIRE(service.analyze(a, contextFor(a), 100).status ==
               std::string("ok"));
    REQUIRE(provider.requests.size() == static_cast<std::size_t>(1));
    // 0 == "use the adapter's configured default".
    CHECK_EQ(provider.requests[0].timeoutMillis, 0);
}

// The adapter applies config.readTimeoutMillis when the request carries no
// override, and lets a non-zero request value win.
TEST_CASE(llm7_read_timeout_precedence) {
    // No override (0) -> configured value is used.
    {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(chatCompletion("ok"));
        Llm7Config config;
        config.apiToken = "t";
        config.readTimeoutMillis = 45000;
        Llm7Provider provider(config, transport);
        LlmRequest request = oneShotRequest();
        request.timeoutMillis = 0;
        REQUIRE(provider.complete(request).ok());
        CHECK_EQ(transport->requests[0].readTimeoutMillis, 45000);
    }
    // Explicit override wins over the configured value.
    {
        auto transport = std::make_shared<ScriptedTransport>();
        transport->responses.push_back(chatCompletion("ok"));
        Llm7Config config;
        config.apiToken = "t";
        config.readTimeoutMillis = 45000;
        Llm7Provider provider(config, transport);
        LlmRequest request = oneShotRequest();
        request.timeoutMillis = 7000;
        REQUIRE(provider.complete(request).ok());
        CHECK_EQ(transport->requests[0].readTimeoutMillis, 7000);
    }
}

// Extract the balanced object even when prose and fences surround it.
TEST_CASE(parser_extracts_balanced_object_around_braces_in_strings) {
    std::string extracted;
    const std::string text =
        "prefix ```json {\"summary_ar\":\"قيمة {غير} صحيحة\",\"direction\":"
        "\"no_trade\"} ``` suffix";
    REQUIRE(extractFirstJsonObject(text, extracted));
    CHECK_EQ(extracted,
             std::string("{\"summary_ar\":\"قيمة {غير} صحيحة\",\"direction\":"
                         "\"no_trade\"}"));
    const ParsedAnalysis parsed = parseAnalysisResponse(text);
    CHECK(parsed.valid);
    CHECK_EQ(parsed.analysis.proposedDirection, std::string("no_trade"));
}
