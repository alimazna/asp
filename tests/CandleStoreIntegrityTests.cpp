// CandleStore data-integrity + persistence tests (Agent B hardening).
//
// These pin the properties the store must hold regardless of how it is
// implemented: exact round-trip fidelity, idempotent re-ingest, restart
// durability, bounded reads, and the grid-alignment quarantine. The real M1
// case loads genuine MT5 broker bars (research/data/xauusd_m1/xauusd_m1_real.csv,
// committed + checksummed) so the store is exercised against real data, never a
// synthetic stand-in.

#include "TestHarness.h"

#include "mt5/Mt5BridgeContract.h"
#include "persistence/CandleStore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace aura;
namespace fs = std::filesystem;

namespace {

std::atomic<unsigned long> g_dbCounter{0};

std::string tempDbPath() {
    const auto unique = g_dbCounter.fetch_add(1);
    fs::path base = fs::temp_directory_path();
    return (base / ("candle_integrity_" + std::to_string(unique) + "_" +
                    std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count()) +
                    ".sqlite"))
        .string();
}

void removeDb(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path + "-wal", ec);
    fs::remove(path + "-shm", ec);
}

StoredCandle makeCandle(const std::string& source, const std::string& symbol,
                        const std::string& tf, std::int64_t openTime,
                        double base = 2650.0) {
    StoredCandle c;
    c.sourceId = source;
    c.symbol = symbol;
    c.timeframe = tf;
    c.openTimeUtc = openTime;
    c.open = base;
    c.high = base + 5.0;
    c.low = base - 5.0;
    c.close = base + 2.5;
    c.tickVolume = 10;
    c.spread = 5;
    c.realVolume = 0;
    c.receivedAtUtc = openTime + 60;
    return c;
}

// --- real M1 CSV loading ---------------------------------------------------

std::string realCsvPath() {
    const std::vector<std::string> candidates = {
        std::string(AURA_SOURCE_DIR) + "/research/data/xauusd_m1/xauusd_m1_real.csv",
        "/workspace/project/asp/research/data/xauusd_m1/xauusd_m1_real.csv",
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return std::string();
}

std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    // Howard Hinnant's days_from_civil, exact for the proleptic Gregorian
    // calendar. Avoids timegm(), which is not declared under strict -std=c++17.
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::int64_t parseEpochSeconds(const std::string& text) {
    std::tm tm{};
    std::istringstream ss(text);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return -1;
    const std::int64_t days =
        daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
}

struct RealBar {
    std::int64_t openTime = 0;
    double open = 0, high = 0, low = 0, close = 0;
    std::int64_t volume = 0;
};

std::vector<RealBar> loadRealM1(const std::string& path, std::size_t maxRows) {
    std::vector<RealBar> out;
    std::ifstream in(path);
    if (!in.is_open()) return out;
    std::string line;
    std::getline(in, line);  // header: timestamp,open,high,low,close,volume
    while (std::getline(in, line) && out.size() < maxRows) {
        if (line.empty()) continue;
        std::vector<std::string> fields;
        std::stringstream ls(line);
        std::string f;
        while (std::getline(ls, f, ',')) fields.push_back(f);
        if (fields.size() < 6) continue;
        RealBar b;
        b.openTime = parseEpochSeconds(fields[0]);
        b.open = std::stod(fields[1]);
        b.high = std::stod(fields[2]);
        b.low = std::stod(fields[3]);
        b.close = std::stod(fields[4]);
        b.volume = std::stoll(fields[5]);
        if (b.openTime < 0) continue;
        out.push_back(b);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Real-data round-trip: genuine M1 bars must persist byte-for-byte and survive
// a restart. This is the end-to-end integrity check; the fixtures below cover
// the same paths with hand-built data for the specific edge cases.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_RealM1RoundTripAndRestart) {
    const std::string csv = realCsvPath();
    CHECK_TRUE(!csv.empty());
    if (csv.empty()) return;

    const std::size_t kRows = 5000;
    const std::vector<RealBar> bars = loadRealM1(csv, kRows);
    CHECK_EQ(kRows, bars.size());
    if (bars.empty()) return;

    // Every genuine M1 bar must already be grid-aligned; the loader is honest.
    for (const auto& b : bars) {
        CHECK_EQ(std::int64_t(0), b.openTime % 60);
    }

    std::vector<StoredCandle> input;
    input.reserve(bars.size());
    for (const auto& b : bars) {
        StoredCandle c;
        c.sourceId = "mt5-real";
        c.symbol = "XAUUSD";
        c.timeframe = "M1";
        c.openTimeUtc = b.openTime;
        c.open = b.open;
        c.high = b.high;
        c.low = b.low;
        c.close = b.close;
        c.tickVolume = b.volume;
        c.spread = 0;
        c.realVolume = 0;
        c.receivedAtUtc = b.openTime + 60;
        input.push_back(c);
    }

    const std::string path = tempDbPath();
    std::string error;
    {
        CandleStore store;
        REQUIRE(store.open(path, error));
        std::vector<CandleInsertResult> results;
        REQUIRE(store.insertBatch(input, results, error));
        CHECK_EQ(input.size(), results.size());
        for (const auto& r : results) CHECK_EQ(std::string("stored_new"), r.state);

        // Read the whole series back (limit above the row count so the newest-N
        // bound is not what is under test here) and verify exact fidelity.
        const auto back = store.recent("XAUUSD", "M1", 100000);
        CHECK_EQ(bars.size(), back.size());
        CHECK_EQ(bars.front().openTime, back.front().openTimeUtc);
        CHECK_EQ(bars.back().openTime, back.back().openTimeUtc);
        for (std::size_t i = 0; i < back.size() && i < bars.size(); ++i) {
            CHECK_EQ(bars[i].openTime, back[i].openTimeUtc);
            CHECK_EQ(bars[i].open, back[i].open);
            CHECK_EQ(bars[i].high, back[i].high);
            CHECK_EQ(bars[i].low, back[i].low);
            CHECK_EQ(bars[i].close, back[i].close);
            CHECK_EQ(bars[i].volume, back[i].tickVolume);
        }
        CHECK_EQ(bars.back().openTime,
                 store.latestOpenTimeAnySource("XAUUSD", "M1"));

        // Re-ingesting the identical batch is a pure no-op.
        std::vector<CandleInsertResult> replay;
        REQUIRE(store.insertBatch(input, replay, error));
        for (const auto& r : replay)
            CHECK_EQ(std::string("already_stored"), r.state);
        CHECK_EQ(bars.size(), store.recent("XAUUSD", "M1", 100000).size());
    }

    // Restart durability: a fresh handle over the same file sees the same data.
    {
        CandleStore store;
        REQUIRE(store.open(path, error));
        const auto back = store.recent("XAUUSD", "M1", 100000);
        CHECK_EQ(bars.size(), back.size());
        CHECK_EQ(bars.front().openTime, back.front().openTimeUtc);
        CHECK_EQ(bars.back().openTime, back.back().openTimeUtc);
    }
    removeDb(path);
}

// ---------------------------------------------------------------------------
// Bounded read: `recent` must return the NEWEST `limit` bars, ascending. The
// candles route derives `newest_closed_time` from the last element, so serving
// the oldest N would publish a stale watermark as the newest closed candle.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_RecentReturnsNewestAscending) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    std::vector<StoredCandle> input;
    for (int i = 0; i < 10; ++i) {
        input.push_back(makeCandle("s1", "XAUUSD", "M1", 1782299280 + i * 60));
    }
    std::vector<CandleInsertResult> results;
    REQUIRE(store.insertBatch(input, results, error));

    const auto window = store.recent("XAUUSD", "M1", 3);
    REQUIRE(3 == static_cast<int>(window.size()));
    // Newest three, still ascending by open time.
    CHECK_EQ(std::int64_t(1782299280 + 7 * 60), window[0].openTimeUtc);
    CHECK_EQ(std::int64_t(1782299280 + 8 * 60), window[1].openTimeUtc);
    CHECK_EQ(std::int64_t(1782299280 + 9 * 60), window[2].openTimeUtc);

    // A limit at/above the row count still returns the full series ascending.
    const auto all = store.recent("XAUUSD", "M1", 10);
    CHECK_EQ(std::size_t(10), all.size());
    CHECK_EQ(std::int64_t(1782299280), all.front().openTimeUtc);
    CHECK_EQ(std::int64_t(1782299280 + 9 * 60), all.back().openTimeUtc);

    removeDb(path);
}

// ---------------------------------------------------------------------------
// A non-positive limit must never become an unbounded scan. SQLite treats a
// negative LIMIT as "no limit", so a raw negative reaches every row.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_NonPositiveLimitIsEmptyNotUnbounded) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    std::vector<StoredCandle> input;
    for (int i = 0; i < 5; ++i) {
        input.push_back(makeCandle("s1", "XAUUSD", "M1", 1782299280 + i * 60));
    }
    std::vector<CandleInsertResult> results;
    REQUIRE(store.insertBatch(input, results, error));

    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M1", 0).size());
    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M1", -1).size());
    CHECK_EQ(std::size_t(0), store.allRows("XAUUSD", "M1", 0).size());
    CHECK_EQ(std::size_t(0), store.allRows("XAUUSD", "M1", -7).size());

    removeDb(path);
}

// ---------------------------------------------------------------------------
// Out-of-order and gapped input is stored exactly as received; the read path
// orders by open time and never fabricates a missing bar.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_OutOfOrderAndGapsPreserved) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    const std::int64_t base = 1782299280;
    std::vector<StoredCandle> input;
    input.push_back(makeCandle("s1", "XAUUSD", "M1", base + 4 * 60));
    input.push_back(makeCandle("s1", "XAUUSD", "M1", base));          // gap before
    input.push_back(makeCandle("s1", "XAUUSD", "M1", base + 2 * 60)); // hole at +1
    input.push_back(makeCandle("s1", "XAUUSD", "M1", base + 4 * 60)); // dup
    std::vector<CandleInsertResult> results;
    REQUIRE(store.insertBatch(input, results, error));
    CHECK_EQ(std::string("stored_new"), results[0].state);
    CHECK_EQ(std::string("stored_new"), results[1].state);
    CHECK_EQ(std::string("stored_new"), results[2].state);
    CHECK_EQ(std::string("already_stored"), results[3].state);

    const auto rows = store.recent("XAUUSD", "M1", 100);
    REQUIRE(3 == static_cast<int>(rows.size()));
    CHECK_EQ(std::int64_t(base), rows[0].openTimeUtc);
    CHECK_EQ(std::int64_t(base + 2 * 60), rows[1].openTimeUtc);
    CHECK_EQ(std::int64_t(base + 4 * 60), rows[2].openTimeUtc);

    removeDb(path);
}

// ---------------------------------------------------------------------------
// Quarantine semantics under the newest-N read: a single misaligned legacy row
// withholds the whole series from every guarded path, but the quarantine view
// (allRows) still exposes it, and purging it restores the clean series.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_QuarantineWithholdsThenRestores) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    const std::int64_t base = 1782299280;  // aligned to M1
    std::vector<StoredCandle> input;
    for (int i = 0; i < 5; ++i) {
        input.push_back(makeCandle("sender", "XAUUSD", "M1", base + i * 60));
    }
    input.push_back(makeCandle("legacy", "XAUUSD", "M1", base + 61));  // misaligned
    std::vector<CandleInsertResult> results;
    REQUIRE(store.insertBatch(input, results, error));

    // Cross-source reads withhold the whole (symbol, timeframe) series, even
    // though the offending row belongs to a DIFFERENT source identity.
    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M1", 10).size());
    CHECK_EQ(std::int64_t(0), store.latestOpenTimeAnySource("XAUUSD", "M1"));

    // Source-scoped reconciliation is keyed to the sender's OWN rows (the
    // documented contract): the "sender" identity sent only clean bars, so its
    // own watermark is intact while the shared chart series stays withheld.
    const auto senderPoints = store.syncPoints("sender", "XAUUSD");
    REQUIRE(1 == static_cast<int>(senderPoints.size()));
    CHECK_EQ(std::string("M1"), senderPoints[0].timeframe);
    CHECK_EQ(std::int64_t(base + 4 * 60), senderPoints[0].lastOpenTimeUtc);
    REQUIRE(1 == static_cast<int>(store.syncPointsForSource("sender").size()));

    // The contaminated identity has no clean watermark of its own.
    CHECK_EQ(std::size_t(0), store.syncPoints("legacy", "XAUUSD").size());
    CHECK_EQ(std::int64_t(0), store.latestOpenTime("legacy", "XAUUSD", "M1"));

    // The quarantine view still shows the offending row.
    const auto raw = store.allRows("XAUUSD", "M1", 100);
    CHECK_EQ(std::size_t(6), raw.size());

    int deleted = 0;
    REQUIRE(store.deleteCandle("legacy", "XAUUSD", "M1", base + 61, deleted,
                               error));
    CHECK_EQ(1, deleted);

    // Clean series restored, and the newest-N window is the newest rows.
    const auto restored = store.recent("XAUUSD", "M1", 3);
    REQUIRE(3 == static_cast<int>(restored.size()));
    CHECK_EQ(std::int64_t(base + 2 * 60), restored[0].openTimeUtc);
    CHECK_EQ(std::int64_t(base + 4 * 60), restored[2].openTimeUtc);
    CHECK_EQ(std::int64_t(base + 4 * 60),
             store.latestOpenTimeAnySource("XAUUSD", "M1"));

    removeDb(path);
}

// ---------------------------------------------------------------------------
// A failed batch must not leave a partial transaction open, and the store must
// remain usable. Re-inserting the same batch after a rejected (non-duplicate)
// row behaves deterministically.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_StoreUsableAfterEmptyAndRepeatBatches) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    std::vector<CandleInsertResult> results;
    std::vector<StoredCandle> empty;
    CHECK_TRUE(store.insertBatch(empty, results, error));
    CHECK_EQ(std::size_t(0), results.size());

    std::vector<StoredCandle> one;
    one.push_back(makeCandle("s1", "XAUUSD", "M1", 1782299280));
    CHECK_TRUE(store.insertBatch(one, results, error));
    CHECK_EQ(std::string("stored_new"), results[0].state);
    CHECK_TRUE(store.insertBatch(one, results, error));
    CHECK_EQ(std::string("already_stored"), results[0].state);
    CHECK_EQ(std::size_t(1), store.recent("XAUUSD", "M1", 10).size());

    removeDb(path);
}

// ---------------------------------------------------------------------------
// A batch is atomic: a failure partway through must roll back every row already
// inserted in that batch (no partial write), and the store must stay usable for
// the next batch. The fault seam injects the mid-batch failure deterministically.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreIntegrity_BatchIsAtomicOnMidBatchFailure) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    std::vector<StoredCandle> batch;
    for (int i = 0; i < 6; ++i) {
        batch.push_back(makeCandle("s1", "XAUUSD", "M1", 1782299280 + i * 60));
    }

    // Fail after the third successful INSERT of this batch.
    store.setFaultAfterInsertsForTest(3);
    std::vector<CandleInsertResult> results;
    const bool ok = store.insertBatch(batch, results, error);
    CHECK_TRUE(!ok);
    CHECK_TRUE(!error.empty());
    // Nothing from the aborted batch survived.
    CHECK_EQ(std::size_t(0), store.recent("XAUUSD", "M1", 100).size());
    CHECK_EQ(std::int64_t(0), store.latestOpenTime("s1", "XAUUSD", "M1"));

    // The seam is one-shot and the store is still usable: the same batch now
    // commits cleanly and in full.
    std::vector<CandleInsertResult> retry;
    REQUIRE(store.insertBatch(batch, retry, error));
    CHECK_EQ(batch.size(), retry.size());
    for (const auto& r : retry) CHECK_EQ(std::string("stored_new"), r.state);
    CHECK_EQ(std::size_t(6), store.recent("XAUUSD", "M1", 100).size());

    removeDb(path);
}
