#pragma once
// Provider-neutral LLM adapter contract.
//
// The news pipeline depends on this interface, never on a specific vendor, so
// the provider or model can be swapped through configuration. Failure classes
// are explicit so the caller can apply bounded retries only where they help and
// never retry an auth or schema failure.

#include <string>
#include <vector>

namespace aura {

struct LlmMessage {
    std::string role;      // "system" | "user" | "assistant"
    std::string content;
};

struct LlmRequest {
    std::string model;
    std::vector<LlmMessage> messages;
    double temperature = 0.2;
    int timeoutMillis = 30000;
};

enum class LlmOutcome {
    Ok,
    AuthFailure,      // 401/403 — credential problem, not retryable
    RateLimited,      // 429 — retryable after backoff
    Timeout,          // transport timeout — retryable
    BadRequest,       // 400/404/422 — request problem, not retryable
    ServerError,      // 5xx — retryable
    TransportError    // DNS/connection — retryable
};

struct LlmResult {
    LlmOutcome outcome = LlmOutcome::TransportError;
    int httpStatus = 0;
    std::string content;   // assistant text when outcome == Ok
    std::string error;     // safe, non-secret diagnostic

    bool retryable() const noexcept {
        return outcome == LlmOutcome::RateLimited ||
               outcome == LlmOutcome::Timeout ||
               outcome == LlmOutcome::ServerError ||
               outcome == LlmOutcome::TransportError;
    }
    bool ok() const noexcept { return outcome == LlmOutcome::Ok; }
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;
    virtual std::string providerId() const = 0;
    virtual std::string modelId() const = 0;
    virtual LlmResult complete(const LlmRequest& request) = 0;
};

}  // namespace aura
