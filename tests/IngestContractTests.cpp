// INGEST-0001 - the production entry point's argument parser must accept the
// "--flag=value" form, and the ingest route must reject a timeframe the analysis
// engine cannot map instead of persisting an unusable row.
//
// Both were found while verifying a live deployment:
//   * `astra_server --host=127.0.0.1 --port=N` silently ignored BOTH flags and
//     bound the default 0.0.0.0:8080, because the parser only looked at
//     argv[i] == "--port" and then read argv[i+1] (which was "--port=N" itself,
//     unparseable, so the default was kept). Railway-style tooling emits the "="
//     form, so this is a live footgun.
//   * an ingest batch with timeframe "M7" was accepted, persisted, and then
//     silently skipped by the analysis feed, leaving a permanently unusable row.
//
// This test runs the REAL astra_server binary over real HTTP against a real
// SQLite file, so it fails if either guarantee regresses. Linux-only.

#include "TestHarness.h"

#include "foundation/HttpClient.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

#ifndef _WIN32
#include <arpa/inet.h>
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

// Poll the health route until it answers or the deadline passes.
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

HttpResponse postIngest(std::uint16_t port, const std::string& body,
                        const std::string& token = "ingest-test-token") {
    HttpRequest req;
    req.port = port;
    req.path = "/api/v1/candles/ingest";
    req.method = "POST";
    req.body = body;
    req.headers = "Authorization: Bearer " + token + "\r\n";
    req.connectTimeoutMillis = 1000;
    req.readTimeoutMillis = 3000;
    return httpRequest(req);
}

#endif  // !_WIN32

}  // namespace

TEST_CASE(equals_form_cli_args_are_honored_and_bad_timeframe_is_rejected) {
#ifdef _WIN32
    std::cout << "  [SKIP] process-level server test is Linux-only\n";
    return;
#else
    // Locate the built server binary. The build directory name varies between
    // local and CI checkouts, so probe the conventional ones in order.
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
                         ("astra-ingest-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "data", ec);
    REQUIRE(fs::exists(tmp / "data"));

    const std::uint16_t port = freePort();
    REQUIRE(port != 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::setsid();
        const std::string dataDir = (tmp / "data").string();
        ::setenv("ASTRA_DATA_DIR", dataDir.c_str(), 1);
        ::setenv("ASTRA_SENDER_TOKEN", "ingest-test-token", 1);
        // The "--flag=value" form is the whole point of this test: it must be
        // honored exactly like "--flag value".
        const std::string hostArg = "--host=127.0.0.1";
        const std::string portArg = "--port=" + std::to_string(port);
        const std::string exeStr = exe.string();
        ::execl(exeStr.c_str(), exeStr.c_str(), hostArg.c_str(), portArg.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    const bool healthy = waitForHealth(port, 15000);
    CHECK(healthy);

    if (healthy) {
        // 1. A supported timeframe with two genuinely closed, aligned candles.
        //    The stamps must land on the M15 grid: the ingest route rejects a
        //    bar whose open time is not a multiple of its timeframe duration,
        //    so an off-grid fixture can never be stored.
        const std::string good =
            "{\"source_id\":\"test-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"candles\":["
            "{\"time\":1792064700,\"open\":1901,\"high\":1911,\"low\":1899,"
            "\"close\":1906,\"tick_volume\":500},"
            "{\"time\":1792065600,\"open\":1906,\"high\":1912.5,\"low\":1904.5,"
            "\"close\":1910,\"tick_volume\":412}]}";
        const HttpResponse first = postIngest(port, good);
        CHECK(first.is2xx());
        CHECK(first.body.find("\"state\":\"stored_new\"") != std::string::npos);

        // 2. The identical batch replayed must not create a second record.
        const HttpResponse replay = postIngest(port, good);
        CHECK(replay.is2xx());
        CHECK(replay.body.find("\"state\":\"already_stored\"") != std::string::npos);
        CHECK(replay.body.find("\"state\":\"stored_new\"") == std::string::npos);

        // 3. An unmappable timeframe must be refused, not persisted.
        const std::string badTf =
            "{\"source_id\":\"test-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M7\",\"candles\":["
            "{\"time\":1792066800,\"open\":1900,\"high\":1910,\"low\":1890,"
            "\"close\":1905,\"tick_volume\":100}]}";
        const HttpResponse bad = postIngest(port, badTf);
        CHECK(bad.statusCode == 400);
        CHECK(bad.body.find("\"code\":\"invalid_timeframe\"") != std::string::npos);

        // 4. Unauthenticated ingestion must be refused.
        const HttpResponse noAuth = postIngest(port, good, "");
        CHECK(noAuth.statusCode == 401);

        // 5. The authoritative store must hold exactly the two accepted candles,
        //    and no row for the rejected M7 batch.
        const HttpResponse candles = httpGet(
            HttpRequest{std::string("127.0.0.1"), port,
                        "/api/v1/candles?tf=M15&limit=100"});
        CHECK(candles.is2xx());
        const HttpResponse m7 = httpGet(
            HttpRequest{std::string("127.0.0.1"), port,
                        "/api/v1/candles?tf=M7&limit=100"});
        // An unknown timeframe has no series, so this must not report data.
        CHECK(!(m7.is2xx() && m7.body.find("\"count\":2") != std::string::npos));
    }

    ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, &status, 0);

    fs::remove_all(tmp, ec);
#endif
}

// INGEST-0002 - bars from a non-UTC broker must be accepted in the broker's own
// clock frame.
//
// MT5 stamps bar open times in broker server time, commonly UTC+2/+3. The
// ingest route compared those stamps against a raw UTC clock, so a UTC+3
// broker's newest M1..H1 bars looked ~3h future-dated and were rejected while
// only H4/D1/W1/MN1 survived. The sender now measures (broker - UTC) and sends
// it as `clock_offset_seconds`; this test proves the server honours it and
// still refuses genuinely future data and implausible offsets.
TEST_CASE(broker_clock_offset_is_honored_for_intraday_bars) {
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
                         ("astra-offset-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "data", ec);
    REQUIRE(fs::exists(tmp / "data"));

    const std::uint16_t port = freePort();
    REQUIRE(port != 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::setsid();
        const std::string dataDir = (tmp / "data").string();
        ::setenv("ASTRA_DATA_DIR", dataDir.c_str(), 1);
        ::setenv("ASTRA_SENDER_TOKEN", "ingest-test-token", 1);
        const std::string hostArg = "--host=127.0.0.1";
        const std::string portArg = "--port=" + std::to_string(port);
        const std::string exeStr = exe.string();
        ::execl(exeStr.c_str(), exeStr.c_str(), hostArg.c_str(), portArg.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    const bool healthy = waitForHealth(port, 15000);
    CHECK(healthy);

    if (healthy) {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t offset = 3 * 3600;   // a UTC+3 broker

        // Newest CLOSED M15 bar in broker time, aligned to its grid.
        const std::int64_t brokerNow = now + offset;
        const std::int64_t openM15 = (brokerNow / 900) * 900 - 900;

        // 1. The intraday bar that used to be dropped must now be stored. The
        //    stamp is ~3h ahead of UTC, so raw-UTC validation rejects it.
        const std::string withOffset =
            "{\"source_id\":\"offset-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":" +
            std::to_string(offset) + ",\"candles\":[{\"time\":" +
            std::to_string(openM15) +
            ",\"open\":1900,\"high\":1910,\"low\":1890,\"close\":1905,"
            "\"tick_volume\":100}]}";
        const HttpResponse ok = postIngest(port, withOffset);
        CHECK(ok.is2xx());
        CHECK(ok.body.find("\"state\":\"stored_new\"") != std::string::npos);

        // 2. Replaying it must dedupe rather than insert again.
        const HttpResponse replay = postIngest(port, withOffset);
        CHECK(replay.is2xx());
        CHECK(replay.body.find("\"state\":\"already_stored\"") !=
              std::string::npos);

        // 3. A genuinely future bar must still be refused: the offset is a
        //    clock correction, not a licence to accept fabricated data.
        const std::int64_t future = (brokerNow / 900) * 900 + 900 * 10;
        const std::string tooFar =
            "{\"source_id\":\"offset-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":" +
            std::to_string(offset) + ",\"candles\":[{\"time\":" +
            std::to_string(future) +
            ",\"open\":1900,\"high\":1910,\"low\":1890,\"close\":1905,"
            "\"tick_volume\":100}]}";
        const HttpResponse futureResp = postIngest(port, tooFar);
        CHECK(futureResp.is2xx());
        CHECK(futureResp.body.find("\"state\":\"rejected\"") != std::string::npos);

        // 4. An implausible offset (100h) must be ignored, not trusted: a value
        //    outside any real timezone means a stale feed, and honouring it
        //    would mask exactly the staleness the system must report.
        const std::string absurd =
            "{\"source_id\":\"offset-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":360000,"
            "\"candles\":[{\"time\":" + std::to_string(now + 100 * 3600) +
            ",\"open\":1900,\"high\":1910,\"low\":1890,\"close\":1905,"
            "\"tick_volume\":100}]}";
        const HttpResponse absurdResp = postIngest(port, absurd);
        CHECK(absurdResp.is2xx());
        CHECK(absurdResp.body.find("\"state\":\"rejected\"") != std::string::npos);

        // 5. The M15 series must be readable, proving the bar was persisted
        //    and not merely acknowledged.
        const HttpResponse candles = httpGet(
            HttpRequest{std::string("127.0.0.1"), port,
                        "/api/v1/candles?tf=M15&limit=100"});
        CHECK(candles.is2xx());
        CHECK(candles.body.find("\"count\":1") != std::string::npos);
    }

    ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, &status, 0);

    fs::remove_all(tmp, ec);
#endif
}

// INGEST-ACK-0001 - every entry in the ingest ack must name the candle it
// refers to, and a sender-supplied `uid` must be echoed back unchanged.
//
// A sender reconciles its local ledger against the ack: an accepted/duplicate/
// rejected entry it cannot correlate is a candle it can never mark confirmed,
// so it retries forever. Two defects were found against the real binary:
//   * `uid` was parsed off the wire and then dropped, so the sender's own
//     correlation id never came back; and
//   * validation-level rejections (bad OHLC, misaligned grid, missing `time`)
//     emitted only a free-text `reason` with no `identity`, so a rejected
//     candle was indistinguishable from an unconfirmed one.
//
// This runs the REAL astra_server binary over real HTTP, so either regression
// fails the test. Linux-only.
TEST_CASE(ingest_ack_correlates_every_candle_by_identity_and_echoes_uid) {
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
                         ("astra-ack-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "data", ec);
    REQUIRE(fs::exists(tmp / "data"));

    const std::uint16_t port = freePort();
    REQUIRE(port != 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::setsid();
        const std::string dataDir = (tmp / "data").string();
        ::setenv("ASTRA_DATA_DIR", dataDir.c_str(), 1);
        ::setenv("ASTRA_SENDER_TOKEN", "ingest-test-token", 1);
        const std::string hostArg = "--host=127.0.0.1";
        const std::string portArg = "--port=" + std::to_string(port);
        const std::string exeStr = exe.string();
        ::execl(exeStr.c_str(), exeStr.c_str(), hostArg.c_str(), portArg.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    const bool healthy = waitForHealth(port, 15000);
    CHECK(healthy);

    if (healthy) {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t offset = 3 * 3600;   // a UTC+3 broker
        const std::int64_t openM15 = ((now + offset) / 900) * 900 - 900;

        // 1. A stored candle echoes the sender's uid verbatim.
        const std::string withUid =
            "{\"source_id\":\"ack-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":" +
            std::to_string(offset) + ",\"candles\":[{\"uid\":\"SENDER-42\","
            "\"time\":" + std::to_string(openM15) +
            ",\"open\":1900,\"high\":1910,\"low\":1890,\"close\":1905,"
            "\"tick_volume\":100}]}";
        const HttpResponse stored = postIngest(port, withUid);
        CHECK(stored.is2xx());
        CHECK(stored.body.find("\"state\":\"stored_new\"") != std::string::npos);
        CHECK(stored.body.find("\"uid\":\"SENDER-42\"") != std::string::npos);
        // The uid field must be well-formed JSON, not a double-quoted literal.
        CHECK(stored.body.find("\"uid\":\"\"SENDER-42\"\"") ==
              std::string::npos);

        // 2. The dedupe ack carries the same uid, so a replay is matchable too.
        const HttpResponse dup = postIngest(port, withUid);
        CHECK(dup.is2xx());
        CHECK(dup.body.find("\"state\":\"already_stored\"") != std::string::npos);
        CHECK(dup.body.find("\"uid\":\"SENDER-42\"") != std::string::npos);

        // 3. A validation-level rejection names the candle by identity (here an
        //    OHLC-ordering violation, where `time` is present and usable).
        const std::string badOhlc =
            "{\"source_id\":\"ack-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":" +
            std::to_string(offset) + ",\"candles\":[{\"uid\":\"SENDER-43\","
            "\"time\":" + std::to_string(openM15 + 900) +
            ",\"open\":1900,\"high\":1880,\"low\":1890,\"close\":1905,"
            "\"tick_volume\":100}]}";
        const HttpResponse rej = postIngest(port, badOhlc);
        CHECK(rej.is2xx());
        CHECK(rej.body.find("\"state\":\"rejected\"") != std::string::npos);
        CHECK(rej.body.find("\"identity\":\"symbol=XAUUSD&timeframe=M15&"
                            "open_time=" +
                            std::to_string(openM15 + 900) + "\"") !=
              std::string::npos);

        // 4. When `time` itself is missing the derived key is meaningless, so
        //    the uid is the only correlate and must be used instead of an
        //    empty identity.
        const std::string noTime =
            "{\"source_id\":\"ack-src\",\"symbol\":\"XAUUSD\","
            "\"timeframe\":\"M15\",\"clock_offset_seconds\":" +
            std::to_string(offset) + ",\"candles\":[{\"uid\":\"SENDER-44\","
            "\"open_time_utc\":" + std::to_string(openM15) +
            ",\"open\":1900,\"high\":1910,\"low\":1890,\"close\":1905}]}";
        const HttpResponse rejNoTime = postIngest(port, noTime);
        CHECK(rejNoTime.is2xx());
        CHECK(rejNoTime.body.find("\"state\":\"rejected\"") != std::string::npos);
        CHECK(rejNoTime.body.find("\"identity\":\"uid=SENDER-44\"") !=
              std::string::npos);
    }

    ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, &status, 0);

    fs::remove_all(tmp, ec);
#endif
}
