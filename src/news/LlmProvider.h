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
    RateLimited,      // 429 (temporary) — retryable after a short backoff
    QuotaExhausted,   // 429 with insufficient_quota / daily quota — NOT retryable;
                      // the caller must cool down until the quota resets
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
    // Parsed from the `Retry-After` header or the provider's `retry_after` body
    // field. -1 means "not provided / unknown" — never invent a reset time.
    int retryAfterSeconds = -1;

    bool retryable() const noexcept {
        return outcome == LlmOutcome::RateLimited ||
               outcome == LlmOutcome::Timeout ||
               outcome == LlmOutcome::ServerError ||
               outcome == LlmOutcome::TransportError;
    }
    bool ok() const noexcept { return outcome == LlmOutcome::Ok; }
    bool quotaExhausted() const noexcept {
        return outcome == LlmOutcome::QuotaExhausted;
    }
    bool retryAfterKnown() const noexcept { return retryAfterSeconds >= 0; }

    // Stable, non-secret category string for logs and the API. Never derived
    // from provider text, so it cannot leak response details.
    const char* category() const noexcept {
        switch (outcome) {
            case LlmOutcome::Ok: return "ok";
            case LlmOutcome::AuthFailure: return "auth_failure";
            case LlmOutcome::RateLimited: return "rate_limited";
            case LlmOutcome::QuotaExhausted: return "quota_exhausted";
            case LlmOutcome::Timeout: return "timeout";
            case LlmOutcome::BadRequest: return "bad_request";
            case LlmOutcome::ServerError: return "server_error";
            case LlmOutcome::TransportError: return "transport_error";
        }
        return "unknown";
    }
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;
    virtual std::string providerId() const = 0;
    virtual std::string modelId() const = 0;
    virtual LlmResult complete(const LlmRequest& request) = 0;
};

}  // namespace aura
