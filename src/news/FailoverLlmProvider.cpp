// Ordered multi-provider LLM failover implementation.

#include "news/FailoverLlmProvider.h"

namespace aura {

FailoverLlmProvider::FailoverLlmProvider(std::vector<FailoverEntry> entries)
    : entries_(std::move(entries)) {
    stats_.reserve(entries_.size());
    for (const auto& e : entries_) {
        FailoverStat s;
        s.label = e.label;
        stats_.push_back(std::move(s));
    }
}

std::string FailoverLlmProvider::providerId() const {
    for (const auto& e : entries_) {
        if (e.provider != nullptr) return e.provider->providerId();
    }
    return "none";
}

std::string FailoverLlmProvider::modelId() const {
    for (const auto& e : entries_) {
        if (e.provider != nullptr) return e.provider->modelId();
    }
    return "none";
}

LlmResult FailoverLlmProvider::complete(const LlmRequest& request) {
    LlmResult last;
    last.outcome = LlmOutcome::TransportError;
    last.error = "no LLM provider is configured";
    int attempted = 0;
    // The earliest reset any failed provider reported. When the whole chain
    // fails, the caller's cooldown should last until the FIRST provider might
    // recover, not until the last one does — otherwise a chain of three
    // providers each reporting an hour would pause for three hours.
    int minRetryAfter = -1;

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        ILlmProvider* provider = entries_[i].provider;
        if (provider == nullptr) continue;
        ++attempted;
        // Each provider owns its own model. The incoming request carries the
        // PRIMARY's model id (the pipeline stamps it from providerId/modelId),
        // and forwarding that verbatim makes every other provider reject the
        // call as an invalid model. Blanking it lets each adapter apply its own
        // configured model while leaving every other request field intact.
        LlmRequest scoped = request;
        scoped.model.clear();
        LlmResult result = provider->complete(scoped);
        // Stamp attribution here so the caller always knows who answered, even
        // if an adapter forgot to set it.
        if (result.provider.empty()) result.provider = entries_[i].label;
        if (result.model.empty()) result.model = provider->modelId();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            FailoverStat& s = stats_[i];
            ++s.attempts;
            s.lastOutcome = result.category();
            if (result.ok()) {
                ++s.successes;
            } else {
                ++s.failures;
                if (result.quotaExhausted()) ++s.skippedAfterQuota;
            }
        }

        if (result.ok()) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastSuccessProvider_ = entries_[i].label;
            stats_[i].lastSuccessUtc = 0;  // caller stamps time when it has it
            lastProviderCount_ = attempted;
            return result;
        }

        last = result;
        if (result.retryAfterKnown() &&
            (minRetryAfter < 0 || result.retryAfterSeconds < minRetryAfter)) {
            minRetryAfter = result.retryAfterSeconds;
        }

        // A rejection does NOT end the chain. Observed in production: a provider
        // answers 400 "invalid model" for a model name it does not serve, which
        // is a per-provider condition the next provider may not share. Trying the
        // remaining providers is bounded by the chain length, so it cannot become
        // a retry storm.
        (void)0;
    }

    // Every provider failed: report the earliest known recovery time so the
    // caller's cooldown is not the sum of each provider's window.
    if (minRetryAfter >= 0) last.retryAfterSeconds = minRetryAfter;
    lastProviderCount_ = attempted;
    return last;
}

std::vector<FailoverStat> FailoverLlmProvider::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// An outcome that retrying cannot clear. A rejected key, a spent daily budget
// and a request this provider will always reject are terminal for that
// provider; everything else (timeout, 5xx, transport, rate limit) may succeed
// on the next call and must NOT be reported as down.
bool FailoverLlmProvider::isHardUnavailable(const std::string& category) {
    return category == "auth_failure" || category == "quota_exhausted" ||
           category == "bad_request";
}

namespace {

// Fold per-provider outcomes into a whole-chain verdict. Never-tried providers
// ("none") count as available: they have not been proven dead, and claiming
// otherwise would hide a working credential.
ChainHealth foldHealth(const std::vector<ProviderHealth>& providers) {
    ChainHealth health;
    health.providers = providers;
    health.configured = static_cast<int>(providers.size());
    if (providers.empty()) {
        health.summary = "no_provider";
        return health;
    }
    for (const auto& p : providers) {
        if (p.knownUnavailable) ++health.knownUnavailable;
        if (p.quotaExhausted) ++health.quotaExhausted;
    }
    health.anyAvailable = health.knownUnavailable < health.configured;
    health.allUnavailable = health.knownUnavailable == health.configured;
    health.allQuotaExhausted =
        health.quotaExhausted == health.configured && health.configured > 0;

    if (health.allQuotaExhausted) {
        health.summary = "all_quota_exhausted";
    } else if (health.allUnavailable) {
        health.summary = "all_unavailable";
    } else if (health.knownUnavailable > 0) {
        health.summary = "degraded";
    } else {
        health.summary = "ok";
    }
    return health;
}

}  // namespace

ChainHealth FailoverLlmProvider::health() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProviderHealth> providers;
    providers.reserve(stats_.size());
    for (const auto& s : stats_) {
        ProviderHealth p;
        p.label = s.label;
        p.lastOutcome = s.lastOutcome;
        p.attempts = s.attempts;
        p.successes = s.successes;
        p.knownUnavailable = isHardUnavailable(s.lastOutcome);
        p.quotaExhausted = s.lastOutcome == "quota_exhausted";
        providers.push_back(std::move(p));
    }
    return foldHealth(providers);
}

ChainHealth FailoverLlmProvider::singleProvider(const std::string& label,
                                                const std::string& category) {
    ProviderHealth p;
    p.label = label;
    p.lastOutcome = category.empty() ? "none" : category;
    p.knownUnavailable = isHardUnavailable(p.lastOutcome);
    p.quotaExhausted = p.lastOutcome == "quota_exhausted";
    return foldHealth({p});
}

std::string FailoverLlmProvider::lastSuccessProvider() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSuccessProvider_;
}

}  // namespace aura
