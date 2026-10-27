// Activity-log and provider-status API response builders.

#include "api/ActivityApi.h"

namespace aura {
namespace {

ApiField rawField(const std::string& name, const std::string& json) {
    return ApiField{name, json, true};
}

ApiField stringField(const std::string& name, const std::string& value) {
    return ApiField{name, jsonString(value), true};
}

ApiField intField(const std::string& name, std::int64_t value) {
    return ApiField{name, jsonInteger(value), true};
}

}  // namespace

ApiResponse ActivityApi::list(const std::vector<ActivityEntry>& entries,
                              const ActivityLog::Tally& tally) {
    std::vector<std::string> elements;
    elements.reserve(entries.size());
    for (const auto& e : entries) {
        std::vector<ApiField> f;
        f.push_back(intField("id", e.id));
        f.push_back(intField("at", e.atUtc));
        f.push_back(stringField("kind", e.kind));
        f.push_back(stringField("outcome", e.outcome));
        f.push_back(stringField("reason", e.reason));
        f.push_back(stringField("source", e.source));
        f.push_back(stringField("detail", e.detail));
        f.push_back(intField("item_count", e.itemCount));
        f.push_back(intField("duration_ms", e.durationMs));
        elements.push_back(jsonObject(f));
    }
    std::vector<ApiField> totals;
    totals.push_back(intField("ok", tally.ok));
    totals.push_back(intField("empty", tally.empty));
    totals.push_back(intField("rejected", tally.rejected));
    totals.push_back(intField("unavailable", tally.unavailable));
    totals.push_back(intField("error", tally.error));
    totals.push_back(intField("total", tally.total));

    std::vector<ApiField> fields;
    fields.push_back(rawField("tally", jsonObject(totals)));
    fields.push_back(intField("count",
                              static_cast<std::int64_t>(entries.size())));
    fields.push_back(rawField("entries", jsonArray(elements)));
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true, ""};
}

ApiResponse ActivityApi::providers(const std::vector<FailoverStat>& stats,
                                   const std::string& activeProvider) {
    std::vector<std::string> elements;
    elements.reserve(stats.size());
    for (const auto& s : stats) {
        std::vector<ApiField> f;
        f.push_back(stringField("provider", s.label));
        f.push_back(intField("attempts", s.attempts));
        f.push_back(intField("successes", s.successes));
        f.push_back(intField("failures", s.failures));
        f.push_back(intField("skipped_after_quota", s.skippedAfterQuota));
        f.push_back(stringField("last_outcome", s.lastOutcome));
        elements.push_back(jsonObject(f));
    }
    std::vector<ApiField> fields;
    fields.push_back(stringField("active_provider", activeProvider));
    fields.push_back(intField("configured",
                              static_cast<std::int64_t>(stats.size())));
    fields.push_back(rawField("providers", jsonArray(elements)));
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true, ""};
}

}  // namespace aura
