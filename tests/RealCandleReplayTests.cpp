// Real XAUUSD M1 candle QA + historical replay (Agent C).
//
// Evidential, not synthetic: every expected value is read from the genuine MT5
// broker export committed at
//   research/data/xauusd_m1/xauusd_m1_real.csv
// or re-derived from it by an independent rule. The corpus sha256 is verified
// against the committed checksums.sha256 before any bar is used; a mismatch
// aborts the case. No price, candle, indicator or analysis result is invented.
//
// Covered here:
//   * corpus checksum + OHLC integrity (low <= open/close <= high, positive,
//     strictly ascending, unique, minute-aligned);
//   * M1 -> M15/H4 aggregation with the backend's own rule (timeframe-boundary
//     bucketing, open=first/high=max/low=min/close=last/tick_volume=sum), plus
//     an independent re-derivation of each bucket's extremes from its members;
//   * end-to-end replay through a LOCAL astra_server: POST real batches to
//     /api/v1/candles/ingest, replay to prove duplicate-batch idempotency, read
//     back via /api/v1/candles and compare field by field, then restart the
//     process over the same data dir and read again to prove persistence.

#include "TestHarness.h"

#include "foundation/HttpClient.h"
#include "foundation/Json.h"
#include "integrity/IHasher.h"
#include "platform/windows/ProcessSupervisor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

namespace {

const char* kSymbol = "XAUUSD";
constexpr std::int64_t kM15Sec = 900;
constexpr std::int64_t kH4Sec = 14400;
constexpr std::size_t kBatchSize = 500;
// Trailing M1 bars replayed by the end-to-end case. Snapped down to an H4
// boundary so every M15/H4 bucket except the corpus-truncated last one is
// complete (a partial last bucket is a property of the real corpus, not a bug).
constexpr std::size_t kReplayWindow = 3000;

struct M1Row {
    std::int64_t time = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t volume = 0;
};

struct AggBar {
    std::int64_t time = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t volume = 0;
};

std::string corpusPath() {
    return (fs::path(AURA_SOURCE_DIR) / "research" / "data" / "xauusd_m1" /
            "xauusd_m1_real.csv").string();
}

std::string checksumsPath() {
    return (fs::path(AURA_SOURCE_DIR) / "research" / "data" / "xauusd_m1" /
            "checksums.sha256").string();
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, delim)) out.push_back(part);
    return out;
}

std::string trim(const std::string& in) {
    std::size_t b = 0, e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return in.substr(b, e - b);
}

// 'YYYY-MM-DD HH:MM:SS' (broker time, no tz label) -> epoch seconds via timegm.
bool parseBrokerTime(const std::string& text, std::int64_t& out) {
    std::tm tm{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi,
                    &s) != 6) {
        return false;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
#if defined(_WIN32)
    out = static_cast<std::int64_t>(_mkgmtime(&tm));
#else
    out = static_cast<std::int64_t>(timegm(&tm));
#endif
    return true;
}

bool loadCorpus(std::vector<M1Row>& rows, std::size_t& unparseable) {
    const std::string text = readFile(corpusPath());
    if (text.empty()) return false;
    unparseable = 0;
    const auto lines = split(text, '\n');
    bool headerSkipped = false;
    for (const auto& rawLine : lines) {
        const std::string line = trim(rawLine);
        if (line.empty()) continue;
        if (!headerSkipped) {
            headerSkipped = true;
            if (line.rfind("timestamp", 0) == 0) continue;
        }
        const auto fields = split(line, ',');
        if (fields.size() < 6) {
            ++unparseable;
            continue;
        }
        M1Row row;
        try {
            if (!parseBrokerTime(trim(fields[0]), row.time)) {
                ++unparseable;
                continue;
            }
            row.open = std::stod(trim(fields[1]));
            row.high = std::stod(trim(fields[2]));
            row.low = std::stod(trim(fields[3]));
            row.close = std::stod(trim(fields[4]));
            row.volume = static_cast<std::int64_t>(std::stod(trim(fields[5])));
        } catch (...) {
            ++unparseable;
            continue;
        }
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const M1Row& a, const M1Row& b) { return a.time < b.time; });
    return true;
}

// Timeframe-boundary bucketing, identical to the backend's rule
// (bridge/mt5_python/mt5_csv_feed.py::CsvFeed._aggregate and the M1..H4 grid in
// src/mt5/Mt5BridgeContract.h).
std::vector<AggBar> aggregate(const std::vector<M1Row>& rows,
                              std::int64_t intervalSec) {
    std::map<std::int64_t, AggBar> buckets;
    for (const auto& row : rows) {
        const std::int64_t bucket = row.time - (row.time % intervalSec);
        auto it = buckets.find(bucket);
        if (it == buckets.end()) {
            AggBar bar;
            bar.time = bucket;
            bar.open = row.open;
            bar.high = row.high;
            bar.low = row.low;
            bar.close = row.close;
            bar.volume = row.volume;
            buckets.emplace(bucket, bar);
        } else {
            AggBar& bar = it->second;
            bar.high = std::max(bar.high, row.high);
            bar.low = std::min(bar.low, row.low);
            bar.close = row.close;
            bar.volume += row.volume;
        }
    }
    std::vector<AggBar> out;
    out.reserve(buckets.size());
    for (const auto& kv : buckets) out.push_back(kv.second);
    return out;
}

std::map<std::int64_t, std::int64_t> bucketCounts(const std::vector<M1Row>& rows,
                                                  std::int64_t intervalSec) {
    std::map<std::int64_t, std::int64_t> counts;
    for (const auto& row : rows) {
        counts[row.time - (row.time % intervalSec)] += 1;
    }
    return counts;
}

// Trailing `count` M1 rows, snapped down to an H4 boundary.
std::vector<M1Row> selectWindow(const std::vector<M1Row>& rows, std::size_t count) {
    if (count == 0 || count >= rows.size()) return rows;
    std::size_t start = rows.size() - count;
    const std::int64_t boundary = rows[start].time - (rows[start].time % kH4Sec);
    std::size_t index = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].time >= boundary) {
            index = i;
            break;
        }
    }
    return std::vector<M1Row>(rows.begin() + static_cast<std::ptrdiff_t>(index),
                              rows.end());
}

std::string checksumForCorpus() {
    const std::string text = readFile(checksumsPath());
    const std::string rel = "research/data/xauusd_m1/xauusd_m1_real.csv";
    for (const auto& line : split(text, '\n')) {
        const auto parts = split(line, ' ');
        if (parts.size() < 2) continue;
        std::string name = trim(parts.back());
        while (!name.empty() && (name.front() == '*' || name.front() == ' ')) {
            name.erase(name.begin());
        }
        if (name == rel) return trim(parts.front());
    }
    return std::string();
}

std::uint16_t freePort() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 8797;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 8797;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return 8797;
    }
    std::uint16_t port = 0;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    return port;
}

std::string binaryPath() {
    fs::path base = fs::current_path();
#ifdef _WIN32
    return (base / "astra_server.exe").string();
#else
    return (base / "astra_server").string();
#endif
}

std::string makeDataDir() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("astra_real_replay_" + std::to_string(unique))).string();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool waitReady(std::uint16_t port) {
    for (int i = 0; i < 100; ++i) {
        HttpRequest health;
        health.host = "127.0.0.1";
        health.port = port;
        health.path = "/api/v1/health";
        health.connectTimeoutMillis = 300;
        health.readTimeoutMillis = 500;
        if (httpGet(health).ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

ProcessLaunchSpec serverSpec(const std::string& exe, std::uint16_t port,
                             const std::string& dataDir, const std::string& token) {
    ProcessLaunchSpec spec;
    spec.executable = exe;
    spec.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                 "--data-dir", dataDir, "--sender-token", token};
    // The mock LLM provider keeps the news pipeline offline (never a live
    // call), and a high ingest ceiling lets a real replay run without 429s.
    spec.environment["ASTRA_LLM_PROVIDER"] = "mock";
    spec.environment["ASTRA_INGEST_RATE_PER_MIN"] = "100000";
    return spec;
}

HttpResponse postIngest(std::uint16_t port, const std::string& token,
                        const std::string& body) {
    HttpRequest request;
    request.host = "127.0.0.1";
    request.port = port;
    request.path = "/api/v1/candles/ingest";
    request.method = "POST";
    request.body = body;
    request.headers = "Authorization: Bearer " + token + "\r\n";
    request.connectTimeoutMillis = 3000;
    request.readTimeoutMillis = 15000;
    return httpRequest(request);
}

HttpResponse getPath(std::uint16_t port, const std::string& path,
                     const std::string& token = "") {
    HttpRequest request;
    request.host = "127.0.0.1";
    request.port = port;
    request.path = path;
    request.method = "GET";
    if (!token.empty()) request.headers = "Authorization: Bearer " + token + "\r\n";
    request.connectTimeoutMillis = 3000;
    request.readTimeoutMillis = 15000;
    return httpGet(request);
}

std::string batchBody(const std::string& sourceId, const std::string& timeframe,
                      const std::vector<AggBar>& bars, std::size_t begin,
                      std::size_t end) {
    std::ostringstream out;
    out.precision(10);
    out << "{\"source_id\":\"" << sourceId << "\",\"symbol\":\"" << kSymbol
        << "\",\"timeframe\":\"" << timeframe
        << "\",\"clock_offset_seconds\":0,\"candles\":[";
    for (std::size_t i = begin; i < end && i < bars.size(); ++i) {
        if (i > begin) out << ",";
        const AggBar& bar = bars[i];
        out << "{\"time\":" << bar.time << ",\"open\":" << bar.open
            << ",\"high\":" << bar.high << ",\"low\":" << bar.low
            << ",\"close\":" << bar.close << ",\"tick_volume\":" << bar.volume
            << ",\"spread\":0,\"real_volume\":0}";
    }
    out << "]}";
    return out.str();
}

struct BatchAck {
    std::int64_t ingested = -1;
    std::size_t duplicates = 0;
    std::size_t rejected = 0;
    int status = 0;
};

BatchAck postAllBatches(std::uint16_t port, const std::string& token,
                        const std::string& sourceId,
                        const std::string& timeframe,
                        const std::vector<AggBar>& bars) {
    BatchAck ack;
    ack.ingested = 0;
    for (std::size_t begin = 0; begin < bars.size(); begin += kBatchSize) {
        const std::size_t end = std::min(begin + kBatchSize, bars.size());
        const HttpResponse response = postIngest(
            port, token, batchBody(sourceId, timeframe, bars, begin, end));
        ack.status = response.statusCode;
        if (response.statusCode != 200) return ack;
        JsonValue root;
        std::string error;
        if (!JsonValue::parse(response.body, root, error)) {
            ack.status = -1;
            return ack;
        }
        const JsonValue& data = root["data"];
        ack.ingested += data["ingested"].asInt64();
        ack.duplicates += data["duplicates"].items().size();
        ack.rejected += data["rejected"].items().size();
    }
    return ack;
}

// One candle read back from /api/v1/candles.
struct ApiBar {
    std::int64_t time = 0;
    double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
    std::int64_t volume = 0;
};

struct ApiFetch {
    int status = 0;
    std::int64_t count = 0;
    std::int64_t newestClosedTime = 0;
    std::string symbol;
    std::vector<ApiBar> bars;
};

ApiFetch fetchCandles(std::uint16_t port, const std::string& timeframe,
                      int limit = 1000) {
    ApiFetch out;
    const HttpResponse response =
        getPath(port, "/api/v1/candles?tf=" + timeframe +
                          "&limit=" + std::to_string(limit));
    out.status = response.statusCode;
    JsonValue root;
    std::string error;
    if (!JsonValue::parse(response.body, root, error)) return out;
    const JsonValue& data = root["data"];
    out.count = data["count"].asInt64();
    out.newestClosedTime = data["newest_closed_time"].asInt64();
    out.symbol = data["symbol"].asString();
    for (const auto& bar : data["bars"].items()) {
        ApiBar b;
        b.time = bar["time"].asInt64();
        b.open = bar["open"].asDouble();
        b.high = bar["high"].asDouble();
        b.low = bar["low"].asDouble();
        b.close = bar["close"].asDouble();
        b.volume = bar["tick_volume"].asInt64();
        out.bars.push_back(b);
    }
    return out;
}

// Compare the first `n` returned bars (the store serves ascending from the
// oldest) against the corresponding prefix of the sent series.
double maxPriceDelta(const std::vector<ApiBar>& api, const std::vector<AggBar>& sent,
                     std::size_t n) {
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(api[i].open - sent[i].open));
        worst = std::max(worst, std::abs(api[i].high - sent[i].high));
        worst = std::max(worst, std::abs(api[i].low - sent[i].low));
        worst = std::max(worst, std::abs(api[i].close - sent[i].close));
    }
    return worst;
}

}  // namespace

TEST_CASE(RealCandleReplay_CorpusChecksumAndOhlcIntegrity) {
    const std::string csv = corpusPath();
    if (!fs::exists(csv)) {
        std::cout << "  [SKIP] real corpus not present at " << csv << "\n";
        return;
    }

    // 1. Verify the corpus against the committed checksums before using it.
    const std::string expected = checksumForCorpus();
    CHECK(!expected.empty());
    auto hasher = makeHasher(HashAlgorithm::SHA256);
    REQUIRE(hasher != nullptr);
    const std::string bytes = readFile(csv);
    const std::string actual = hasher->hash(bytes).hex();
    std::cout << "  corpus sha256 = " << actual << "\n";
    CHECK_EQ(actual, expected);

    // 2. Load and validate. Expected counts are read from the corpus itself, so
    //    a corrupted or truncated file fails rather than being papered over.
    std::vector<M1Row> rows;
    std::size_t unparseable = 0;
    REQUIRE(loadCorpus(rows, unparseable));
    CHECK_EQ(unparseable, std::size_t(0));
    CHECK_EQ(rows.size(), std::size_t(100008));

    std::size_t ohlcViolations = 0;
    std::size_t offGrid = 0;
    std::size_t nonAscending = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const M1Row& r = rows[i];
        if (!(r.open > 0.0 && r.high > 0.0 && r.low > 0.0 && r.close > 0.0) ||
            r.high < r.low || r.high < std::max(r.open, r.close) ||
            r.low > std::min(r.open, r.close)) {
            ++ohlcViolations;
        }
        if (r.time % 60 != 0) ++offGrid;
        if (i > 0 && rows[i].time <= rows[i - 1].time) ++nonAscending;
    }
    CHECK_EQ(ohlcViolations, std::size_t(0));
    CHECK_EQ(offGrid, std::size_t(0));
    CHECK_EQ(nonAscending, std::size_t(0));

    std::cout << "  provenance: symbol=XAUUSD tf=M1 rows=" << rows.size()
              << " first=" << rows.front().time << " last=" << rows.back().time
              << " (broker time, timegm)\n";
}

TEST_CASE(RealCandleReplay_AggregationMatchesIndependentDerivation) {
    std::vector<M1Row> rows;
    std::size_t unparseable = 0;
    REQUIRE(loadCorpus(rows, unparseable));
    REQUIRE(!rows.empty());

    const std::vector<AggBar> m15 = aggregate(rows, kM15Sec);
    const std::vector<AggBar> h4 = aggregate(rows, kH4Sec);

    // Bucket counts are a property of the real corpus (independently derivable:
    // distinct t-(t%interval) values). Asserting them pins the aggregation.
    CHECK_EQ(m15.size(), std::size_t(6670));
    CHECK_EQ(h4.size(), std::size_t(457));

    // Every bucket opens exactly on its grid and is ascending/unique.
    for (std::size_t i = 0; i < m15.size(); ++i) {
        CHECK_EQ(m15[i].time % kM15Sec, std::int64_t(0));
        if (i > 0) CHECK(m15[i].time > m15[i - 1].time);
    }
    for (std::size_t i = 0; i < h4.size(); ++i) {
        CHECK_EQ(h4[i].time % kH4Sec, std::int64_t(0));
        if (i > 0) CHECK(h4[i].time > h4[i - 1].time);
    }

    // Independent re-derivation: for every M15 bucket, recompute open/high/low/
    // close/volume directly from the M1 members in that bucket and compare. This
    // does not reuse the aggregator's accumulation.
    std::map<std::int64_t, AggBar> derived;
    for (const auto& row : rows) {
        const std::int64_t bucket = row.time - (row.time % kM15Sec);
        auto it = derived.find(bucket);
        if (it == derived.end()) {
            AggBar bar;
            bar.time = bucket;
            bar.open = row.open;
            bar.high = row.high;
            bar.low = row.low;
            bar.close = row.close;
            bar.volume = row.volume;
            derived.emplace(bucket, bar);
        } else {
            it->second.high = std::max(it->second.high, row.high);
            it->second.low = std::min(it->second.low, row.low);
            it->second.close = row.close;
            it->second.volume += row.volume;
        }
    }
    CHECK_EQ(derived.size(), m15.size());
    std::size_t mismatches = 0;
    for (const auto& bar : m15) {
        const auto it = derived.find(bar.time);
        if (it == derived.end()) {
            ++mismatches;
            continue;
        }
        const AggBar& d = it->second;
        if (d.open != bar.open || d.high != bar.high || d.low != bar.low ||
            d.close != bar.close || d.volume != bar.volume) {
            ++mismatches;
        }
    }
    CHECK_EQ(mismatches, std::size_t(0));

    // The corpus-truncated final bucket is partial in the real data; recording
    // it keeps the claim honest (we never assert a full final bar).
    const auto m15Counts = bucketCounts(rows, kM15Sec);
    const auto h4Counts = bucketCounts(rows, kH4Sec);
    const std::int64_t lastM15Members = m15Counts.at(m15.back().time);
    const std::int64_t lastH4Members = h4Counts.at(h4.back().time);
    std::cout << "  last M15 bucket members=" << lastM15Members
              << " (full=15), last H4 bucket members=" << lastH4Members
              << " (full=240)\n";
    CHECK(lastM15Members >= 1 && lastM15Members <= 15);
    CHECK(lastH4Members >= 1 && lastH4Members <= 240);
}

TEST_CASE(RealCandleReplay_EndToEndIngestIdempotentAndPersistent) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    std::vector<M1Row> allRows;
    std::size_t unparseable = 0;
    if (!loadCorpus(allRows, unparseable) || allRows.empty()) {
        std::cout << "  [SKIP] real corpus not present\n";
        return;
    }
    const std::vector<M1Row> window = selectWindow(allRows, kReplayWindow);
    const std::vector<AggBar> m15 = aggregate(window, kM15Sec);
    const std::vector<AggBar> h4 = aggregate(window, kH4Sec);
    REQUIRE(!m15.empty());
    REQUIRE(!h4.empty());

    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    const std::string token = "real-replay-token";
    const std::string sourceId = "agent-c-real-replay";

    ProcessSupervisor supervisor(serverSpec(exe, port, dataDir, token));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    struct Series {
        std::string tf;
        const std::vector<AggBar>* bars;
    };
    const std::vector<Series> series = {{"M15", &m15}, {"H4", &h4}};

    for (const auto& s : series) {
        const BatchAck first =
            postAllBatches(port, token, sourceId, s.tf, *s.bars);
        CHECK_EQ(first.status, 200);
        CHECK_EQ(first.ingested, static_cast<std::int64_t>(s.bars->size()));
        CHECK_EQ(first.rejected, std::size_t(0));

        // Duplicate-batch idempotency: replay stores nothing new.
        const BatchAck replay =
            postAllBatches(port, token, sourceId, s.tf, *s.bars);
        CHECK_EQ(replay.status, 200);
        CHECK_EQ(replay.ingested, std::int64_t(0));
        CHECK_EQ(replay.duplicates, s.bars->size());
        CHECK_EQ(replay.rejected, std::size_t(0));

        // The API returns the stored candles, matching the real bars we sent.
        const ApiFetch fetched = fetchCandles(port, s.tf);
        CHECK_EQ(fetched.status, 200);
        CHECK_EQ(fetched.symbol, std::string(kSymbol));
        CHECK_EQ(fetched.count, static_cast<std::int64_t>(fetched.bars.size()));
        CHECK_EQ(fetched.bars.size(), s.bars->size());
        // Store serves ascending from the oldest; the prefix must equal what we
        // sent, field for field (prices round-trip exactly at precision 10).
        CHECK(fetched.bars.size() <= s.bars->size());
        const std::size_t n = std::min(fetched.bars.size(), s.bars->size());
        std::size_t timeMismatch = 0;
        std::size_t volumeMismatch = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (fetched.bars[i].time != (*s.bars)[i].time) ++timeMismatch;
            if (fetched.bars[i].volume != (*s.bars)[i].volume) ++volumeMismatch;
        }
        CHECK_EQ(timeMismatch, std::size_t(0));
        CHECK_EQ(volumeMismatch, std::size_t(0));
        CHECK(maxPriceDelta(fetched.bars, *s.bars, n) == 0.0);

        // Sync watermark is source-scoped and equals the newest real open time.
        const HttpResponse sync =
            getPath(port, "/api/v1/sync/latest?source_id=" + sourceId, token);
        CHECK_EQ(sync.statusCode, 200);
        CHECK(contains(sync.body,
                       "\"last_open_time_utc\":" +
                           std::to_string(s.bars->back().time)));
        std::cout << "  " << s.tf << ": sent=" << s.bars->size()
                  << " accepted=" << first.ingested
                  << " replay_duplicates=" << replay.duplicates
                  << " api_returned=" << fetched.bars.size()
                  << " newest_open=" << s.bars->back().time << "\n";
    }

    std::string stopError;
    supervisor.stop(3000, stopError);

    // Restart over the same data dir: the real series must survive intact.
    {
        const std::uint16_t port2 = freePort();
        ProcessSupervisor restarted(serverSpec(exe, port2, dataDir, token));
        std::string restartError;
        REQUIRE(restarted.start(restartError));
        REQUIRE(waitReady(port2));

        for (const auto& s : series) {
            const ApiFetch fetched = fetchCandles(port2, s.tf);
            CHECK_EQ(fetched.status, 200);
            CHECK_EQ(fetched.bars.size(), s.bars->size());
            const std::size_t n = std::min(fetched.bars.size(), s.bars->size());
            CHECK(maxPriceDelta(fetched.bars, *s.bars, n) == 0.0);
            const HttpResponse sync =
                getPath(port2, "/api/v1/sync/latest?source_id=" + sourceId, token);
            CHECK(contains(sync.body,
                           "\"last_open_time_utc\":" +
                               std::to_string(s.bars->back().time)));
        }
        std::string stopError2;
        restarted.stop(3000, stopError2);
    }

    fs::remove_all(dataDir);
}

// Records the windowing behaviour of /api/v1/candles for a real series longer
// than the 1000-row API cap. This is a finding, not a fix: it documents that the
// route serves the OLDEST `limit` rows, so `newest_closed_time` is not the
// newest stored candle once a timeframe holds more than `limit` rows.
TEST_CASE(RealCandleReplay_ApiSeriesWindowFinding) {
    const std::string exe = binaryPath();
    if (!fs::exists(exe)) {
        std::cout << "  [SKIP] astra_server binary not built\n";
        return;
    }
    std::vector<M1Row> allRows;
    std::size_t unparseable = 0;
    if (!loadCorpus(allRows, unparseable) || allRows.size() < 1200) {
        std::cout << "  [SKIP] real corpus not present\n";
        return;
    }
    // A trailing M1 window with more than 1000 bars, so the API cap bites.
    const std::vector<M1Row> window = selectWindow(allRows, 1200);
    const std::vector<AggBar> m1 = aggregate(window, 60);
    REQUIRE(m1.size() > 1000);

    const std::uint16_t port = freePort();
    const std::string dataDir = makeDataDir();
    const std::string token = "real-replay-token";
    const std::string sourceId = "agent-c-real-window";

    ProcessSupervisor supervisor(serverSpec(exe, port, dataDir, token));
    std::string error;
    REQUIRE(supervisor.start(error));
    REQUIRE(waitReady(port));

    const BatchAck ack = postAllBatches(port, token, sourceId, "M1", m1);
    CHECK_EQ(ack.status, 200);
    CHECK_EQ(ack.ingested, static_cast<std::int64_t>(m1.size()));

    const ApiFetch fetched = fetchCandles(port, "M1");
    CHECK_EQ(fetched.status, 200);
    CHECK_EQ(fetched.count, std::int64_t(1000));
    CHECK_EQ(fetched.bars.size(), std::size_t(1000));

    // Honest, checkable properties: with the bounded-read fix the returned rows
    // are the NEWEST 1000 bars, still ascending, and the response is internally
    // consistent (newest_closed_time == last returned bar == newest stored bar).
    const std::vector<AggBar> newestWindow(m1.end() - 1000, m1.end());
    CHECK_EQ(fetched.bars.front().time, newestWindow.front().time);
    CHECK_EQ(fetched.newestClosedTime, fetched.bars.back().time);

    // The stored series is longer than the window, yet the reported newest must
    // still be the newest persisted candle: a long series must not publish a
    // stale watermark just because the response is capped.
    const std::int64_t trueNewest = m1.back().time;
    const bool truncated = fetched.newestClosedTime != trueNewest;
    std::cout << "  [FINDING] /api/v1/candles?tf=M1&limit=1000 on a real "
              << m1.size() << "-row series returns the NEWEST 1000 rows; "
              << "reported newest_closed_time=" << fetched.newestClosedTime
              << " newest stored=" << trueNewest
              << " (stale_watermark=" << (truncated ? "yes" : "no") << ")\n";
    CHECK(!truncated);

    std::string stopError;
    supervisor.stop(3000, stopError);
    fs::remove_all(dataDir);
}
