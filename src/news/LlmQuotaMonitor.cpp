// LLM7 daily-quota / rate-limit monitor implementation.

#include "news/LlmQuotaMonitor.h"

namespace aura {

void LlmQuotaMonitor::record(const LlmResult& result, std::int64_t nowUtc) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string category = result.category();

    if (result.ok()) {
        state_.category = "ok";
        state_.retryAfterSeconds = -1;
        state_.lastSuccessUtc = nowUtc;
        state_.lastChangeUtc = nowUtc;
        state_.coolingDown = false;
        state_.cooldownUntilUtc = 0;
        cooldownUntil_ = 0;
        return;
    }

    // Only update the reported category on a genuine change, so `lastChangeUtc`
    // reflects when the condition started rather than the last probe.
    if (state_.category != category) {
        state_.category = category;
        state_.lastChangeUtc = nowUtc;
    }
    state_.retryAfterSeconds = result.retryAfterSeconds;

    if (result.quotaExhausted()) {
        // Stop calling until the reset window. Use the provider's reset time
        // when given; otherwise a conservative fallback. Never invent a precise
        // time — retryAfterSeconds stays -1 when unknown.
        const int cooldown = result.retryAfterKnown() ? result.retryAfterSeconds
                                                      : fallbackCooldown_;
        cooldownUntil_ = nowUtc + cooldown;
        state_.coolingDown = true;
        state_.cooldownUntilUtc = cooldownUntil_;
    }
    // A rate limit or transport error does NOT open a cooldown: a single bounded
    // retry is allowed and the caller's own attempt cap bounds it.
}

bool LlmQuotaMonitor::shouldAttempt(std::int64_t nowUtc) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.coolingDown) return true;
    return nowUtc >= cooldownUntil_;
}

LlmQuotaState LlmQuotaMonitor::state(std::int64_t nowUtc) const {
    std::lock_guard<std::mutex> lock(mutex_);
    LlmQuotaState snapshot = state_;
    // The cooldown is derived from the clock, so recompute `coolingDown` here
    // rather than trusting a stale stored flag.
    if (snapshot.coolingDown && nowUtc >= cooldownUntil_) {
        snapshot.coolingDown = false;
    }
    return snapshot;
}

void LlmQuotaMonitor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.category = "ok";
    state_.retryAfterSeconds = -1;
    state_.coolingDown = false;
    state_.cooldownUntilUtc = 0;
    cooldownUntil_ = 0;
}

}  // namespace aura
