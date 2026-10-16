#pragma once
// Server-side idempotent candle store.
//
// A durable SQLite-backed store of accepted market-data candles. The unique
// identity is (source_id, symbol, timeframe, open_time_utc); inserting the same
// candle twice is a no-op that reports `already_stored`, never a second record.
// This store is the authoritative server-side half of the sender deduplication
// contract. It is a separate component from FilePersistenceStore (which remains
// the decision/position/outcome ledger).

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace aura {

struct StoredCandle {
    std::string sourceId;
    std::string symbol;
    std::string timeframe;
    std::int64_t openTimeUtc = 0;   // candle open time, epoch seconds (UTC)
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t tickVolume = 0;
    std::int64_t spread = 0;
    std::int64_t realVolume = 0;
    std::int64_t receivedAtUtc = 0; // server accept time, epoch seconds
};

// Per-candle result of an insert attempt.
struct CandleInsertResult {
    std::string symbol;
    std::string timeframe;
    std::int64_t openTimeUtc = 0;
    // "stored_new" | "already_stored" | "rejected"
    std::string state;
    std::string reason;             // rejection detail, empty when accepted
};

struct CandleSyncPoint {
    std::string symbol;
    std::string timeframe;
    std::int64_t lastOpenTimeUtc = 0;  // newest persisted candle open time
    std::int64_t count = 0;            // rows retained for the identity
};

class CandleStore {
public:
    CandleStore() = default;
    ~CandleStore();

    CandleStore(const CandleStore&) = delete;
    CandleStore& operator=(const CandleStore&) = delete;

    // Open (creating if missing) the SQLite database at `path`. Returns false
    // and fills `error` on failure. Thread-safe round-trips via the SQLite
    // connection; callers serialize batches with storeMutex.
    bool open(const std::string& path, std::string& error);

    bool isOpen() const noexcept { return db_ != nullptr; }

    // Insert a batch of candles under one transaction. Deduplication is by the
    // unique identity (source_id, symbol, timeframe, open_time_utc). A repeat of
    // an already-present candle is reported `already_stored`, not duplicated.
    // The per-candle result vector preserves input order.
    bool insertBatch(const std::vector<StoredCandle>& candles,
                     std::vector<CandleInsertResult>& results,
                     std::string& error);

    // Most recent candles for (symbol, timeframe), ascending by open time, up to
    // `limit`. Empty when none exist.
    std::vector<StoredCandle> recent(const std::string& symbol,
                                     const std::string& timeframe,
                                     int limit) const;

    // The newest persisted open time per (symbol, timeframe), used by the
    // sender to reconcile its local ledger.
    std::vector<CandleSyncPoint> syncPoints(const std::string& sourceId,
                                            const std::string& symbol) const;

    // The newest persisted open time per (symbol, timeframe) for one source
    // identity, across every symbol that source has sent. This is the safe
    // basis for reconciliation: a sender must never treat another source's
    // watermark as proof that its own candle was persisted.
    std::vector<CandleSyncPoint> syncPointsForSource(
        const std::string& sourceId) const;

    // Names of timeframes that have at least one persisted candle for the
    // identity.
    std::vector<std::string> timeframesWithData(const std::string& sourceId,
                                                const std::string& symbol) const;

    // Distinct symbols present in the store (any source).
    std::vector<std::string> distinctSymbols() const;

    // Newest persisted open time for (sourceId, symbol, timeframe), or 0.
    std::int64_t latestOpenTime(const std::string& sourceId,
                                const std::string& symbol,
                                const std::string& timeframe) const;

    // Newest persisted open time for (symbol, timeframe) across all sources,
    // used by the sync/reconcile endpoint (the sender does not own the server
    // source id).
    std::int64_t latestOpenTimeAnySource(const std::string& symbol,
                                         const std::string& timeframe) const;

    // Delete exactly one candle by its full identity. Deliberately narrow: only
    // a single, fully-qualified row can be removed, so quarantine-purge tooling
    // cannot be turned into a bulk wipe. Returns true and sets `deleted` to 1
    // when a row was removed, 0 when no such row existed.
    bool deleteCandle(const std::string& sourceId, const std::string& symbol,
                      const std::string& timeframe, std::int64_t openTimeUtc,
                      int& deleted, std::string& error);

    // Every persisted row for (symbol, timeframe), ascending by open time,
    // WITHOUT the alignment guard that `recent` applies. This is the quarantine
    // view: the read APIs hide a contaminated series, so an operator needs a way
    // to see the exact offending rows in order to purge them one by one. Never
    // exposed on an unauthenticated route.
    std::vector<StoredCandle> allRows(const std::string& symbol,
                                      const std::string& timeframe,
                                      int limit) const;

private:
    bool exec(const std::string& sql, std::string& error) const;
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
};

}  // namespace aura