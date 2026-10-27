#pragma once
// Deterministic in-process LLM provider for tests and offline development.
//
// Returns a fixed, schema-valid analysis JSON so the full news pipeline can be
// exercised without spending the user's external API quota. It also supports
// scripted failure injection (timeout, rate limit, malformed output) so failure
// paths are testable deterministically.

#include "news/LlmProvider.h"

#include <string>

namespace aura {

class MockLlmProvider : public ILlmProvider {
public:
    enum class Mode { Valid, Malformed, Empty, Timeout, RateLimited, AuthFailure,
                      // 429 with a spent daily budget: NOT retryable; the caller
                      // must cool down until the reset window.
                      QuotaExhausted,
                      // First call returns schema-invalid output, subsequent calls
                      // return a valid payload. Models the repair turn.
                      MalformedThenValid };

    explicit MockLlmProvider(Mode mode = Mode::Valid) : mode_(mode) {}

    // Map a config string to a mode. Unknown values fall back to Valid so a
    // typo in a deployment setting can never silently enable failure injection.
    static Mode modeFromString(const std::string& value) {
        if (value == "malformed") return Mode::Malformed;
        if (value == "empty") return Mode::Empty;
        if (value == "timeout") return Mode::Timeout;
        if (value == "rate_limited") return Mode::RateLimited;
        if (value == "quota_exhausted") return Mode::QuotaExhausted;
        if (value == "auth_failure") return Mode::AuthFailure;
        if (value == "malformed_then_valid") return Mode::MalformedThenValid;
        return Mode::Valid;
    }

    std::string providerId() const override { return "mock"; }
    std::string modelId() const override { return "mock-model"; }

    // Test seam: the messages of the most recent complete() call, so a test can
    // assert the repair turn replays the conversation (assistant + user).
    const std::vector<LlmMessage>& lastMessages() const { return lastMessages_; }

    LlmResult complete(const LlmRequest& req) override {
        lastMessages_ = req.messages;
        LlmResult result;
        switch (mode_) {
            case Mode::Valid:
                result.outcome = LlmOutcome::Ok;
                result.httpStatus = 200;
                result.content = validPayload();
                break;
            case Mode::Malformed:
                result.outcome = LlmOutcome::Ok;
                result.httpStatus = 200;
                result.content = "Sure! Here is the analysis: { not valid json ";
                break;
            case Mode::Empty:
                result.outcome = LlmOutcome::Ok;
                result.httpStatus = 200;
                result.content = "";
                break;
            case Mode::Timeout:
                result.outcome = LlmOutcome::Timeout;
                result.error = "injected timeout";
                break;
            case Mode::RateLimited:
                result.outcome = LlmOutcome::RateLimited;
                result.httpStatus = 429;
                result.error = "injected rate limit";
                result.retryAfterSeconds = 20;
                break;
            case Mode::QuotaExhausted:
                result.outcome = LlmOutcome::QuotaExhausted;
                result.httpStatus = 429;
                result.error = "injected daily quota exhaustion";
                // A real provider may or may not supply a reset time; the mock
                // reports one so the monitor can be tested both ways via the
                // monitor's own unit tests.
                result.retryAfterSeconds = 3600;
                break;
            case Mode::AuthFailure:
                result.outcome = LlmOutcome::AuthFailure;
                result.httpStatus = 401;
                result.error = "injected auth failure";
                break;
            case Mode::MalformedThenValid:
                result.outcome = LlmOutcome::Ok;
                result.httpStatus = 200;
                if (callCount_++ == 0) {
                    // Valid JSON that violates the schema (direction missing):
                    // the same failure the real model produced.
                    result.content = R"({"summary_ar":"ملخص","facts":[{"text":"حقيقة"}]})";
                } else {
                    result.content = validPayload();
                }
                break;
        }
        return result;
    }

    // A valid payload that passes parseAnalysisResponse unchanged.
    static std::string validPayload() {
        return R"({"summary_ar":"ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع.",)"
               R"("facts":[{"text":"صدر مؤشر أسعار المستهلك أقل من التوقعات","kind":"fact"}],)"
               R"("market_implications_ar":["ضعف الدولار يدعم الذهب"],)"
               R"("supporting_evidence_ar":["H4 اتجاه صاعد"],)"
               R"("opposing_evidence_ar":["زخم M15 ضعيف"],)"
               R"("gold_impact_ar":"إيجابي على المدى القصير","gold_relevance":"high",)"
               R"("direction":"bullish","entry":2650.5,"stop_loss":2640.0,)"
               R"("take_profit":2680.0,"astra_agreement":"agree",)"
               R"("invalidating_conditions_ar":"إغلاق M15 دون 2640",)"
               R"("limitations_ar":["لا تتوفر بيانات التضخم التفصيلية"]})";
    }

private:
    Mode mode_;
    int callCount_ = 0;
    std::vector<LlmMessage> lastMessages_;
};

}  // namespace aura
