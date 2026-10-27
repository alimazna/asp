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
#include "news/NewsModels.h"
#include "news/NewsPrompt.h"

#include <functional>
#include <string>

namespace aura {

class NewsAnalysisService {
public:
    NewsAnalysisService(ILlmProvider* provider, int maxAttempts = 2)
        : provider_(provider), maxAttempts_(maxAttempts < 1 ? 1 : maxAttempts) {}

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
    int lastAttemptCount_ = 0;
};

}  // namespace aura
