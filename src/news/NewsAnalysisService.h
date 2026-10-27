#pragma once
// News-intelligence service: turns an article plus real market context into a
// validated, evidence-separated analysis. This is a READ-ONLY analysis layer;
// it never modifies ASTRA's decisions and never executes anything.
//
// Failure handling:
//   * a provider failure yields status "unavailable" with a classified reason;
//   * a schema violation yields status "invalid_model_output";
//   * retryable failures are retried at most `maxAttempts` times with a bounded
//     delay, so the service cannot produce a retry storm.
//
// The service depends only on ILlmProvider, so tests inject MockLlmProvider.

#include "news/LlmProvider.h"
#include "news/LlmQuotaMonitor.h"
#include "news/NewsModels.h"
#include "news/NewsPrompt.h"

#include <functional>
#include <string>

namespace aura {

class NewsAnalysisService {
public:
    // `quotaMonitor` is optional: when supplied, the service records every
    // provider outcome and refuses to call the provider while a daily-quota
    // cooldown is active, so a spent budget is not hammered. It is a raw
    // pointer because the monitor is owned by the server and outlives the
    // service.
    NewsAnalysisService(ILlmProvider* provider, int maxAttempts = 2,
                        LlmQuotaMonitor* quotaMonitor = nullptr)
        : provider_(provider),
          maxAttempts_(maxAttempts < 1 ? 1 : maxAttempts),
          quotaMonitor_(quotaMonitor) {}

    // Analyse one article. `context` carries the real candle + ASTRA data the
    // caller assembled from the backend (never raw DB or MT5 access).
    NewsAnalysis analyze(const NewsArticle& article,
                         const NewsPromptContext& context,
                         std::int64_t nowUtc);

    // Test seam: how many attempts the last analyze() used.
    int lastAttemptCount() const noexcept { return lastAttemptCount_; }

private:
    ILlmProvider* provider_;
    int maxAttempts_;
    LlmQuotaMonitor* quotaMonitor_;
    int lastAttemptCount_ = 0;
};

}  // namespace aura
