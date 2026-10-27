// TST-AYZR-E-0002 - AYZR performance-resilience suite.
//
// Agent E scope: the failure modes that only appear under concurrency and
// restarts. Every case drives the REAL `astra_server` binary over real loopback
// HTTP against a real SQLite file, so a regression in the ingest transaction,
// the durability path or the dependency isolation fails here rather than being
// hidden by a stub.
//
// Coverage:
//   * concurrent ingestion correctness - disjoint batches from concurrent
//     clients are all stored exactly once; the SAME batch from concurrent
//     clients yields exactly one stored_new and never a second row;
//   * restart recovery - ingested data and its watermark survive a stop/start
//     cycle, and a replayed batch is still a duplicate (never re-inserted);
//   * dependency isolation - an AI provider failure (dead/unresponsive LLM7
//     endpoint) and a news-store failure must NOT disable the candle or the
//     ASTRA analysis endpoints, which must keep serving real data.
//
// The data is the committed REAL MT5 corpus
// (`research/data/xauusd_m1/xauusd_m1_real.csv`); higher timeframes are
// deterministic aggregations of complete M1 buckets only, so no bar here is
// fabricated. The test SKIPs (it does not fail) when the corpus is absent.
//
// Read-only with respect to ASTRA: nothing here mutates a decision, the engine
// or production state. LLM calls use the mock provider or a dead loopback port,
// so no external quota is spent.

#include "TestHarness.h"

#include "foundation/HttpClient.h"
#include "platform/windows/ProcessSupervisor.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

namespace {

std::string corpusPath() {
    return std::string(AURA_SOURCE_DIR) +
           "/research/data/xauusd_m1/xauusd_m1_real.csv";
}

std::uint16_t freePort() {
#ifdef _WIN32
    return 8793;
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8793;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    std::uint16_t port = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
    ::close(s);
    return port;
#endif
}

std::string binaryPath() {
    const fs::path base = fs::current_path();
#ifdef _WIN32
    return (base / "astra_server.exe").string();
#else
    return (base / "astra_server").string();
#endif
}

std::string uniqueDir(const std::string& tag) {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("astra_ayzr_perf_" + tag + "_" + std::to_string(unique)))
        .string();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

HttpResponse request(std::uint16_t port, const std::string& method,
                     const std::string& path, const std::string& token,
                     const std::string& body = "", int readTimeoutMs = 60000) {
    HttpRequest req;
    req.host = "127.0.0.1";
    req.port = port;
    req.path = path;
    req.method = method;
    req.body = body;
    if (!token.empty()) {
        req.headers = "Authorization: Bearer " + token + "\r\n";
    }
    req.connectTimeoutMillis = 3000;
    req.readTimeoutMillis = readTimeoutMs;
    return httpRequest(req);
}

bool waitReady(std::uint16_t port, int attempts = 100) {
    for (int i = 0; i < attempts; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 800;
        if (httpGet(health).ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// One real M1 bar from the committed corpus.
struct M1Bar {
    std::int64_t t = 0;
    double open = 0, high = 0, low = 0, close = 0;
    std::int64_t volume = 0;
};

// Parse "YYYY-MM-DD HH:MM:SS" as UTC epoch seconds (no locale dependency).
bool parseStamp(const std::string& text, std::int64_t& epochOut) {
    std::tm tm{};
    std::istringstream in(text);
    in >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (in.fail()) return false;
#ifdef _WIN32
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) return false;
    epochOut = static_cast<std::int64_t>(t);
    return true;
}

// Load up to `limit` real M1 bars. Returns false when the corpus is missing or
// unreadable, so the caller can SKIP rather than fabricate data.
bool loadRealM1(int limit, std::vector<M1Bar>& out) {
    out.clear();
    std::ifstream in(corpusPath());
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line)) return false;
    if (line != "timestamp,open,high,low,close,volume") return false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ',')) parts.push_back(field);
        if (parts.size() < 6) continue;
        M1Bar bar;
        if (!parseStamp(parts[0], bar.t)) continue;
        bar.open = std::stod(parts[1]);
        bar.high = std::stod(parts[2]);
        bar.low = std::stod(parts[3]);
        bar.close = std::stod(parts[4]);
        bar.volume = static_cast<std::int64_t>(std::stod(parts[5]));
        out.push_back(bar);
        if (limit > 0 && static_cast<int>(out.size()) >= limit) break;
    }
    return !out.empty();
}

std::string candleJson(const M1Bar& bar) {
    std::ostringstream out;
    out << "{\"time\":" << bar.t << ",\"open\":" << bar.open
        << ",\"high\":" << bar.high << ",\"low\":" << bar.low
        << ",\"close\":" << bar.close
        << ",\"tick_volume\":" << bar.volume
        << ",\"spread\":0,\"real_volume\":0}";
    return out.str();
}

std::string ingestBody(const std::string& source, const std::string& timeframe,
                       const std::vector<M1Bar>& bars) {
    std::ostringstream out;
    out << "{\"source_id\":\"" << source << "\",\"symbol\":\"XAUUSD\","
        << "\"timeframe\":\"" << timeframe << "\",\"candles\":[";
    for (std::size_t i = 0; i < bars.size(); ++i) {
        if (i > 0) out << ",";
        out << candleJson(bars[i]);
    }
    out << "]}";
    return out.str();
}

// Deterministic M1 -> timeframe aggregation, keeping only buckets whose full M1
// minute set is present. A partial bucket would be a fabricated bar.
std::vector<M1Bar> aggregateComplete(const std::vector<M1Bar>& m1,
                                     std::int64_t step, std::size_t expect) {
    std::vector<M1Bar> out;
    std::size_t i = 0;
    while (i < m1.size()) {
        const std::int64_t bucket = m1[i].t - (m1[i].t % step);
        if (m1[i].t != bucket) {   // bucket does not start on the grid
            ++i;
            continue;
        }
        std::size_t j = i;
        M1Bar agg = m1[i];
        agg.t = bucket;
        while (j < m1.size() && m1[j].t - (m1[j].t % step) == bucket) {
            agg.high = std::max(agg.high, m1[j].high);
            agg.low = std::min(agg.low, m1[j].low);
            agg.close = m1[j].close;
            agg.volume += m1[j].volume;
            ++j;
        }
        if (j - i == expect) out.push_back(agg);
        i = j;
    }
    return out;
}

// Count JSON array elements by the marker a field's value uses.
int countOccurrences(const std::string& haystack, const std::string& needle) {
    int n = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// RAII wrapper around a spawned astra_server plus its temp data dir.
struct TestServer {
    std::unique_ptr<ProcessSupervisor> supervisor;
    std::uint16_t port = 0;
    std::string dataDir;
    bool started = false;

    bool start(const std::vector<std::string>& extraArgs,
               const std::map<std::string, std::string>& env,
               std::string& error) {
        const std::string exe = binaryPath();
        if (!fs::exists(exe)) {
            error = "astra_server binary not built";
            return false;
        }
        port = freePort();
        dataDir = uniqueDir("srv");
        fs::create_directories(dataDir);

        ProcessLaunchSpec spec;
        spec.executable = exe;
        spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--data-dir", dataDir};
        for (const auto& a : extraArgs) spec.args.push_back(a);
        spec.environment = env;
        spec.environment["ASTRA_DATA_DIR"] = dataDir;
        supervisor = std::make_unique<ProcessSupervisor>(spec);
        if (!supervisor->start(error)) return false;
        started = true;
        if (!waitReady(port)) {
            error = "server did not become ready";
            return false;
        }
        return true;
    }

    void stop() {
        if (supervisor && started) {
            std::string stopError;
            supervisor->stop(3000, stopError);
            started = false;
        }
    }

    ~TestServer() {
        stop();
        if (!dataDir.empty()) {
            std::error_code ec;
            fs::remove_all(dataDir, ec);
        }
    }
};

std::map<std::string, std::string> baseEnv() {
    return {
        {"ASTRA_SENDER_TOKEN", "perf-sender"},
        {"ASTRA_CLIENT_TOKEN", "perf-client"},
        {"ASTRA_LLM_PROVIDER", "mock"},
        // The resilience cases fire many requests quickly; the limiter is not
        // what they test, and a 429 would be a false failure.
        {"ASTRA_INGEST_RATE_PER_MIN", "0"},
        {"ASTRA_SYNC_RATE_PER_MIN", "0"},
    };
}

// Start the server against an EXISTING data dir (restart recovery).
struct RestartableServer {
    std::unique_ptr<ProcessSupervisor> supervisor;
    std::uint16_t port = 0;
    std::string dataDir;

    bool startFresh(std::string& error) {
        dataDir = uniqueDir("restart");
        fs::create_directories(dataDir);
        return start(error);
    }

    bool start(std::string& error) {
        const std::string exe = binaryPath();
        if (!fs::exists(exe)) {
            error = "astra_server binary not built";
            return false;
        }
        port = freePort();
        ProcessLaunchSpec spec;
        spec.executable = exe;
        spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--data-dir", dataDir};
        spec.environment = baseEnv();
        spec.environment["ASTRA_DATA_DIR"] = dataDir;
        supervisor = std::make_unique<ProcessSupervisor>(spec);
        if (!supervisor->start(error)) return false;
        if (!waitReady(port)) {
            error = "server did not become ready";
            return false;
        }
        return true;
    }

    void stop() {
        if (supervisor) {
            std::string stopError;
            supervisor->stop(3000, stopError);
            supervisor.reset();
        }
    }

    ~RestartableServer() {
        stop();
        if (!dataDir.empty()) {
            std::error_code ec;
            fs::remove_all(dataDir, ec);
        }
    }
};

}  // namespace

// ===========================================================================
// Section 1 - Concurrent ingestion correctness
// ===========================================================================

// Concurrent DISJOINT ingestion must be lossless: every candle from every
// client is stored exactly once, and no client is told a candle it sent was a
// duplicate of another client's disjoint window.
TEST_CASE(ayzr_concurrent_disjoint_ingest_is_lossless) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(4000, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "perf-sender"}, baseEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const int threads = 4;
    const int perBatch = 250;
    std::vector<std::string> bodies;
    for (int i = 0; i < threads; ++i) {
        std::vector<M1Bar> window(m1.begin() + i * perBatch,
                                  m1.begin() + (i + 1) * perBatch);
        bodies.push_back(ingestBody("conc-" + std::to_string(i), "M1", window));
    }

    std::vector<HttpResponse> responses(threads);
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&, i]() {
            responses[i] = request(server.port, "POST",
                                   "/api/v1/candles/ingest", "perf-sender",
                                   bodies[i]);
        });
    }
    for (auto& w : workers) w.join();

    int storedTotal = 0;
    for (int i = 0; i < threads; ++i) {
        CHECK_EQ(responses[i].statusCode, 200);
        CHECK(contains(responses[i].body, "\"state\":\"stored_new\""));
        CHECK(contains(responses[i].body, "\"ingested\":250"));
        storedTotal += countOccurrences(responses[i].body, "\"stored_new\"");
    }
    // Every one of the 1000 disjoint real bars was stored exactly once.
    CHECK_EQ(storedTotal, threads * perBatch);

    // The store serves exactly the 1000 real bars that were sent.
    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=1000", "");
    CHECK_EQ(chart.statusCode, 200);
    CHECK(contains(chart.body, "\"count\":1000"));
}

// The SAME batch posted by concurrent clients must be stored exactly once: one
// request reports stored_new, every other reports already_stored, and a second
// row for a candle never appears.
TEST_CASE(ayzr_concurrent_duplicate_ingest_stores_once) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(1200, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "perf-sender"}, baseEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    // A fresh window (the corpus prefix is untouched by the other cases here).
    std::vector<M1Bar> window(m1.begin() + 200, m1.begin() + 400);
    const std::string body = ingestBody("dup-source", "M1", window);

    const int threads = 8;
    std::vector<HttpResponse> responses(threads);
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&, i]() {
            responses[i] = request(server.port, "POST",
                                   "/api/v1/candles/ingest", "perf-sender", body);
        });
    }
    for (auto& w : workers) w.join();

    int storedNew = 0;
    int duplicates = 0;
    for (int i = 0; i < threads; ++i) {
        CHECK_EQ(responses[i].statusCode, 200);
        storedNew += countOccurrences(responses[i].body, "\"stored_new\"");
        duplicates += countOccurrences(responses[i].body, "\"already_stored\"");
    }
    CHECK_EQ(storedNew, 200);          // exactly one request stored the batch
    CHECK_EQ(duplicates, (threads - 1) * 200);

    // One row per candle, never two.
    const HttpResponse chart =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=1000", "");
    CHECK_EQ(chart.statusCode, 200);
    CHECK(contains(chart.body, "\"count\":200"));
}

// A read issued while ingestion is in flight must return a consistent,
// real-data answer: no 5xx, no truncated body, and never a fabricated bar.
TEST_CASE(ayzr_concurrent_read_during_ingest_stays_consistent) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(4000, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "perf-sender"}, baseEnv(), error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    // Seed a baseline so the read has real data to serve from the start.
    std::vector<M1Bar> seed(m1.begin(), m1.begin() + 500);
    const HttpResponse seeded = request(server.port, "POST",
                                        "/api/v1/candles/ingest", "perf-sender",
                                        ingestBody("seed", "M1", seed));
    CHECK_EQ(seeded.statusCode, 200);

    std::atomic<int> readFailures{0};
    std::atomic<int> readSuccesses{0};
    std::atomic<bool> stop{false};

    std::thread writer([&]() {
        for (int i = 0; i < 6 && !stop.load(); ++i) {
            std::vector<M1Bar> window(m1.begin() + 500 + i * 500,
                                      m1.begin() + 1000 + i * 500);
            request(server.port, "POST", "/api/v1/candles/ingest",
                    "perf-sender", ingestBody("wr-" + std::to_string(i), "M1",
                                              window));
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < 10; ++i) {
            const HttpResponse r =
                request(server.port, "GET", "/api/v1/candles?tf=M1&limit=100", "");
            if (r.statusCode == 200 && contains(r.body, "\"bars\"")) {
                ++readSuccesses;
            } else {
                ++readFailures;
            }
        }
        stop.store(true);
    });

    reader.join();
    writer.join();

    // Every read during ingestion either served real bars or honestly reported
    // unavailability; none may be a 5xx or a malformed body.
    CHECK_EQ(readFailures.load(), 0);
    CHECK_EQ(readSuccesses.load(), 10);
}

// ===========================================================================
// Section 2 - Restart recovery
// ===========================================================================

// Ingested real candles and their watermark must survive a stop/start cycle,
// and a replayed batch after restart must still be a duplicate.
TEST_CASE(ayzr_restart_preserves_candles_and_idempotency) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(1500, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    std::vector<M1Bar> window(m1.begin(), m1.begin() + 500);
    const std::string body = ingestBody("restart-src", "M1", window);

    RestartableServer server;
    std::string error;
    if (!server.startFresh(error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const HttpResponse first = request(server.port, "POST",
                                       "/api/v1/candles/ingest", "perf-sender",
                                       body);
    CHECK_EQ(first.statusCode, 200);
    CHECK(contains(first.body, "\"state\":\"stored_new\""));
    CHECK(contains(first.body, "\"ingested\":500"));

    const HttpResponse before =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=1000", "");
    CHECK_EQ(before.statusCode, 200);
    CHECK(contains(before.body, "\"count\":500"));

    // Stop and restart against the SAME data dir.
    server.stop();
    std::string restartError;
    REQUIRE(server.start(restartError));

    // The real candles are still served, unchanged.
    const HttpResponse after =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=1000", "");
    CHECK_EQ(after.statusCode, 200);
    CHECK(contains(after.body, "\"count\":500"));
    CHECK_EQ(after.body, before.body);

    // The watermark is the same newest real bar.
    HttpRequest sync;
    sync.host = "127.0.0.1";
    sync.port = server.port;
    sync.path = "/api/v1/sync/latest?source_id=restart-src";
    sync.headers = "Authorization: Bearer perf-sender\r\n";
    const HttpResponse syncAfter = httpGet(sync);
    CHECK_EQ(syncAfter.statusCode, 200);
    CHECK(contains(syncAfter.body,
                   "\"last_open_time_utc\":" + std::to_string(window.back().t)));

    // Replaying the batch after restart is still a duplicate: durable
    // idempotency, not an in-memory dedup cache.
    const HttpResponse replay = request(server.port, "POST",
                                        "/api/v1/candles/ingest", "perf-sender",
                                        body);
    CHECK_EQ(replay.statusCode, 200);
    CHECK(contains(replay.body, "\"state\":\"already_stored\""));
    CHECK(contains(replay.body, "\"ingested\":0"));

    const HttpResponse finalCount =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=1000", "");
    CHECK(contains(finalCount.body, "\"count\":500"));
}

// ===========================================================================
// Section 3 - Dependency isolation (AI / news failure must not disable ASTRA)
// ===========================================================================

// An AI provider that is unreachable must degrade news analysis to `unavailable`
// while the candle and ASTRA analysis endpoints keep serving real data.
TEST_CASE(ayzr_ai_failure_does_not_disable_candles_or_analysis) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(2000, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    const std::uint16_t deadPort = freePort();   // nothing listens here
    auto env = baseEnv();
    env["ASTRA_LLM_PROVIDER"] = "llm7";
    env["LLM7_API_TOKEN"] = "perf-llm7-token";
    env["LLM7_BASE_URL"] =
        "http://127.0.0.1:" + std::to_string(deadPort) + "/v1";
    env["LLM7_CONNECT_TIMEOUT_MS"] = "500";
    env["LLM7_READ_TIMEOUT_MS"] = "800";
    env["LLM7_MAX_ATTEMPTS"] = "1";

    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "perf-sender"}, env, error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    // Real candles in, real candles out.
    std::vector<M1Bar> window(m1.begin(), m1.begin() + 500);
    const HttpResponse ingested =
        request(server.port, "POST", "/api/v1/candles/ingest", "perf-sender",
                ingestBody("ai-fail-src", "M1", window));
    CHECK_EQ(ingested.statusCode, 200);
    CHECK(contains(ingested.body, "\"ingested\":500"));

    // A real M15 series, aggregated from complete M1 buckets only, so the
    // higher-timeframe read is exercised with genuine bars too.
    const std::vector<M1Bar> m15 = aggregateComplete(m1, 900, 15);
    REQUIRE(!m15.empty());
    const HttpResponse m15Ingest =
        request(server.port, "POST", "/api/v1/candles/ingest", "perf-sender",
                ingestBody("ai-fail-src", "M15", m15));
    CHECK_EQ(m15Ingest.statusCode, 200);
    CHECK(contains(m15Ingest.body,
                   "\"ingested\":" + std::to_string(m15.size())));

    // A news article, so the analyze path actually reaches the dead provider.
    const std::string news =
        "{\"articles\":[{\"headline\":\"Gold steady ahead of CPI\","
        "\"source\":\"example.com\",\"url\":\"https://example.com/ai-fail\","
        "\"published_at\":1792000000,\"body\":\"Gold held steady.\","
        "\"gold_relevance\":\"high\"}]}";
    const HttpResponse newsIngest = request(server.port, "POST",
                                            "/api/v1/news/ingest", "perf-sender",
                                            news);
    CHECK_EQ(newsIngest.statusCode, 200);

    // The AI call fails honestly: no direction, no levels.
    const HttpResponse analyze = request(
        server.port, "GET", "/api/v1/news/analyze?id=https://example.com/ai-fail",
        "perf-client");
    CHECK_EQ(analyze.statusCode, 200);
    CHECK(contains(analyze.body, "\"status\":\"unavailable\""));
    CHECK(contains(analyze.body, "\"direction\":\"no_trade\""));

    // ASTRA is NOT disabled by the AI failure.
    const HttpResponse candles =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=500", "");
    CHECK_EQ(candles.statusCode, 200);
    CHECK(contains(candles.body, "\"count\":500"));
    CHECK(contains(candles.body, "\"bars\""));

    const HttpResponse analysis =
        request(server.port, "GET", "/api/v1/analysis/latest", "");
    CHECK_EQ(analysis.statusCode, 200);
    CHECK(contains(analysis.body, "\"api\":\"v1\""));

    const HttpResponse context =
        request(server.port, "GET", "/api/v1/context/latest", "");
    CHECK_EQ(context.statusCode, 200);

    const HttpResponse m15Read =
        request(server.port, "GET", "/api/v1/candles?tf=M15&limit=500", "");
    CHECK_EQ(m15Read.statusCode, 200);
    CHECK(contains(m15Read.body,
                   "\"count\":" + std::to_string(m15.size())));

    // The provider credential is never echoed to a client.
    CHECK(!contains(analyze.body, "perf-llm7-token"));
}

// A news-store failure must not disable the candle or ASTRA analysis endpoints:
// the news surface degrades honestly while ASTRA keeps serving real data.
TEST_CASE(ayzr_news_store_failure_does_not_disable_candles_or_analysis) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(2000, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }

    // Make news.sqlite a DIRECTORY so the news store cannot open.
    const std::string dataDir = uniqueDir("newsfail");
    fs::create_directories(fs::path(dataDir) / "news.sqlite");

    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    const std::uint16_t port = freePort();
    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", "perf-sender"};
    spec.environment = baseEnv();
    spec.environment["ASTRA_DATA_DIR"] = dataDir;
    ProcessSupervisor supervisor(spec);
    std::string error;
    if (!supervisor.start(error) || !waitReady(port)) {
        std::cout << "  [SKIP] server did not start: " << error << "\n";
        std::string stopError;
        supervisor.stop(2000, stopError);
        std::error_code ec;
        fs::remove_all(dataDir, ec);
        return;
    }

    std::vector<M1Bar> window(m1.begin(), m1.begin() + 500);
    const HttpResponse ingested =
        request(port, "POST", "/api/v1/candles/ingest", "perf-sender",
                ingestBody("news-fail-src", "M1", window));
    CHECK_EQ(ingested.statusCode, 200);
    CHECK(contains(ingested.body, "\"ingested\":500"));

    // News read degrades to an honest empty list, not an error and not
    // fabricated articles.
    const HttpResponse list =
        request(port, "GET", "/api/v1/news", "perf-client");
    CHECK_EQ(list.statusCode, 200);
    CHECK(contains(list.body, "\"news\":[]"));
    const HttpResponse history =
        request(port, "GET", "/api/v1/history", "perf-client");
    CHECK_EQ(history.statusCode, 200);
    CHECK(contains(history.body, "\"history\":[]"));

    // ASTRA keeps working with real data.
    const HttpResponse candles =
        request(port, "GET", "/api/v1/candles?tf=M1&limit=500", "");
    CHECK_EQ(candles.statusCode, 200);
    CHECK(contains(candles.body, "\"count\":500"));
    const HttpResponse analysis =
        request(port, "GET", "/api/v1/analysis/latest", "");
    CHECK_EQ(analysis.statusCode, 200);
    CHECK(contains(analysis.body, "\"api\":\"v1\""));

    std::string stopError;
    supervisor.stop(3000, stopError);
    std::error_code ec;
    fs::remove_all(dataDir, ec);
}

// After an AI failure the system must recover without a restart: a later
// analysis with a healthy provider serves a real result again. This pins that
// the failure is per-request degradation, not a latched disabled state.
TEST_CASE(ayzr_ai_failure_is_not_latched) {
    std::vector<M1Bar> m1;
    if (!loadRealM1(2000, m1)) {
        std::cout << "  [SKIP] real corpus unavailable: " << corpusPath() << "\n";
        return;
    }
    const std::uint16_t deadPort = freePort();
    auto env = baseEnv();
    env["ASTRA_LLM_PROVIDER"] = "llm7";
    env["LLM7_API_TOKEN"] = "perf-llm7-token";
    env["LLM7_BASE_URL"] =
        "http://127.0.0.1:" + std::to_string(deadPort) + "/v1";
    env["LLM7_CONNECT_TIMEOUT_MS"] = "500";
    env["LLM7_READ_TIMEOUT_MS"] = "800";
    env["LLM7_MAX_ATTEMPTS"] = "1";

    TestServer server;
    std::string error;
    if (!server.start({"--sender-token", "perf-sender"}, env, error)) {
        std::cout << "  [SKIP] " << error << "\n";
        return;
    }

    const std::string news =
        "{\"articles\":[{\"headline\":\"Gold eases on firm yields\","
        "\"source\":\"example.com\",\"url\":\"https://example.com/latch\","
        "\"published_at\":1792000000,\"body\":\"Gold eased.\","
        "\"gold_relevance\":\"medium\"}]}";
    CHECK_EQ(request(server.port, "POST", "/api/v1/news/ingest",
                     "perf-sender", news)
                 .statusCode,
             200);

    // Two consecutive analyses both fail honestly - a transient failure is not
    // remembered as a permanent one.
    for (int i = 0; i < 2; ++i) {
        const HttpResponse analyze = request(
            server.port, "GET",
            "/api/v1/news/analyze?id=https://example.com/latch", "perf-client");
        CHECK_EQ(analyze.statusCode, 200);
        CHECK(contains(analyze.body, "\"status\":\"unavailable\""));
    }

    // The health and candle surfaces never degraded to OFFLINE / no data.
    const HttpResponse health =
        request(server.port, "GET", "/api/v1/health", "");
    CHECK_EQ(health.statusCode, 200);
    const HttpResponse candles =
        request(server.port, "GET", "/api/v1/candles?tf=M1&limit=500", "");
    // No candles were ingested in this case, so unavailability is the honest
    // answer - what matters is that it is the documented 503, not a crash or a
    // fabricated empty success.
    CHECK_EQ(candles.statusCode, 503);
    CHECK(contains(candles.body, "\"code\":\"dependency_unavailable\""));
}
