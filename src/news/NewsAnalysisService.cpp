// News-intelligence service implementation.

#include "news/NewsAnalysisService.h"

#include "news/NewsAnalysisParser.h"

#include <chrono>
#include <thread>

namespace aura {

namespace {

// The provider call, quota bookkeeping, bounded retry and bounded schema repair
// are identical for an article turn and a macro-event turn. Factoring them here
// keeps one implementation of the retry/repair contract, so the two paths
// cannot drift apart. `analysis` carries the stamped identity on entry and the
// validated fields on exit.
NewsAnalysis runAnalysisTurn(ILlmProvider* provider, int maxAttempts,
                             LlmQuotaMonitor* quotaMonitor,
                             const NewsPrompt& prompt, std::int64_t nowUtc,
                             NewsAnalysis analysis, int& attemptCount) {
    attemptCount = 0;
    // The null check must come first: dereferencing provider to stamp the
    // identity would crash before the guard could run.
    if (provider == nullptr) {
        analysis.status = "unavailable";
        analysis.statusReason = "no provider configured";
        return analysis;
    }

    analysis.providerId = provider->providerId();
    analysis.modelId = provider->modelId();

    // Daily-quota cooldown: when a previous call exhausted the provider's daily
    // budget, do not call again until the reset window. Serve the honest
    // "paused" state instead of hammering the provider.
    if (quotaMonitor != nullptr && !quotaMonitor->shouldAttempt(nowUtc)) {
        analysis.status = "unavailable";
        analysis.statusReason = "provider daily quota exhausted; cooling down";
        analysis.limitationsAr.push_back(
            "تم استهلاك حصة التحليل اليومية لمزوّد الذكاء الاصطناعي؛ "
            "سيُستأنف التحليل تلقائياً بعد تجدّد الحصة.");
        return analysis;
    }

    LlmRequest request;
    request.model = provider->modelId();
    request.messages = {{"system", prompt.system}, {"user", prompt.user}};
    // 0 means "no per-request override": the adapter then applies its configured
    // read timeout (LLM7_READ_TIMEOUT_MS). Leaving the struct default (30000)
    // here would silently shadow that configuration.
    request.timeoutMillis = 0;

    LlmResult result;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        attemptCount = attempt + 1;
        result = provider->complete(request);
        if (quotaMonitor != nullptr) {
            quotaMonitor->record(result, nowUtc);
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
    if (!parsed.valid && maxAttempts >= 2) {
        LlmRequest repairRequest;
        repairRequest.model = provider->modelId();
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
        LlmResult repairResult = provider->complete(repairRequest);
        if (quotaMonitor != nullptr) {
            quotaMonitor->record(repairResult, nowUtc);
        }
        ++attemptCount;
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

    // Carry the validated fields; identity/timestamps were stamped by the caller.
    NewsAnalysis a = parsed.analysis;
    a.articleId = analysis.articleId;
    a.analyzedAtUtc = analysis.analyzedAtUtc;
    a.timeframeContext = analysis.timeframeContext;
    // Attribute the analysis to the provider that actually answered, not to the
    // chain's primary. With failover configured those differ whenever an earlier
    // provider was skipped, and the record must name the real one.
    a.providerId = result.provider.empty() ? provider->providerId()
                                           : result.provider;
    a.modelId = result.model.empty() ? provider->modelId() : result.model;
    return a;
}

}  // namespace

NewsAnalysis NewsAnalysisService::analyze(const NewsArticle& article,
                                          const NewsPromptContext& context,
                                          std::int64_t nowUtc) {
    NewsAnalysis analysis;
    analysis.articleId = article.articleId;
    analysis.analyzedAtUtc = nowUtc;
    analysis.timeframeContext = context.timeframeContext;
    return runAnalysisTurn(provider_, maxAttempts_, quotaMonitor_,
                           buildNewsPrompt(context), nowUtc, std::move(analysis),
                           lastAttemptCount_);
}

NewsAnalysis NewsAnalysisService::analyzeEvent(const std::string& eventKey,
                                               EventTurn turn,
                                               const EventPromptContext& context,
                                               std::int64_t nowUtc) {
    NewsAnalysis analysis;
    // The event key is the record's identity, exactly as an article id is for
    // an article turn. It is what the store keys on.
    analysis.articleId = eventKey;
    analysis.analyzedAtUtc = nowUtc;
    analysis.timeframeContext = context.timeframeContext;
    return runAnalysisTurn(provider_, maxAttempts_, quotaMonitor_,
                           buildEventPrompt(turn, context), nowUtc,
                           std::move(analysis), lastAttemptCount_);
}

}  // namespace aura

