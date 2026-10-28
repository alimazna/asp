#pragma once
// Google Gemini provider adapter (native `generateContent` shape).
//
// Gemini is NOT OpenAI-compatible: it takes the credential in an `X-goog-api-key`
// header, uses `contents[].parts[].text` instead of `messages[].content`, calls
// the assistant role "model", and puts the system prompt in `systemInstruction`.
// A separate adapter is therefore required — pointing the OpenAI adapter at
// Gemini would send a body it does not understand.
//
// Everything else matches the contract of the OpenAI-compatible adapter: same
// ILlmProvider interface, same LlmOutcome classification, same "never log the
// credential" rule. The news pipeline cannot tell the two apart.

#include "foundation/OutboundHttpClient.h"
#include "news/LlmProvider.h"

#include <memory>
#include <string>

namespace aura {

struct GeminiConfig {
    std::string baseUrl = "https://generativelanguage.googleapis.com/v1beta";
    std::string model = "gemini-flash-lite-latest";
    std::string apiKey;   // never logged, never serialized
    std::string providerLabel = "gemini";
    int connectTimeoutMillis = 5000;
    int readTimeoutMillis = 45000;
    double temperature = 0.2;
    // Gemini supports a native JSON response mode. It is requested when true so
    // the model is far more likely to emit a bare object, but the pipeline still
    // validates defensively: this is a hint, never a guarantee.
    bool jsonMode = true;
};

class GeminiProvider : public ILlmProvider {
public:
    GeminiProvider(GeminiConfig config,
                   std::shared_ptr<IOutboundHttpClient> http);

    std::string providerId() const override { return config_.providerLabel; }
    std::string modelId() const override { return config_.model; }

    LlmResult complete(const LlmRequest& request) override;

    // Exposed for tests: build the exact JSON body sent to Gemini.
    std::string buildRequestBody(const LlmRequest& request) const;
    // Exposed for tests: the request URL for the configured model.
    std::string buildUrl() const;

    // Exposed for tests: HTTP status + transport result -> outcome.
    static LlmOutcome classify(bool transportOk, int status);

    // Exposed for tests: extract candidates[0].content.parts[*].text, joining
    // multiple parts. Returns false when the shape is not what we expect.
    static bool extractText(const std::string& body, std::string& out,
                            std::string& error);

    // Exposed for tests: parse an integer `Retry-After` header value. Returns -1
    // when absent or not an integer number of seconds; an HTTP-date is not
    // converted, because that would require clock reconciliation and could
    // report a precise reset the provider never gave us.
    static int parseRetryAfterHeader(const std::string& value);

    // Exposed for tests: does a 429 body indicate a spent quota rather than a
    // temporary rate limit? Only specific quota markers count, so a plain rate
    // limit is never mislabelled as exhaustion (which would wrongly cool the
    // provider down and hide it from the app).
    static bool isQuotaExhaustedBody(const std::string& body);

    // Exposed for tests: parse a `google.rpc.RetryInfo` `retryDelay` value
    // (e.g. "3.5s") into whole seconds. Returns -1 when absent or unparseable —
    // an unknown reset is reported as unknown, never guessed.
    static int parseRetryInfoSeconds(const std::string& body);

private:
    GeminiConfig config_;
    std::shared_ptr<IOutboundHttpClient> http_;
};

}  // namespace aura
