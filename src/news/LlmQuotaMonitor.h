#pragma once
// LLM7 daily-quota / rate-limit monitor.
//
// The news-intelligence adapter talks to an external LLM whose budget can be
// spent for the day. Two distinct 429 conditions must be told apart, because
// they call for opposite behaviour:
//
//   * QuotaExhausted  — the daily budget is spent. Retrying is pointless (and
//                       wastes nothing but time); the service must cool down
//                       until the provider's reset window, and the user must be
//                       told the analysis is paused, not that it is broken.
//   * RateLimited     — a short-lived burst limit. A bounded backoff will
//                       usually succeed, so a normal retry is appropriate.
//
// This monitor records the outcome of each provider call and exposes a small,
// secret-free snapshot for the API and the Android app. It NEVER fabricates a
// reset time: when the provider does not tell us when the quota resets, the
// snapshot reports the reset as unknown and the UI says so.
//
// It is deliberately dependency-light and clock-injectable so its behaviour is
// fully deterministic under test.

#include "news/LlmProvider.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace aura {

// The provider-availability snapshot surfaced to the API and the app.
struct LlmQuotaState {
    // "ok"            — the last call succeeded (or no call has failed yet)
    // "rate_limited"  — a temporary burst limit; retrying soon is reasonable
    // "quota_exhausted" — the daily budget is spent; paused until reset
    // "auth_failure"  — the credential was rejected; needs operator action
    // "unavailable"   — a transport/server error; transient
    std::string category = "ok";

    // Seconds until the provider's window resets, when the provider told us.
    // -1 means "unknown" — the UI must not invent a countdown.
    int retryAfterSeconds = -1;

    // Epoch seconds of the last state change, and of the last successful call
    // (0 when none has succeeded yet). Honest timestamps, never synthesised.
    std::int64_t lastChangeUtc = 0;
    std::int64_t lastSuccessUtc = 0;

    // True while the monitor is withholding calls to avoid hammering a provider
    // whose daily budget is spent. The service still serves cached data.
    bool coolingDown = false;
    // Epoch seconds when the cooldown ends; 0 when not cooling down. When the
    // reset time is unknown, cooldown uses a conservative fallback and this
    // reports that fallback's end.
    std::int64_t cooldownUntilUtc = 0;

    bool quotaExhausted() const noexcept { return category == "quota_exhausted"; }
    bool rateLimited() const noexcept { return category == "rate_limited"; }
    bool ok() const noexcept { return category == "ok"; }
    bool retryAfterKnown() const noexcept { return retryAfterSeconds >= 0; }
};

class LlmQuotaMonitor {
public:
    // `fallbackCooldownSeconds` is used only when a quota-exhaustion response
    // omits a reset time: the monitor must still stop calling, but it never
    // claims a precise reset. Default is one hour, a deliberately conservative
    // lower bound for a daily window.
    explicit LlmQuotaMonitor(int fallbackCooldownSeconds = 3600)
        : fallbackCooldown_(fallbackCooldownSeconds < 1
                                ? 1
                                : fallbackCooldownSeconds) {}

    // Record the outcome of one provider call. `nowUtc` is epoch seconds.
    void record(const LlmResult& result, std::int64_t nowUtc);

    // True when a call may be attempted now: either no cooldown is active, or
    // the cooldown has elapsed. A quota exhaustion opens a cooldown; a rate
    // limit does not (a single bounded retry is allowed).
    bool shouldAttempt(std::int64_t nowUtc) const;

    // Current snapshot, copied under the lock.
    LlmQuotaState state(std::int64_t nowUtc) const;

    // Force-clear the cooldown (operator action after rotating a credential or
    // confirming the reset). Does not touch the success timestamp.
    void reset();

private:
    mutable std::mutex mutex_;
    int fallbackCooldown_;
    LlmQuotaState state_;
    std::int64_t cooldownUntil_ = 0;
};

}  // namespace aura
