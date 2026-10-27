// News-intelligence service implementation.

#include "news/NewsAnalysisService.h"

#include "news/NewsAnalysisParser.h"

#include <chrono>
#include <thread>

namespace aura {

NewsAnalysis NewsAnalysisService::analyze(const NewsArticle& article,
                                          const NewsPromptContext& context,
                                          std::int64_t nowUtc) {
    NewsAnalysis analysis;
    analysis.articleId = article.articleId;
    analysis.analyzedAtUtc = nowUtc;
    analysis.timeframeContext = context.timeframeContext;

    lastAttemptCount_ = 0;
    // The null check must come first: dereferencing provider_ to stamp the
    // identity would crash before the guard below could run.
    if (provider_ == nullptr) {
        analysis.status = "unavailable";
        analysis.statusReason = "no provider configured";
        return analysis;
    }

    analysis.providerId = provider_->providerId();
    analysis.modelId = provider_->modelId();

    // Daily-quota cooldown: when a previous call exhausted the provider's daily
    // budget, do not call again until the reset window. Serve the honest
    // "paused" state instead of hammering the provider.
    if (quotaMonitor_ != nullptr && !quotaMonitor_->shouldAttempt(nowUtc)) {
        lastAttemptCount_ = 0;
        analysis.status = "unavailable";
        analysis.statusReason = "provider daily quota exhausted; cooling down";
        analysis.limitationsAr.push_back(
            "تم استهلاك حصة التحليل اليومية لمزوّد الذكاء الاصطناعي؛ "
            "سيُستأنف التحليل تلقائياً بعد تجدّد الحصة.");
        return analysis;
    }

    const NewsPrompt prompt = buildNewsPrompt(context);
    LlmRequest request;
    request.model = provider_->modelId();
    request.messages = {{"system", prompt.system}, {"user", prompt.user}};
    // 0 means "no per-request override": the adapter then applies its configured
    // read timeout (LLM7_READ_TIMEOUT_MS). Leaving the struct default (30000)
    // here would silently shadow that configuration.
    request.timeoutMillis = 0;

    LlmResult result;
    for (int attempt = 0; attempt < maxAttempts_; ++attempt) {
        lastAttemptCount_ = attempt + 1;
        result = provider_->complete(request);
        if (quotaMonitor_ != nullptr) {
            quotaMonitor_->record(result, nowUtc);
        }
        if (result.ok() || !result.retryable()) break;
        // Bounded backoff. A short fixed delay keeps a single analysis request
        // from hammering a rate-limited provider; the attempt cap bounds it.
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * (attempt + 1)));
    }

    if (!result.ok()) {
        analysis.status = "unavailable";
        if (result.quotaExhausted()) {
            analysis.statusReason = "provider daily quota exhausted";
            analysis.limitationsAr.push_back(
                "تم استهلاك حصة التحليل اليومية لمزوّد الذكاء الاصطناعي؛ "
                "سيُستأنف التحليل تلقائياً بعد تجدّد الحصة.");
        } else if (result.outcome == LlmOutcome::RateLimited) {
            analysis.statusReason = "provider rate limited; retry shortly";
            analysis.limitationsAr.push_back(
                "مزوّد التحليل مشغول مؤقتاً؛ أعد المحاولة بعد لحظات.");
        } else {
            analysis.statusReason = result.error.empty()
                                        ? "provider unavailable"
                                        : ("provider unavailable: " + result.error);
            analysis.limitationsAr.push_back(
                "لم يتوفر تحليل الذكاء الاصطناعي لهذا الخبر.");
        }
        return analysis;
    }

    ParsedAnalysis parsed = parseAnalysisResponse(result.content);

    // Bounded schema repair. `nemotron-3-nano:30b` has no JSON mode, so a schema
    // miss on the first turn is expected rather than exceptional. One corrective
    // turn — echoing the invalid reply and the parser's reason — fixes most
    // cases. It is only attempted when the caller allowed more than one attempt,
    // so it shares the existing retry budget and cannot become a retry storm.
    if (!parsed.valid && maxAttempts_ >= 2) {
        LlmRequest repairRequest;
        repairRequest.model = provider_->modelId();
        // The repair must replay the conversation and append the model's own
        // invalid reply as an assistant turn, then the corrective user turn.
        // Verified against nemotron-3-nano:30b: inlining the reply into a single
        // user message does NOT recover a valid object, but this turn sequence
        // does. The schema is unchanged.
        repairRequest.messages = {{"system", prompt.system},
                                  {"user", prompt.user},
                                  {"assistant", result.content},
                                  {"user", buildRepairUserMessage(parsed.error)}};
        repairRequest.timeoutMillis = 0;
        LlmResult repairResult = provider_->complete(repairRequest);
        if (quotaMonitor_ != nullptr) {
            quotaMonitor_->record(repairResult, nowUtc);
        }
        ++lastAttemptCount_;
        if (repairResult.ok()) {
            parsed = parseAnalysisResponse(repairResult.content);
        }
    }

    if (!parsed.valid) {
        analysis.status = "invalid_model_output";
        analysis.statusReason = parsed.error;
        analysis.limitationsAr.push_back(
            "استجابة النموذج غير صالحة ولم يتم اعتماد أي سيناريو.");
        return analysis;
    }

    // Carry the validated fields; identity/timestamps were stamped above.
    NewsAnalysis& a = parsed.analysis;
    a.articleId = article.articleId;
    a.analyzedAtUtc = nowUtc;
    a.providerId = provider_->providerId();
    a.modelId = provider_->modelId();
    a.timeframeContext = context.timeframeContext;
    return a;
}

}  // namespace aura
