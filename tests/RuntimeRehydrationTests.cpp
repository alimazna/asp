// RUNTIME-0001 - a redeployed backend must still serve an objective.
//
// The durable SQLite candle store survives a redeploy; the in-memory analysis
// DataBus does not. The multi-timeframe gate needs at least three closed bars on
// BOTH the operational (M15) and structural (H4) streams before it will pass
// (AnalyticalFeatureEngine::minTriggerBars). Because the bus was never seeded
// from the store, a freshly deployed backend started empty and reported
//
//   decision.mtf_core_gate_passed = false
//   block_reasons = ["multi-timeframe core gate: M15 and H4 streams unavailable"]
//
// until enough live candles accumulated — which is exactly the live symptom: the
// sender delivered all nine timeframes, /timeframes showed `sequence: 1` for M15,
// yet no trade plan was produced.
//
// This test runs the REAL astra_server binary against a REAL SQLite file, ingests
// history, restarts the process against the same data directory, and asserts the
// gate passes from persisted history alone — with no new ingestion after the
// restart. Linux-only.

#include "TestHarness.h"

#include "foundation/HttpClient.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

#ifndef _WIN32
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

namespace {

#ifndef _WIN32

constexpr std::int64_t kH4Seconds = 4 * 3600;
constexpr std::int64_t kM15Seconds = 15 * 60;

std::uint16_t freePort() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
}

bool waitForHealth(std::uint16_t port, int timeoutMillis) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMillis);
    while (std::chrono::steady_clock::now() < deadline) {
        HttpRequest req;
        req.port = port;
        req.path = "/api/v1/health";
        req.connectTimeoutMillis = 300;
        req.readTimeoutMillis = 1000;
        const HttpResponse r = httpGet(req);
        if (r.ok && r.statusCode == 200) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// Launch astra_server against `dataDir`; returns the child pid.
pid_t launch(const fs::path& exe, const fs::path& dataDir, std::uint16_t port) {
    const pid_t pid = ::fork();
    if (pid != 0) return pid;
    ::setsid();
    const std::string dataDirStr = dataDir.string();
    ::setenv("ASTRA_DATA_DIR", dataDirStr.c_str(), 1);
    ::setenv("ASTRA_SENDER_TOKEN", "rehydrate-test-token", 1);
    const std::string hostArg = "--host=127.0.0.1";
    const std::string portArg = "--port=" + std::to_string(port);
    const std::string exeStr = exe.string();
    ::execl(exeStr.c_str(), exeStr.c_str(), hostArg.c_str(), portArg.c_str(),
            static_cast<char*>(nullptr));
    _exit(127);
}

void stopServer(pid_t pid) {
    ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        if (::waitpid(pid, &status, WNOHANG) == pid) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, &status, 0);
}

HttpResponse postIngest(std::uint16_t port, const std::string& body) {
    HttpRequest req;
    req.port = port;
    req.path = "/api/v1/candles/ingest";
    req.method = "POST";
    req.body = body;
    req.headers = "Authorization: Bearer rehydrate-test-token\r\n";
    req.connectTimeoutMillis = 1000;
    req.readTimeoutMillis = 3000;
    return httpRequest(req);
}

HttpResponse getPath(std::uint16_t port, const std::string& path) {
    HttpRequest req;
    req.port = port;
    req.path = path;
    req.connectTimeoutMillis = 1000;
    req.readTimeoutMillis = 3000;
    return httpGet(req);
}

// Build an ingest body of `count` closed candles for `timeframe`, ending at the
// shared grid-aligned instant `endSec` (a multiple of every timeframe duration
// used here) so both gate streams are causal at the same decision instant.
// Prices are deterministic and rise so the structural trend is well-defined;
// this is fixture data for a local test, never production data.
std::string bodyFor(const std::string& timeframe, std::int64_t seconds,
                    int count, std::int64_t endSec) {
    std::vector<std::int64_t> times;
    for (int i = count; i >= 1; --i) {
        times.push_back(endSec - static_cast<std::int64_t>(i - 1) * seconds);
    }
    std::string body = "{\"source_id\":\"rehydrate-src\",\"symbol\":\"XAUUSD\","
                       "\"timeframe\":\"" + timeframe + "\",\"candles\":[";
    for (std::size_t i = 0; i < times.size(); ++i) {
        const double open = 1900.0 + static_cast<double>(i) * 0.5;
        const double close = open + 0.25;
        if (i != 0) body += ",";
        body += "{\"time\":" + std::to_string(times[i]) +
                ",\"open\":" + std::to_string(open) +
                ",\"high\":" + std::to_string(close + 0.5) +
                ",\"low\":" + std::to_string(open - 0.5) +
                ",\"close\":" + std::to_string(close) +
                ",\"tick_volume\":100}";
    }
    body += "]}";
    return body;
}

// A grid-aligned instant two days in the past. Aligning to the H4 grid keeps it
// a multiple of the M15 grid too, so both streams share one decision instant.
std::int64_t sharedEnd() {
    std::int64_t base = static_cast<std::int64_t>(std::time(nullptr)) - 2 * 86400;
    base -= base % kH4Seconds;
    return base;
}

#endif  // !_WIN32

}  // namespace

// The regression: history persisted before a restart must be visible to the
// engine after the restart, so the multi-timeframe gate can pass without waiting
// for new live candles.
TEST_CASE(restart_rehydrates_analysis_history_so_the_core_gate_passes) {
#ifdef _WIN32
    std::cout << "  [SKIP] process-level server test is Linux-only\n";
    return;
#else
    fs::path exe;
    for (const char* dir : {"build-verify", "build-linux", "build", "out"}) {
        const fs::path candidate =
            fs::path(AURA_SOURCE_DIR) / dir / "astra_server";
        if (fs::exists(candidate)) {
            exe = candidate;
            break;
        }
    }
    if (exe.empty()) {
        std::cout << "  [SKIP] astra_server binary not found under any build dir\n";
        return;
    }

    const fs::path tmp = fs::temp_directory_path() /
                         ("astra-rehydrate-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "data", ec);
    REQUIRE(fs::exists(tmp / "data"));
    const fs::path dataDir = tmp / "data";

    // --- First run: ingest genuine closed history for both gate streams. -----
    const std::uint16_t port1 = freePort();
    REQUIRE(port1 != 0);
    const pid_t pid1 = launch(exe, dataDir, port1);
    REQUIRE(pid1 >= 0);
    REQUIRE(waitForHealth(port1, 15000));

    const std::int64_t endSec = sharedEnd();
    const HttpResponse m15 = postIngest(port1, bodyFor("M15", kM15Seconds, 12, endSec));
    CHECK(m15.is2xx());
    CHECK(m15.body.find("\"state\":\"stored_new\"") != std::string::npos);
    const HttpResponse h4 = postIngest(port1, bodyFor("H4", kH4Seconds, 6, endSec));
    CHECK(h4.is2xx());
    CHECK(h4.body.find("\"state\":\"stored_new\"") != std::string::npos);

    // The store must actually hold the history, otherwise the restart below
    // proves nothing.
    const HttpResponse stored = getPath(port1, "/api/v1/candles?tf=M15&limit=100");
    CHECK(stored.is2xx());

    stopServer(pid1);

    // --- Second run: same data directory, NO new ingestion. -----------------
    // This is the redeploy. Before the fix the bus started empty here, so the
    // gate reported "M15 and H4 streams unavailable".
    const std::uint16_t port2 = freePort();
    REQUIRE(port2 != 0);
    const pid_t pid2 = launch(exe, dataDir, port2);
    REQUIRE(pid2 >= 0);
    REQUIRE(waitForHealth(port2, 15000));

    const HttpResponse analysis = getPath(port2, "/api/v1/analysis/latest");
    CHECK(analysis.is2xx());
    // The stream-unavailable reason must be gone: history was rehydrated.
    CHECK(analysis.body.find("M15 and H4 streams unavailable") ==
          std::string::npos);
    CHECK(analysis.body.find("H4 structural authority unavailable") ==
          std::string::npos);
    CHECK(analysis.body.find("M15 operational stream unavailable") ==
          std::string::npos);

    // The engine must genuinely see both streams: the gate passes and the
    // decision is evaluated (a direction may still be NONE by design, which is
    // an honest outcome, not a block).
    CHECK(analysis.body.find("\"mtf_core_gate_passed\":true") !=
          std::string::npos);

    // The bus must hold the rehydrated history, not a single bar.
    const HttpResponse tf = getPath(port2, "/api/v1/timeframes");
    CHECK(tf.is2xx());
    const std::size_t m15Pos = tf.body.find("\"timeframe\":\"M15\"");
    CHECK(m15Pos != std::string::npos);
    if (m15Pos != std::string::npos) {
        const std::size_t seqPos = tf.body.find("\"sequence\":", m15Pos);
        CHECK(seqPos != std::string::npos);
        if (seqPos != std::string::npos) {
            const std::size_t end = tf.body.find_first_not_of("0123456789",
                                                              seqPos + 11);
            const long seq =
                std::stol(tf.body.substr(seqPos + 11, end - (seqPos + 11)));
            // Twelve M15 bars were seeded, so the bus must hold all of them.
            CHECK(seq >= 12);
        }
    }

    // There is no MT5 terminal on this host, so the bridge handshake resolved no
    // symbol. The analysis must still name the instrument the persisted candles
    // actually belong to instead of reporting an empty symbol for real data.
    CHECK(analysis.body.find("\"symbol\":\"XAUUSD\"") != std::string::npos);

    // /health must reflect the rehydrated data too, not stay at its startup
    // default. This backend has no tick thread, so seeding is the only thing
    // that can publish a health observation; before the fix /health reported
    // observed_at -1 with "no state observed" for every timeframe while
    // /analysis/latest reported the gate open on the same data.
    const HttpResponse health = getPath(port2, "/api/v1/health");
    CHECK(health.is2xx());
    CHECK(health.body.find("\"observed_at\":-1") == std::string::npos);
    // The seeded streams must carry a real observation. Timeframes that were
    // never ingested may honestly report "no state observed"; M15 and H4 were
    // rehydrated and must not.
    CHECK(health.body.find("timeframe-M15: no state observed") ==
          std::string::npos);
    CHECK(health.body.find("timeframe-H4: no state observed") ==
          std::string::npos);
    // The reason text must name the actual fault. A bare "closed bar present"
    // is a positive note and must never appear as a degraded reason; the
    // seeded bars are present but their quality is UNKNOWN (no live tick), so
    // the honest reason says so.
    CHECK(health.body.find("closed bar present\"") == std::string::npos);
    CHECK(health.body.find("not decision-grade") != std::string::npos);

    stopServer(pid2);
    fs::remove_all(tmp, ec);
#endif
}
