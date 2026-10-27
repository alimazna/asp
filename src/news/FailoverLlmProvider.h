#pragma once
// Ordered multi-provider LLM failover.
//
// The system is given several AI credentials (LLM7, Gemini, z.ai, Mistral...).
// Only one needs to work for analysis to proceed, and no single provider's
// outage, quota exhaustion, or rejected key may take the feature down. This
// adapter holds an ordered list of providers and tries them in turn.
//
// It implements ILlmProvider, so the news pipeline is unchanged and unaware that
// failover exists. Attribution stays honest: the returned LlmResult records which
// provider actually answered, and `providerId()` reports the primary — the
// pipeline stamps each analysis with the provider from the result, not from this
// object.
//
// Failover rules (deliberately conservative):
//   * A provider that answers Ok ends the chain immediately.
//   * A provider that is AuthFailure (bad/missing key), QuotaExhausted (daily
//     budget spent), TransportError, Timeout or ServerError is skipped in favour
//     of the next one. None of these is the caller's fault, so another provider
//     may well succeed.
//   * A BadRequest is also skipped in favour of the next provider: a 400 often
//     means "invalid model" for THAT provider, which the next one may serve.
//     Trying the rest is bounded by the chain length, so it is not a storm.
//   * Each provider uses its own configured model. The incoming request's model
//     field (which carries the primary's id) is cleared before each call, so one
//     provider's model name is never sent to another.
//   * The whole chain is bounded by the caller's own attempt budget; this class
//     never loops by itself, so it cannot become a retry storm.

#include "news/LlmProvider.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace aura {

// One entry in the failover chain.
struct FailoverEntry {
    ILlmProvider* provider = nullptr;  // borrowed; the owner outlives the chain
    std::string label;                 // honest provider id, e.g. "gemini"
};

// A per-provider tally of attempts and outcomes, for the activity log and for
// an operator diagnosing which credential is dead. Secret-free by construction.
struct FailoverStat {
    std::string label;
    std::int64_t attempts = 0;
    std::int64_t successes = 0;
    std::int64_t failures = 0;
    std::int64_t skippedAfterQuota = 0;
    std::string lastOutcome = "none";   // LlmResult::category() value
    std::int64_t lastSuccessUtc = 0;
};

class FailoverLlmProvider : public ILlmProvider {
public:
    // `entries` must be non-empty for the chain to do anything. A null provider
    // entry is ignored, so a misconfigured credential degrades to "try the rest"
    // rather than crashing.
    explicit FailoverLlmProvider(std::vector<FailoverEntry> entries);

    // Reports the primary provider's id so a caller that only logs identity
    // still sees something stable. Per-call attribution comes from LlmResult.
    std::string providerId() const override;
    std::string modelId() const override;

    LlmResult complete(const LlmRequest& request) override;

    // Test seam: how many providers were actually attempted by the last call.
    int lastProviderCount() const noexcept { return lastProviderCount_; }

    // Snapshot of per-provider tallies, in chain order.
    std::vector<FailoverStat> stats() const;

    // The provider label that answered the last successful call ("" if none).
    std::string lastSuccessProvider() const;

private:
    std::vector<FailoverEntry> entries_;
    mutable std::mutex mutex_;
    std::vector<FailoverStat> stats_;
    int lastProviderCount_ = 0;
    std::string lastSuccessProvider_;
};

}  // namespace aura
