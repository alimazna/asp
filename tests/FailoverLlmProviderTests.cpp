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
        r.retryAfterSeconds = retryAfter;
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
    int retryAfter = -1;

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

// --- Chain health: the "which credentials are down" verdict ----------------

// A single exhausted provider in a three-provider chain must NOT read as "all
// down": the two never-tried providers may still answer, and reporting them as
// dead would hide a working credential.
TEST_CASE(health_one_exhausted_is_degraded_not_all_down) {
    RecordingProvider dead("gemini", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider idle1("zai", "m", LlmOutcome::Ok);
    RecordingProvider idle2("mistral", "m", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&dead, "gemini"},
                               FailoverEntry{&idle1, "zai"},
                               FailoverEntry{&idle2, "mistral"}});

    chain.complete(LlmRequest{});  // gemini exhausts, zai answers

    const ChainHealth h = chain.health();
    CHECK_EQ(h.configured, 3);
    CHECK_EQ(h.knownUnavailable, 1);
    CHECK(h.anyAvailable);
    CHECK(!h.allUnavailable);
    CHECK_EQ(h.summary, std::string("degraded"));
}

// Every provider out of daily budget is the case the user asked to be told
// about, and it must be distinguishable from a generic outage.
TEST_CASE(health_all_quota_exhausted) {
    RecordingProvider a("gemini", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider b("zai", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider c("mistral", "m", LlmOutcome::QuotaExhausted);
    FailoverLlmProvider chain({FailoverEntry{&a, "gemini"},
                               FailoverEntry{&b, "zai"},
                               FailoverEntry{&c, "mistral"}});

    const LlmResult r = chain.complete(LlmRequest{});
    CHECK(!r.ok());
    CHECK(r.quotaExhausted());

    const ChainHealth h = chain.health();
    CHECK_EQ(h.configured, 3);
    CHECK_EQ(h.quotaExhausted, 3);
    CHECK_EQ(h.knownUnavailable, 3);
    CHECK(h.allUnavailable);
    CHECK(h.allQuotaExhausted);
    CHECK_EQ(h.summary, std::string("all_quota_exhausted"));
}

// A chain where every provider rejects its key is "all unavailable" but NOT
// "all quota exhausted": the user must rotate credentials, not wait.
TEST_CASE(health_all_auth_failure_is_not_quota) {
    RecordingProvider a("gemini", "m", LlmOutcome::AuthFailure);
    RecordingProvider b("zai", "m", LlmOutcome::AuthFailure);
    FailoverLlmProvider chain({FailoverEntry{&a, "gemini"},
                               FailoverEntry{&b, "zai"}});

    chain.complete(LlmRequest{});

    const ChainHealth h = chain.health();
    CHECK(h.allUnavailable);
    CHECK(!h.allQuotaExhausted);
    CHECK_EQ(h.summary, std::string("all_unavailable"));
    CHECK_EQ(h.providers.size(), static_cast<std::size_t>(2));
    CHECK(h.providers[0].knownUnavailable);
    CHECK(!h.providers[0].quotaExhausted);
}

// Transient outcomes must NOT mark a provider down: the next call may succeed,
// and a "down" badge for a momentary 5xx would be dishonest.
TEST_CASE(health_transient_failure_stays_available) {
    RecordingProvider flaky("gemini", "m", LlmOutcome::ServerError);
    RecordingProvider live("zai", "m", LlmOutcome::Ok);
    FailoverLlmProvider chain({FailoverEntry{&flaky, "gemini"},
                               FailoverEntry{&live, "zai"}});

    chain.complete(LlmRequest{});

    const ChainHealth h = chain.health();
    CHECK_EQ(h.knownUnavailable, 0);
    CHECK(h.anyAvailable);
    CHECK_EQ(h.summary, std::string("ok"));
}

// An empty chain has no provider at all; the verdict must say so rather than
// claiming availability.
TEST_CASE(health_empty_chain_reports_no_provider) {
    FailoverLlmProvider chain({});
    const ChainHealth h = chain.health();
    CHECK_EQ(h.configured, 0);
    CHECK(!h.anyAvailable);
    CHECK_EQ(h.summary, std::string("no_provider"));
}

// The single-provider path (pinned mode / mock) derives the same verdict from
// one provider's last outcome, so the app sees one contract in both modes.
TEST_CASE(health_single_provider_path) {
    const ChainHealth down =
        FailoverLlmProvider::singleProvider("llm7", "quota_exhausted");
    CHECK(down.allUnavailable);
    CHECK(down.allQuotaExhausted);
    CHECK_EQ(down.summary, std::string("all_quota_exhausted"));

    const ChainHealth fresh =
        FailoverLlmProvider::singleProvider("gemini", "none");
    CHECK(fresh.anyAvailable);
    CHECK(!fresh.allUnavailable);
    CHECK_EQ(fresh.summary, std::string("ok"));

    const ChainHealth transient =
        FailoverLlmProvider::singleProvider("gemini", "timeout");
    CHECK(transient.anyAvailable);
    CHECK_EQ(transient.summary, std::string("ok"));
}

// When every provider fails, the cooldown must be the EARLIEST reported reset,
// not the last one: three providers each reporting an hour must not pause for
// three hours.
TEST_CASE(failover_all_fail_uses_earliest_reset) {
    RecordingProvider a("gemini", "m", LlmOutcome::QuotaExhausted);
    RecordingProvider b("zai", "m", LlmOutcome::QuotaExhausted);
    a.retryAfter = 3600;
    b.retryAfter = 120;
    FailoverLlmProvider chain({FailoverEntry{&a, "gemini"},
                               FailoverEntry{&b, "zai"}});

    const LlmResult r = chain.complete(LlmRequest{});
    CHECK(!r.ok());
    CHECK_EQ(r.retryAfterSeconds, 120);
}
