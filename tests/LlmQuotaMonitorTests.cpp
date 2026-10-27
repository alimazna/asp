// TST-NEWS-0004 - LLM7 daily-quota monitoring and notification.
//
// Scope: src/news/LlmQuotaMonitor.*, the 429 refinement in Llm7Provider.*, and
// the Arabic quota messaging produced by NewsAnalysisService.*.
//
// The failure this suite pins: a 429 is NOT a single condition. LLM7 returns
// 429 both for a short burst limit (retry in seconds) and for a spent daily
// budget (retry tomorrow). Treating the second as retryable makes the service
// hammer a provider that cannot answer until the window resets; treating the
// first as permanent makes the app look broken for a few seconds. The tests
// below fix the boundary and the user-facing Arabic message on each side.
//
// Evidence for the body markers: live LLM7 probes on 2026-10-27 returned
// HTTP 429 with an `insufficient_quota` error type and a `retry_after` field
// when the daily budget was spent. These tests use that shape via a scripted
// transport so no external quota is consumed.

#include "TestHarness.h"

#include "foundation/OutboundHttpClient.h"
#include "news/Llm7Provider.h"
#include "news/LlmQuotaMonitor.h"
#include "news/MockLlmProvider.h"
#include "news/NewsAnalysisService.h"
#include "news/NewsPrompt.h"

#include <memory>
#include <string>
#include <vector>

using namespace aura;

namespace {

class ScriptedTransport : public IOutboundHttpClient {
public:
    std::vector<OutboundHttpResponse> responses;
    std::vector<OutboundHttpRequest> requests;

    OutboundHttpResponse send(const OutboundHttpRequest& request) override {
        requests.push_back(request);
        if (responses.empty()) return OutboundHttpResponse{};
        const std::size_t i =
            index_ < responses.size() ? index_ : responses.size() - 1;
        ++index_;
        return responses[i];
    }

private:
    std::size_t index_ = 0;
};

OutboundHttpResponse httpStatus(int status, const std::string& body,
                                const std::string& retryAfter = "") {
    OutboundHttpResponse r;
    r.ok = true;
    r.statusCode = status;
    r.body = body;
    if (!retryAfter.empty()) r.headers.emplace_back("retry-after", retryAfter);
    return r;
}

Llm7Provider makeProvider(std::shared_ptr<ScriptedTransport> transport,
                          const std::string& token = "test-token") {
    Llm7Config config;
    config.apiToken = token;
    return Llm7Provider(config, transport);
}

LlmRequest oneShot() {
    LlmRequest request;
    request.messages = {{"user", "hi"}};
    return request;
}

// The exact body LLM7 returns when the daily budget is spent. Captured from a
// live probe on 2026-10-27 (HTTP 429, `retry-after: 66401` header):
//   {"error":{"message":"Daily token quota exceeded. Retry after 66401
//    seconds.","type":"insufficient_quota","param":null,
//    "code":"quota_exceeded","retry_after":66401}}
const char* kQuotaBody =
    R"({"error":{"message":"Daily token quota exceeded. Retry after 66401 seconds.",)"
    R"("type":"insufficient_quota","param":null,"code":"quota_exceeded","retry_after":66401}})";

// A plain burst rate limit: 429 but retryable in seconds.
const char* kRateLimitBody =
    R"({"error":{"message":"Rate limit reached","type":"rate_limit_exceeded"}})";

NewsArticle article() {
    NewsArticle a;
    a.articleId = "art-1";
    a.source = "example.com";
    a.headline = "Gold rises after inflation data";
    a.dedupKey = newsDedupKey(a.headline);
    a.publishedAtUtc = 1792045800;
    a.collectedAtUtc = 1792045805;
    return a;
}

NewsPromptContext context() {
    NewsPromptContext ctx;
    ctx.headline = "Gold rises after inflation data";
    ctx.source = "example.com";
    ctx.bodyAvailable = true;
    ctx.bodyText = "Inflation slowed, lifting gold.";
    ctx.publishedAtIso = "2026-10-27T00:00:00Z";
    ctx.candleSummary = "M15: 2650.0 -> 2655.0";
    ctx.astraSummary = "{\"status\":\"ok\"}";
    ctx.timeframeContext = "M15,H4";
    return ctx;
}

}  // namespace

// ---------------------------------------------------------------------------
// 429 refinement: body markers decide quota-exhausted vs. temporary rate limit.
// ---------------------------------------------------------------------------

TEST_CASE(Quota_429WithInsufficientQuotaIsQuotaExhausted) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(429, kQuotaBody, "66401"));
    Llm7Provider provider = makeProvider(transport);
    const LlmResult r = provider.complete(oneShot());
    CHECK(r.outcome == LlmOutcome::QuotaExhausted);
    CHECK(r.quotaExhausted());
    CHECK(!r.retryable());
    CHECK_EQ(r.retryAfterSeconds, 66401);
    CHECK(std::string(r.category()) == "quota_exhausted");
}

TEST_CASE(Quota_429WithoutQuotaMarkersIsTemporaryRateLimit) {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses.push_back(httpStatus(429, kRateLimitBody, "15"));
    Llm7Provider provider = makeProvider(transport);
    const LlmResult r = provider.complete(oneShot());
    CHECK(r.outcome == LlmOutcome::RateLimited);
    CHECK(!r.quotaExhausted());
    CHECK(r.retryable());
    CHECK_EQ(r.retryAfterSeconds, 15);
    CHECK(std::string(r.category()) == "rate_limited");
}

TEST_CASE(Quota_DailyTokenQuotaMarkerIsDetected) {
    CHECK(Llm7Provider::isQuotaExhaustedBody(
        R"({"error":"Daily token quota exceeded"})"));
    CHECK(Llm7Provider::isQuotaExhaustedBody(R"({"type":"insufficient_quota"})"));
    CHECK(Llm7Provider::isQuotaExhaustedBody(R"({"code":"quota_exceeded"})"));
    // A generic rate-limit body must NOT be misread as exhaustion.
    CHECK(!Llm7Provider::isQuotaExhaustedBody(kRateLimitBody));
    CHECK(!Llm7Provider::isQuotaExhaustedBody(""));
}

TEST_CASE(Quota_RetryAfterParsesHeaderAndBody) {
    // Integer header wins.
    CHECK_EQ(Llm7Provider::parseRetryAfterSeconds("120", ""), 120);
    // Body `retry_after` used when the header is absent.
    CHECK_EQ(Llm7Provider::parseRetryAfterSeconds("", kQuotaBody), 66401);
    // Nested under error{}.
    CHECK_EQ(Llm7Provider::parseRetryAfterSeconds(
                 "", R"({"error":{"retry_after":42}})"),
             42);
    // Nothing reliable -> -1 (unknown), never a fabricated value.
    CHECK_EQ(Llm7Provider::parseRetryAfterSeconds("", kRateLimitBody), -1);
    CHECK_EQ(Llm7Provider::parseRetryAfterSeconds("Wed, 21 Oct 2026 07:28:00 GMT",
                                                  ""),
             -1);
}

// ---------------------------------------------------------------------------
// Monitor state machine.
// ---------------------------------------------------------------------------

TEST_CASE(Quota_MonitorOpensCooldownOnExhaustionAndRecoversAfterWindow) {
    LlmQuotaMonitor monitor(3600);
    LlmResult r;
    r.outcome = LlmOutcome::QuotaExhausted;
    r.httpStatus = 429;
    r.retryAfterSeconds = 600;

    monitor.record(r, /*nowUtc=*/1000);
    LlmQuotaState s = monitor.state(1000);
    CHECK(s.quotaExhausted());
    CHECK(s.coolingDown);
    CHECK_EQ(s.cooldownUntilUtc, 1600);
    CHECK(!monitor.shouldAttempt(1000));
    CHECK(!monitor.shouldAttempt(1599));
    // At the reset instant a call is allowed again.
    CHECK(monitor.shouldAttempt(1600));
    CHECK(!monitor.state(1600).coolingDown);
}

TEST_CASE(Quota_MonitorUsesFallbackWhenResetUnknownButReportsUnknown) {
    LlmQuotaMonitor monitor(/*fallbackCooldownSeconds=*/1800);
    LlmResult r;
    r.outcome = LlmOutcome::QuotaExhausted;
    r.httpStatus = 429;
    r.retryAfterSeconds = -1;  // provider did not say when

    monitor.record(r, 5000);
    LlmQuotaState s = monitor.state(5000);
    CHECK(s.coolingDown);
    CHECK(!s.retryAfterKnown());          // never invent a precise reset
    CHECK_EQ(s.retryAfterSeconds, -1);
    CHECK_EQ(s.cooldownUntilUtc, 6800);   // conservative fallback window
    CHECK(!monitor.shouldAttempt(6799));
    CHECK(monitor.shouldAttempt(6800));
}

TEST_CASE(Quota_TemporaryRateLimitDoesNotOpenCooldown) {
    LlmQuotaMonitor monitor(3600);
    LlmResult r;
    r.outcome = LlmOutcome::RateLimited;
    r.httpStatus = 429;
    r.retryAfterSeconds = 20;

    monitor.record(r, 1000);
    LlmQuotaState s = monitor.state(1000);
    CHECK(s.rateLimited());
    CHECK(!s.quotaExhausted());
    CHECK(!s.coolingDown);          // a bounded retry is still allowed
    CHECK(monitor.shouldAttempt(1000));
    CHECK_EQ(s.retryAfterSeconds, 20);
}

TEST_CASE(Quota_SuccessClearsCooldownAndStampsSuccess) {
    LlmQuotaMonitor monitor(3600);
    LlmResult bad;
    bad.outcome = LlmOutcome::QuotaExhausted;
    bad.httpStatus = 429;
    bad.retryAfterSeconds = 100;
    monitor.record(bad, 1000);
    CHECK(!monitor.shouldAttempt(1000));

    LlmResult good;
    good.outcome = LlmOutcome::Ok;
    good.httpStatus = 200;
    monitor.record(good, 2000);
    LlmQuotaState s = monitor.state(2000);
    CHECK(s.ok());
    CHECK(!s.coolingDown);
    CHECK_EQ(s.lastSuccessUtc, 2000);
    CHECK(monitor.shouldAttempt(2000));
}

TEST_CASE(Quota_AuthFailureIsReportedButNotCoolingDown) {
    LlmQuotaMonitor monitor(3600);
    LlmResult r;
    r.outcome = LlmOutcome::AuthFailure;
    r.httpStatus = 401;
    monitor.record(r, 1000);
    LlmQuotaState s = monitor.state(1000);
    CHECK(std::string(s.category) == "auth_failure");
    CHECK(!s.coolingDown);
}

TEST_CASE(Quota_LastChangeOnlyAdvancesOnCategoryChange) {
    LlmQuotaMonitor monitor(3600);
    LlmResult r;
    r.outcome = LlmOutcome::QuotaExhausted;
    r.httpStatus = 429;
    r.retryAfterSeconds = 100;
    monitor.record(r, 1000);
    CHECK_EQ(monitor.state(1000).lastChangeUtc, 1000);
    // Same condition again: the "since" timestamp must not move.
    monitor.record(r, 1100);
    CHECK_EQ(monitor.state(1100).lastChangeUtc, 1000);
}

// ---------------------------------------------------------------------------
// Service behaviour: cooldown prevents provider calls; Arabic messaging.
// ---------------------------------------------------------------------------

TEST_CASE(Quota_ServiceCoolsDownWithoutCallingProviderAgain) {
    MockLlmProvider provider(MockLlmProvider::Mode::QuotaExhausted);
    LlmQuotaMonitor monitor(3600);
    NewsAnalysisService service(&provider, /*maxAttempts=*/2, &monitor);

    // First call: provider returns quota exhaustion, monitor opens cooldown.
    NewsAnalysis first = service.analyze(article(), context(), 1000);
    CHECK(first.status == "unavailable");
    CHECK(!monitor.shouldAttempt(1000));

    // Second call within the cooldown: the service must NOT call the provider.
    NewsAnalysis second = service.analyze(article(), context(), 1001);
    CHECK(second.status == "unavailable");
    CHECK_EQ(service.lastAttemptCount(), 0);  // no provider attempt was made
    CHECK(!second.limitationsAr.empty());
    // The Arabic message tells the user it is paused, not broken.
    CHECK(second.limitationsAr.front().find("الحصة") != std::string::npos);
}

TEST_CASE(Quota_ServiceRecoversWhenWindowElapses) {
    // The mock reports retry_after=3600 on quota exhaustion, so the cooldown is
    // exactly that window; the fallback is NOT used when a reset is supplied.
    MockLlmProvider provider(MockLlmProvider::Mode::QuotaExhausted);
    LlmQuotaMonitor monitor(/*fallback=*/100);
    NewsAnalysisService service(&provider, 2, &monitor);

    service.analyze(article(), context(), 1000);
    CHECK_EQ(service.lastAttemptCount(), 1);   // one attempt, then cooldown
    service.analyze(article(), context(), 1050);
    CHECK_EQ(service.lastAttemptCount(), 0);   // still cooling down
    service.analyze(article(), context(), 4599);
    CHECK_EQ(service.lastAttemptCount(), 0);   // just before the reset
    service.analyze(article(), context(), 4600);  // window elapsed -> retry
    CHECK(service.lastAttemptCount() >= 1);
}

TEST_CASE(Quota_ServiceRateLimitYieldsRetryMessage) {
    MockLlmProvider provider(MockLlmProvider::Mode::RateLimited);
    LlmQuotaMonitor monitor(3600);
    NewsAnalysisService service(&provider, /*maxAttempts=*/1, &monitor);
    NewsAnalysis an = service.analyze(article(), context(), 1000);
    CHECK(an.status == "unavailable");
    CHECK(!an.limitationsAr.empty());
    CHECK(an.limitationsAr.front().find("مشغول") != std::string::npos);
    // A temporary rate limit must not open a quota cooldown.
    CHECK(monitor.shouldAttempt(1000));
}

TEST_CASE(Quota_ServiceWithoutMonitorStillWorks) {
    // The monitor is optional; the service must behave exactly as before.
    MockLlmProvider provider(MockLlmProvider::Mode::Valid);
    NewsAnalysisService service(&provider, 2);
    NewsAnalysis an = service.analyze(article(), context(), 1000);
    CHECK(an.status == "ok");
    CHECK(an.proposedDirection == "bullish");
}
