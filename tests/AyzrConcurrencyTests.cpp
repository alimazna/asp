// CandleStore concurrency + restart-durability tests (Agent B hardening).
//
// The server holds ONE CandleStore shared by the ingestion writer and every
// read route. These tests exercise that model: concurrent readers against a
// writer on the same handle, idempotent duplicate batches from many writers,
// lock contention across multiple connections to the same file (busy timeout),
// and durability across close/reopen.

#include "TestHarness.h"

#include "persistence/CandleStore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace aura;
namespace fs = std::filesystem;

namespace {

std::atomic<unsigned long> g_dbCounter{0};

std::string tempDbPath() {
    const auto unique = g_dbCounter.fetch_add(1);
    return (fs::temp_directory_path() /
            ("candle_concurrency_" + std::to_string(unique) + "_" +
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

// ---------------------------------------------------------------------------
// One handle, one writer appending strictly-increasing batches, several readers
// querying concurrently. Readers must never observe a watermark that goes
// backwards (each committed batch only ever adds newer bars) and must never
// crash the connection. After the writer finishes, the full series is present.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreConcurrency_ReadersAgainstSingleWriter) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    const std::int64_t base = 1782299280;  // aligned to M1
    const int kTotal = 2000;
    const int kBatch = 50;

    std::atomic<bool> done{false};
    std::atomic<int> readerErrors{0};

    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&]() {
            std::int64_t lastSeen = 0;
            while (!done.load(std::memory_order_acquire)) {
                const std::int64_t w =
                    store.latestOpenTimeAnySource("XAUUSD", "M1");
                // Watermark is monotonic non-decreasing under atomic batches.
                if (w < lastSeen) ++readerErrors;
                lastSeen = w;
                // Touch the other read paths so the whole surface is exercised.
                (void)store.recent("XAUUSD", "M1", 25);
                (void)store.syncPointsForSource("writer");
                (void)store.distinctSymbols();
            }
        });
    }

    for (int i = 0; i < kTotal; i += kBatch) {
        std::vector<StoredCandle> batch;
        for (int j = 0; j < kBatch; ++j) {
            batch.push_back(
                makeCandle("writer", "XAUUSD", "M1", base + (i + j) * 60));
        }
        std::vector<CandleInsertResult> results;
        REQUIRE(store.insertBatch(batch, results, error));
    }
    done.store(true, std::memory_order_release);
    for (auto& t : readers) t.join();

    CHECK_EQ(0, readerErrors.load());
    const auto all = store.recent("XAUUSD", "M1", kTotal);
    CHECK_EQ(std::size_t(kTotal), all.size());
    CHECK_EQ(std::int64_t(base), all.front().openTimeUtc);
    CHECK_EQ(std::int64_t(base + (kTotal - 1) * 60), all.back().openTimeUtc);

    removeDb(path);
}

// ---------------------------------------------------------------------------
// Many writers race to insert the SAME identities. Dedup must be exact: exactly
// one "stored_new" per identity across all threads, every other attempt reports
// "already_stored", and the store holds exactly one row per identity.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreConcurrency_DuplicateBatchesStoredExactlyOnce) {
    const std::string path = tempDbPath();
    std::string error;
    CandleStore store;
    REQUIRE(store.open(path, error));

    const std::int64_t base = 1782299280;
    const int kIdentities = 200;
    const int kThreads = 6;

    std::vector<StoredCandle> shared;
    for (int i = 0; i < kIdentities; ++i) {
        shared.push_back(makeCandle("s1", "XAUUSD", "M1", base + i * 60));
    }

    std::atomic<int> storedNew{0};
    std::atomic<int> alreadyStored{0};
    std::atomic<int> failures{0};
    std::mutex errMutex;

    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&]() {
            std::vector<CandleInsertResult> results;
            std::string err;
            if (!store.insertBatch(shared, results, err)) {
                ++failures;
                std::lock_guard<std::mutex> lk(errMutex);
                (void)err;
                return;
            }
            for (const auto& r : results) {
                if (r.state == "stored_new") ++storedNew;
                else if (r.state == "already_stored") ++alreadyStored;
                else ++failures;
            }
        });
    }
    for (auto& w : writers) w.join();

    CHECK_EQ(0, failures.load());
    CHECK_EQ(kIdentities, storedNew.load());          // each identity once
    CHECK_EQ(kIdentities * (kThreads - 1), alreadyStored.load());
    const auto all = store.recent("XAUUSD", "M1", kIdentities + 10);
    CHECK_EQ(std::size_t(kIdentities), all.size());

    removeDb(path);
}

// ---------------------------------------------------------------------------
// Lock contention across independent connections to the same file must be
// absorbed by the busy timeout, not surfaced as a failed batch. Four stores
// each write disjoint single-row batches; every insert must succeed and all
// rows must be present afterwards.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreConcurrency_MultiConnectionContentionUsesBusyTimeout) {
    const std::string path = tempDbPath();
    std::string error;
    {
        CandleStore bootstrap;
        REQUIRE(bootstrap.open(path, error));  // create schema + WAL
    }

    const std::int64_t base = 1782299280;
    const int kPerThread = 100;
    const int kThreads = 4;

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            CandleStore store;
            std::string openErr;
            if (!store.open(path, openErr)) {
                ++failures;
                return;
            }
            for (int i = 0; i < kPerThread; ++i) {
                // Disjoint identities per thread; strictly increasing times.
                const std::int64_t openTime =
                    base + (t * kPerThread + i) * 60;
                std::vector<StoredCandle> batch;
                batch.push_back(makeCandle("s" + std::to_string(t), "XAUUSD",
                                           "M1", openTime));
                std::vector<CandleInsertResult> results;
                std::string err;
                if (!store.insertBatch(batch, results, err)) {
                    ++failures;
                    return;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    CHECK_EQ(0, failures.load());
    CandleStore verify;
    REQUIRE(verify.open(path, error));
    const auto all = verify.recent("XAUUSD", "M1", kPerThread * kThreads + 10);
    CHECK_EQ(std::size_t(kPerThread * kThreads), all.size());

    removeDb(path);
}

// ---------------------------------------------------------------------------
// Durability across connection close/reopen, including data written by a second
// connection while the first was open. A fresh handle must see every committed
// row.
// ---------------------------------------------------------------------------
TEST_CASE(CandleStoreConcurrency_DurableAcrossConnectionsAndReopen) {
    const std::string path = tempDbPath();
    std::string error;
    const std::int64_t base = 1782299280;

    {
        CandleStore a;
        REQUIRE(a.open(path, error));
        std::vector<StoredCandle> batch;
        for (int i = 0; i < 300; ++i) {
            batch.push_back(makeCandle("a", "XAUUSD", "M1", base + i * 60));
        }
        std::vector<CandleInsertResult> results;
        REQUIRE(a.insertBatch(batch, results, error));

        // A second connection to the same file writes a disjoint range and must
        // see the first connection's committed rows.
        CandleStore b;
        REQUIRE(b.open(path, error));
        CHECK_EQ(std::int64_t(base + 299 * 60),
                 b.latestOpenTimeAnySource("XAUUSD", "M1"));
        std::vector<StoredCandle> batch2;
        for (int i = 300; i < 600; ++i) {
            batch2.push_back(makeCandle("b", "XAUUSD", "M1", base + i * 60));
        }
        std::vector<CandleInsertResult> results2;
        REQUIRE(b.insertBatch(batch2, results2, error));
    }

    CandleStore fresh;
    REQUIRE(fresh.open(path, error));
    const auto all = fresh.recent("XAUUSD", "M1", 1000);
    CHECK_EQ(std::size_t(600), all.size());
    CHECK_EQ(std::int64_t(base), all.front().openTimeUtc);
    CHECK_EQ(std::int64_t(base + 599 * 60), all.back().openTimeUtc);

    removeDb(path);
}
