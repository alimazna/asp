// TST-DEP-0001 - CandleStore durability and idempotency (server side).
//
// Verifies the authoritative server-side deduplication contract:
//   * the store persists candles durably across reopen;
//   * the unique identity (source_id, symbol, timeframe, open_time) is
//     enforced — a repeated candle is reported already_stored, never stored a
//     second time;
//   * per-candle results preserve input order and report rejections
//     individually;
//   * sync points expose the newest persisted open time per identity.

#include "TestHarness.h"

#include "foundation/Timestamp.h"
#include "persistence/CandleStore.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace aura;
namespace fs = std::filesystem;

namespace {

std::string tempDbPath() {
    const char* env = std::getenv("TMPDIR");
    fs::path base = env != nullptr ? fs::path(env) : fs::path(".");
    const auto unique = std::chrono::steady_clock::now().time_since_epoch()
                            .count();
    return (base / ("candle_store_test_" + std::to_string(unique) + ".sqlite"))
        .string();
}

StoredCandle makeCandle(const std::string& source, const std::string& symbol,
                        const std::string& tf, std::int64_t openTime) {
    StoredCandle c;
    c.sourceId = source;
    c.symbol = symbol;
    c.timeframe = tf;
    c.openTimeUtc = openTime;
    c.open = 2650.0;
    c.high = 2655.0;
    c.low = 2645.0;
    c.close = 2652.5;
    c.tickVolume = 10;
    c.spread = 5;
    c.realVolume = 0;
    c.receivedAtUtc = openTime + 60;
    return c;
}

}  // namespace

TEST_CASE(CandleStoreTest_DeduplicatesByIdentity) {
    const std::string path = tempDbPath();
    std::string error;

    CandleStore store;
    CHECK_TRUE(store.open(path, error));

    std::vector<CandleInsertResult> results;
    std::vector<StoredCandle> candles;
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 9000));
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 9000));  // dup
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 18000));
    candles.push_back(makeCandle("s2", "XAUUSD", "M5", 45000));

    CHECK_TRUE(store.insertBatch(candles, results, error));
    CHECK_EQ(static_cast<std::size_t>(4), results.size());

    // s1/M15/9000 stored once, then duplicate reported.
    CHECK_EQ(std::string("stored_new"), results[0].state);
    CHECK_EQ(std::string("already_stored"), results[1].state);
    CHECK_EQ(std::string("stored_new"), results[2].state);
    CHECK_EQ(std::string("stored_new"), results[3].state);

    store.insertBatch(candles, results, error);   // full replay
    CHECK_EQ(static_cast<std::size_t>(4), results.size());
    CHECK_EQ(std::string("already_stored"), results[0].state);
    CHECK_EQ(std::string("already_stored"), results[1].state);

    // Distinct query by identity.
    const auto recent = store.recent("XAUUSD", "M15", 100);
    CHECK_EQ(std::size_t(2), recent.size());
    CHECK_EQ(std::int64_t(9000), recent[0].openTimeUtc);
    CHECK_EQ(std::int64_t(18000), recent[1].openTimeUtc);

    std::remove(path.c_str());
}

TEST_CASE(CandleStoreTest_SyncPointsExposeNewestOpenTime) {
    const std::string path = tempDbPath();
    std::string error;

    CandleStore store;
    CHECK_TRUE(store.open(path, error));

    std::vector<CandleInsertResult> results;
    std::vector<StoredCandle> candles;
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 9000));
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 18000));
    candles.push_back(makeCandle("s1", "XAUUSD", "H1", 36000));
    candles.push_back(makeCandle("s1", "XAUUSD", "H1", 72000));
    candles.push_back(makeCandle("s1", "GOLD", "M5", 45000));
    CHECK_TRUE(store.insertBatch(candles, results, error));
    CHECK_EQ(std::size_t(5), results.size());

    const auto points = store.syncPoints("s1", "XAUUSD");
    CHECK_EQ(std::size_t(2), points.size());
    bool foundM15 = false;
    bool foundH1 = false;
    for (const auto& p : points) {
        if (p.timeframe == "M15") {
            foundM15 = true;
            CHECK_EQ(std::int64_t(18000), p.lastOpenTimeUtc);
            CHECK_EQ(std::int64_t(2), p.count);
        } else if (p.timeframe == "H1") {
            foundH1 = true;
            CHECK_EQ(std::int64_t(72000), p.lastOpenTimeUtc);
        }
    }
    CHECK_TRUE(foundM15);
    CHECK_TRUE(foundH1);

    const auto symbols = store.distinctSymbols();
    CHECK_EQ(std::size_t(2), symbols.size());

    CHECK_EQ(std::int64_t(18000),
             store.latestOpenTime("s1", "XAUUSD", "M15"));
    CHECK_EQ(std::int64_t(72000),
             store.latestOpenTimeAnySource("XAUUSD", "H1"));
    CHECK_EQ(std::int64_t(0),
             store.latestOpenTimeAnySource("XAUUSD", "MN1"));

    std::remove(path.c_str());
}

TEST_CASE(CandleStoreTest_PersistsAcrossReopen) {
    const std::string path = tempDbPath();
    std::string error;

    {
        CandleStore store;
        CHECK_TRUE(store.open(path, error));
        std::vector<CandleInsertResult> results;
        std::vector<StoredCandle> candles;
        candles.push_back(makeCandle("s1", "XAUUSD", "D1", 8000));
        CHECK_TRUE(store.insertBatch(candles, results, error));
    }

    // Reopen: data must survive (durable store, not in-memory).
    {
        CandleStore store;
        std::string reopenError;
        CHECK_TRUE(store.open(path, reopenError));
        const auto recent = store.recent("XAUUSD", "D1", 10);
        CHECK_EQ(std::size_t(1), recent.size());
        CHECK_EQ(std::int64_t(8000), recent[0].openTimeUtc);
    }

    std::remove(path.c_str());
}

TEST_CASE(CandleStoreTest_WithholdsContaminatedSeries) {
    const std::string path = tempDbPath();
    std::string error;

    CandleStore store;
    CHECK_TRUE(store.open(path, error));

    // One misaligned M15 row (1792068271 is not a multiple of 900) simulates
    // data written before ingest enforced grid alignment. It contaminates the
    // whole M15 series: even a plausible-looking aligned sibling must not be
    // served as real data.
    std::vector<CandleInsertResult> results;
    std::vector<StoredCandle> candles;
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 1792068271));  // bad
    candles.push_back(makeCandle("s1", "XAUUSD", "M15", 1792068300));  // good
    candles.push_back(makeCandle("s1", "XAUUSD", "H1", 1792065600));   // clean
    CHECK_TRUE(store.insertBatch(candles, results, error));
    CHECK_EQ(std::size_t(3), results.size());
    CHECK_EQ(std::string("stored_new"), results[0].state);

    // Contaminated M15 series is withheld from every read path.
    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M15", 10).size());
    CHECK_EQ(std::int64_t(0),
             store.latestOpenTimeAnySource("XAUUSD", "M15"));

    // The clean H1 series is unaffected and is the only sync point returned.
    const auto points = store.syncPoints("s1", "XAUUSD");
    CHECK_EQ(std::size_t(1), points.size());
    CHECK_EQ(std::string("H1"), points[0].timeframe);
    const auto h1 = store.recent("XAUUSD", "H1", 10);
    CHECK_EQ(std::size_t(1), h1.size());
    CHECK_EQ(std::int64_t(1792065600), h1[0].openTimeUtc);

    std::remove(path.c_str());
}

// The guarded read path deliberately withholds a contaminated series, which
// means an operator cannot even see the row that needs purging. allRows() is
// the quarantine view: it returns every raw row for the timeframe, aligned or
// not, so the exact offending identity can be found and deleted.
TEST_CASE(CandleStoreTest_AllRowsExposesWithheldQuarantineRows) {
    const std::string path = tempDbPath();
    std::string error;

    CandleStore store;
    CHECK_TRUE(store.open(path, error));

    std::vector<CandleInsertResult> results;
    std::vector<StoredCandle> candles;
    candles.push_back(makeCandle("legacy", "XAUUSD", "M15", 1792068271));  // bad
    candles.push_back(makeCandle("sender", "XAUUSD", "M15", 1792068300));  // good
    CHECK_TRUE(store.insertBatch(candles, results, error));

    // The guarded view hides the whole series.
    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M15", 10).size());

    // The quarantine view shows both rows, so the bad one is identifiable.
    const auto rows = store.allRows("XAUUSD", "M15", 10);
    CHECK_EQ(std::size_t(2), rows.size());
    CHECK_EQ(std::int64_t(1792068271), rows[0].openTimeUtc);
    CHECK_EQ(std::string("legacy"), rows[0].sourceId);
    CHECK_EQ(std::int64_t(1792068300), rows[1].openTimeUtc);

    // Purging the bad row by full identity restores the clean series.
    int deleted = 0;
    CHECK_TRUE(store.deleteCandle("legacy", "XAUUSD", "M15", 1792068271,
                                  deleted, error));
    CHECK_EQ(1, deleted);
    const auto restored = store.recent("XAUUSD", "M15", 10);
    CHECK_EQ(std::size_t(1), restored.size());
    CHECK_EQ(std::int64_t(1792068300), restored[0].openTimeUtc);

    std::remove(path.c_str());
}
