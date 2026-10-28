// Gemini provider adapter tests.
//
// The transport is a scripted HTTP double, so the real GeminiProvider
// classification and 429 refinement run against realistic wire bodies. No
// external quota is consumed.

#include "TestHarness.h"

#include "foundation/OutboundHttpClient.h"
#include "news/GeminiProvider.h"
#include "news/LlmProvider.h"

#include <memory>
#include <string>

using namespace aura;

namespace {

class ScriptedTransport : public IOutboundHttpClient {
public:
    OutboundHttpResponse response;
    OutboundHttpRequest lastRequest;
    int calls = 0;

    OutboundHttpResponse send(const OutboundHttpRequest& request) override {
        ++calls;
        lastRequest = request;
        return response;
    }
};

OutboundHttpResponse httpStatus(int status, const std::string& body,
                                const std::string& retryAfter = "") {
    OutboundHttpResponse r;
    r.ok = true;
    r.statusCode = status;
    r.body = body;
    if (!retryAfter.empty()) r.headers.emplace_back("retry-after", retryAfter);
    return r;
}

GeminiConfig config() {
    GeminiConfig c;
    c.baseUrl = "https://example.invalid/v1beta";
    c.model = "gemini-flash-lite-latest";
    c.apiKey = "test-key";
    return c;
}

LlmRequest simpleRequest() {
    LlmRequest req;
    req.messages.push_back(LlmMessage{"user", "hi"});
    return req;
}

}  // namespace

TEST_CASE(gemini_429_plain_rate_limit_stays_rate_limited) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(
        429, "{\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\","
             "\"message\":\"Resource has been exhausted (e.g. check quota).\"}}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::RateLimited));
    CHECK_EQ(r.retryAfterSeconds, -1);
}

TEST_CASE(gemini_429_quota_exhausted_is_detected) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(
        429, "{\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\","
             "\"message\":\"You exceeded your current quota, please check your "
             "plan and billing details.\"}}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::QuotaExhausted));
}

TEST_CASE(gemini_retry_info_delay_is_parsed) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(
        429, "{\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\","
             "\"message\":\"Resource has been exhausted.\","
             "\"details\":[{\"@type\":\"type.googleapis.com/google.rpc.RetryInfo\","
             "\"retryDelay\":\"42.5s\"}]}}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::RateLimited));
    CHECK_EQ(r.retryAfterSeconds, 42);
}

TEST_CASE(gemini_retry_after_header_wins_over_body) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(
        429, "{\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\","
             "\"details\":[{\"retryDelay\":\"10s\"}]}}",
        "90");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(r.retryAfterSeconds, 90);
}

TEST_CASE(gemini_401_is_auth_failure) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(401, "{\"error\":{\"message\":\"bad key\"}}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::AuthFailure));
}

TEST_CASE(gemini_503_is_server_error) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(503, "{\"error\":{\"message\":\"overloaded\"}}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::ServerError));
}

TEST_CASE(gemini_missing_key_never_calls_transport) {
    auto transport = std::make_shared<ScriptedTransport>();
    GeminiConfig c = config();
    c.apiKey.clear();
    GeminiProvider provider(c, transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome),
             static_cast<int>(LlmOutcome::AuthFailure));
    CHECK_EQ(transport->calls, 0);
}

TEST_CASE(gemini_ok_body_is_extracted) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(
        200, "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hello\"}]}}]}");
    GeminiProvider provider(config(), transport);

    const LlmResult r = provider.complete(simpleRequest());
    CHECK_EQ(static_cast<int>(r.outcome), static_cast<int>(LlmOutcome::Ok));
    CHECK_EQ(r.content, std::string("hello"));
}

TEST_CASE(gemini_credential_is_never_in_the_url) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->response = httpStatus(200, "{\"candidates\":[]}");
    GeminiProvider provider(config(), transport);
    provider.complete(simpleRequest());

    // The key must travel in a header, never a query string that could be logged.
    CHECK(transport->lastRequest.url.find("test-key") == std::string::npos);
    bool sawHeader = false;
    for (const auto& h : transport->lastRequest.headers) {
        if (h == "X-goog-api-key: test-key") sawHeader = true;
    }
    CHECK(sawHeader);
}
