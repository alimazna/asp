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

    // Exposed for tests: classify an HTTP status + transport result.
    static LlmOutcome classify(bool transportOk, int status);

private:
    Llm7Config config_;
    std::shared_ptr<IOutboundHttpClient> http_;
};

}  // namespace aura
