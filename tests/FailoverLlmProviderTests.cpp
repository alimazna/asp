// Failover LLM provider tests.
//
// Two behaviours are regression-guarded here because both were found by running
// the real providers, not by inspection:
//   1. the primary's model id must NOT be forwarded to every provider — doing so
//      made Mistral reject the call with "Invalid model: nemotron-3-nano:30b";
//   2. a per-provider BadRequest must NOT abort the chain — one provider
//      answering 400 for a model it does not serve says nothing about the next.

#include "TestHarness.h"

#include "news/FailoverLlmProvider.h"
#include "news/LlmProvider.h"

#include <string>
#include <vector>

using namespace aura;

namespace {

// Records the model id it was asked for, so a test can prove the chain does not
// leak the primary's model into a sibling provider.
class RecordingProvider : public ILlmProvider {
public:
    RecordingProvider(std::string id, std::string model, LlmOutcome outcome)
        : id_(std::move(id)), model_(std::move(model)), outcome_(outcome) {}

    std::string providerId() const override { return id_; }
    std::string modelId() const override { return model_; }

    LlmResult complete(const LlmRequest& request) override {
        ++calls;
        lastModel = request.model;
        LlmResult r;
        r.outcome = outcome_;
        r.provider = id_;
        r.model = model_;
        if (outcome_ != LlmOutcome::Ok) {
            r.error = "scripted " + std::string(categoryName());
        }
        return r;
    }

    const char* categoryName() const {
        switch (outcome_) {
            case LlmOutcome::AuthFailure: return "auth_failure";
            case LlmOutcome::QuotaExhausted: return "quota_exhausted";
            case LlmOutcome::BadRequest: return "bad_request";
            case LlmOutcome::ServerError: return "server_error";
            default: return "error";
        }
    }

    int calls = 0;
    std::string lastModel;

private:
    std::string id_;
    std::string model_;
    LlmOutcome outcome_;
};

}  // namespace

// The chain must reach a working provider when the primary is out of quota.
TEST_CASE(failover_skips_exhausted_primary) {
    RecordingProvider dead("llm7", "nemotron-3-nano:30b",
                           LlmOutcome::QuotaExhausted);
    RecordingProvider live("gemini", "gemini-flash-lite-latest", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&dead, "llm7"},
                               FailoverEntry{&live, "gemini"}});

    LlmRequest request;
    request.model = "nemotron-3-nano:30b";  // the primary's model
    const LlmResult result = chain.complete(request);

    CHECK(result.ok());
    CHECK_EQ(result.provider, std::string("gemini"));
    CHECK_EQ(dead.calls, 1);
    CHECK_EQ(live.calls, 1);
    CHECK_EQ(chain.lastProviderCount(), 2);
}

// Regression: the primary's model id must not be forwarded to another provider.
TEST_CASE(failover_does_not_leak_primary_model) {
    RecordingProvider dead("llm7", "nemotron-3-nano:30b",
                           LlmOutcome::QuotaExhausted);
    RecordingProvider live("mistral", "open-mistral-nemo", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&dead, "llm7"},
                               FailoverEntry{&live, "mistral"}});

    LlmRequest request;
    request.model = "nemotron-3-nano:30b";
    request.messages = {{"user", "analyse"}};
    chain.complete(request);

    // Each provider saw a cleared model, so its own adapter applies its own.
    CHECK_EQ(dead.lastModel, std::string(""));
    CHECK_EQ(live.lastModel, std::string(""));
}

// Regression: a 400 from one provider must not stop the chain.
TEST_CASE(failover_continues_after_bad_request) {
    RecordingProvider rejected("mistral", "open-mistral-nemo",
                               LlmOutcome::BadRequest);
    RecordingProvider live("gemini", "gemini-flash-lite-latest", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&rejected, "mistral"},
                               FailoverEntry{&live, "gemini"}});

    LlmRequest request;
    const LlmResult result = chain.complete(request);

    CHECK(result.ok());
    CHECK_EQ(result.provider, std::string("gemini"));
    CHECK_EQ(rejected.calls, 1);
    CHECK_EQ(live.calls, 1);
}

// Every provider failing yields the LAST failure, so the reported reason is the
// most recent one rather than an arbitrary earlier one.
TEST_CASE(failover_all_fail_reports_last) {
    RecordingProvider a("llm7", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider b("gemini", "m", LlmOutcome::ServerError);
    FailoverLlmProvider chain({FailoverEntry{&a, "llm7"},
                               FailoverEntry{&b, "gemini"}});

    LlmRequest request;
    const LlmResult result = chain.complete(request);

    CHECK(!result.ok());
    CHECK(result.outcome == LlmOutcome::ServerError);
    CHECK_EQ(chain.lastProviderCount(), 2);
}

// A null entry is skipped rather than dereferenced.
TEST_CASE(failover_skips_null_entry) {
    RecordingProvider live("gemini", "m", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{nullptr, "empty"},
                               FailoverEntry{&live, "gemini"}});

    LlmRequest request;
    const LlmResult result = chain.complete(request);

    CHECK(result.ok());
    CHECK_EQ(chain.lastProviderCount(), 1);
}

// Stats expose per-provider counts without any key material.
TEST_CASE(failover_stats_track_outcomes) {
    RecordingProvider dead("llm7", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider live("gemini", "m", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&dead, "llm7"},
                               FailoverEntry{&live, "gemini"}});

    LlmRequest request;
    chain.complete(request);
    const std::vector<FailoverStat> stats = chain.stats();

    CHECK_EQ(stats.size(), static_cast<std::size_t>(2));
    CHECK_EQ(stats[0].label, std::string("llm7"));
    CHECK_EQ(stats[0].attempts, static_cast<std::int64_t>(1));
    CHECK_EQ(stats[0].skippedAfterQuota, static_cast<std::int64_t>(1));
    CHECK_EQ(stats[0].lastOutcome, std::string("quota_exhausted"));
    CHECK_EQ(stats[1].successes, static_cast<std::int64_t>(1));
    CHECK_EQ(stats[1].lastOutcome, std::string("ok"));
}
