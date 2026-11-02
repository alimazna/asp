#pragma once
// Durable activity log: what the backend actually did, and whether it worked.
//
// The owner asked to see, side by side, which operations succeeded and what they
// returned — a successful news read, a successful candle read, an AI call. This
// log is that record. It is append-only and written to SQLite, so it survives a
// restart and can be read back through the API.
//
// Honesty contract:
//   * one row per attempted operation, with the real outcome — never a
//     synthesised success;
//   * `detail` holds a short, secret-free summary (counts, ids, byte sizes);
//     credentials and full article bodies are never written here;
//   * the log is observational only. Nothing reads it to make a decision, so a
//     log write failure degrades visibility, never correctness.

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace aura {

// A single recorded operation.
struct ActivityEntry {
    std::int64_t id = 0;
    std::int64_t atUtc = 0;
    // Coarse operation class, e.g. "news_ingest", "news_read", "candle_read",
    // "llm_call", "llm_analyze", "analysis_read", "history_read".
    std::string kind;
    // "ok" | "empty" | "rejected" | "unavailable" | "error"
    std::string outcome;
    // Short machine-readable reason, e.g. "unauthorized", "quota_exhausted".
    std::string reason;
    // Which provider/source acted, when applicable, e.g. "gemini", "rss".
    std::string source;
    // Free-form, bounded, secret-free summary: counts, ids, sizes.
    std::string detail;
    int itemCount = 0;
    int durationMs = -1;
};

class ActivityLog {
public:
    ActivityLog() = default;
    ~ActivityLog();

    ActivityLog(const ActivityLog&) = delete;
    ActivityLog& operator=(const ActivityLog&) = delete;

    bool open(const std::string& path, std::string& error);
    bool isOpen() const noexcept { return db_ != nullptr; }

    // Append one entry. Returns false with `error` set on a storage failure; the
    // caller may ignore it, because a log write must never fail an operation.
    bool record(const ActivityEntry& entry, std::string& error);

    // Newest first. `kind` filters when non-empty; `limit` is clamped.
    std::vector<ActivityEntry> recent(int limit,
                                      const std::string& kind) const;

    // Totals per outcome for a quick "how healthy has this been" read.
    struct Tally {
        std::int64_t ok = 0;
        std::int64_t empty = 0;
        std::int64_t rejected = 0;
        std::int64_t unavailable = 0;
        std::int64_t error = 0;
        std::int64_t total = 0;
    };
    Tally tally(std::int64_t sinceUtc) const;

    // Delete rows older than `cutoffUtc`, bounding growth. Returns the number
    // removed. Never called automatically at a fixed schedule; the caller
    // decides, so a purge is always an explicit act.
    int pruneBefore(std::int64_t cutoffUtc);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace aura
