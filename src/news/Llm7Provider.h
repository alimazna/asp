#pragma once
// LLM7 provider adapter (OpenAI-compatible chat completions).
//
// Isolated from business logic: the news service depends only on ILlmProvider.
// The adapter owns request shaping, HTTP status -> LlmOutcome classification,
// and extraction of the assistant message. It never logs the Authorization
// header or the request body.

#include "foundation/OutboundHttpClient.h"
#include "news/LlmProvider.h"

#include <memory>
#include <string>

namespace aura {

struct Llm7Config {
    std::string baseUrl = "https://api.llm7.io/v1";
    std::string model = "nemotron-3-nano:30b";
    std::string apiToken;   // never logged, never serialized
    int connectTimeoutMillis = 5000;
    int readTimeoutMillis = 45000;
    // Optional extra knobs kept explicit so a provider swap stays config-only.
    double temperature = 0.2;
};

class Llm7Provider : public ILlmProvider {
public:
    Llm7Provider(Llm7Config config,
                 std::shared_ptr<IOutboundHttpClient> http);

    std::string providerId() const override { return "llm7"; }
    std::string modelId() const override { return config_.model; }

    LlmResult complete(const LlmRequest& request) override;

    // Exposed for tests: build the exact JSON body sent to the provider.
    std::string buildRequestBody(const LlmRequest& request) const;

    // Exposed for tests: classify an HTTP status + transport result. A plain
    // 429 maps to RateLimited; use classifyRateLimitBody() to refine a 429 into
    // QuotaExhausted when the body indicates the daily quota is spent.
    static LlmOutcome classify(bool transportOk, int status);

    // Exposed for tests: decide whether a 429 response means the daily quota is
    // exhausted (not retryable) rather than a temporary rate limit (retryable).
    // `body` is the provider response body; it is inspected for the documented
    // `insufficient_quota` / `quota_exceeded` / "daily token quota" markers.
    static bool isQuotaExhaustedBody(const std::string& body);

    // Exposed for tests: parse a retry delay in seconds from a `Retry-After`
    // header value (integer seconds or an HTTP-date) or, failing that, a
    // `retry_after` numeric field in the JSON body. Returns -1 when no reliable
    // value is present — the caller must then report the reset time as unknown.
    static int parseRetryAfterSeconds(const std::string& retryAfterHeader,
                                      const std::string& body);

private:
    Llm7Config config_;
    std::shared_ptr<IOutboundHttpClient> http_;
};

}  // namespace aura
