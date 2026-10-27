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

        // A rejection does NOT end the chain. Observed in production: a provider
        // answers 400 "invalid model" for a model name it does not serve, which
        // is a per-provider condition the next provider may not share. Trying the
        // remaining providers is bounded by the chain length, so it cannot become
        // a retry storm.
        (void)0;
    }

    lastProviderCount_ = attempted;
    return last;
}

std::vector<FailoverStat> FailoverLlmProvider::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::string FailoverLlmProvider::lastSuccessProvider() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSuccessProvider_;
}

}  // namespace aura
