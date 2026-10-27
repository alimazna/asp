// ASTRA public server entry point (Linux / Railway).
//
// Binds a public host:port (0.0.0.0:$PORT), persists ingested candles in a
// durable SQLite store, feeds them into the AURA runtime so the existing
// analysis pipeline runs unchanged, and serves the frozen v1 read surface plus
// the additive ingestion / sync routes.
//
// Required environment:
//   ASTRA_SENDER_TOKEN  (comma-separated bearer tokens accepted from senders)
//   ASTRA_DATA_DIR      (durable data directory; default ./data)
//   PORT                (bind port; default 8080)
//   ASTRA_HOST          (bind host; default 0.0.0.0)
//   ASTRA_ALLOWED_ORIGINS (comma-separated browser origins for CORS)

#include "api/AstraApiServer.h"
#include "api/BackendFacade.h"
#include "api/AnalysisApi.h"
#include "api/NewsApi.h"
#include "api/ProbabilityApi.h"
#include "data/BarNormalizer.h"
#include "foundation/Json.h"
#include "foundation/OutboundHttpClient.h"
#include "governance/ApprovalGate.h"
#include "governance/IncidentManager.h"
#include "news/Llm7Provider.h"
#include "news/MockLlmProvider.h"
#include "news/NewsAnalysisService.h"
#include "news/NewsStore.h"
#include "persistence/CandleStore.h"
#include "runtime/AuraRuntime.h"
#include "telegram/TelegramGateway.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace aura {
namespace {

volatile std::sig_atomic_t g_stopRequested = 0;
void handleSignal(int) { g_stopRequested = 1; }

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

int parseIntEnv(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    try {
        return std::stoi(std::string(v));
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> splitComma(const std::string& in) {
    std::vector<std::string> out;
    std::stringstream ss(in);
    std::string part;
    while (std::getline(ss, part, ',')) {
        std::string t = part;
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) {
            t.erase(t.begin());
        }
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) {
            t.pop_back();
        }
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// Largest |broker - UTC| accepted as a genuine timezone offset. Real brokers
// sit within a few hours of UTC; the extreme real timezone is UTC+14. A larger
// value means a stale feed rather than an exotic clock, and accepting it would
// mask exactly the staleness this system must never hide.
constexpr std::int64_t kMaxClockOffsetSeconds = 18 * 3600;

// Read the optional broker clock offset from an ingest body, clamped to a
// plausible timezone band. Absent or unparseable values mean "unknown", which
// degrades to the previous raw-UTC behaviour rather than guessing.
std::int64_t parseClockOffset(const JsonValue& root) {
    const JsonValue& value = root["clock_offset_seconds"];
    if (!value.isNumber()) return 0;
    std::int64_t offset = value.asInt64();
    if (offset > kMaxClockOffsetSeconds || offset < -kMaxClockOffsetSeconds) {
        return 0;
    }
    return offset;
}

// Percent-decode a URL query value (also turns '+' into a space). The sync
// route takes a source identity from the query string, and identities may
// legitimately contain characters a client has to escape.
std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < in.size()) {
            const auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

int parsePort(const std::string& text, int fallback) {
    try {
        const int p = std::stoi(text);
        return (p > 0 && p < 65536) ? p : fallback;
    } catch (...) {
        return fallback;
    }
}

// Parse one candle object from a JSON value.
//
// `clockOffsetSeconds` is the broker clock offset the sender measured
// (broker server time - UTC). MT5 stamps bar open times in broker server time,
// commonly UTC+2/+3, so the "future" guard has to be evaluated in that same
// frame. Without it every intraday bar from a non-UTC broker looked ~3h
// future-dated and was dropped, which is why only H4/D1/W1/MN1 were stored.
bool parseCandle(const JsonValue& candle, StoredCandle& out,
                 std::string& error,
                 std::int64_t clockOffsetSeconds = 0) {
    if (!candle.isObject()) {
        error = "candle is not an object";
        return false;
    }
    // Required numeric fields.
    const JsonValue& time = candle["time"];
    if (!time.isNumber() && !time.isString()) {
        error = "candle 'time' missing or not numeric";
        return false;
    }
    auto readPrice = [&candle](const char* key, double& target,
                               bool& present) -> bool {
        const JsonValue& value = candle[key];
        if (value.isNumber()) {
            target = value.asDouble();
            present = true;
            return true;
        }
        present = false;
        return true;   // optional price -> represented as 0 with the flag
    };

    out.openTimeUtc = time.asInt64();

    // Closed candles are historical: an open time far in the future means the
    // record is malformed or fabricated. The comparison uses the sender's
    // broker clock, because MT5 stamps bars in broker server time; a few
    // minutes of slack tolerates ordinary clock skew between the sender and
    // this server.
    const int64_t kFutureSkewSlackSeconds = 300;
    const int64_t nowUtc =
        static_cast<int64_t>(std::time(nullptr)) + clockOffsetSeconds;
    if (out.openTimeUtc > nowUtc + kFutureSkewSlackSeconds) {
        error = "candle open_time is in the future";
        return false;
    }
    if (out.openTimeUtc <= 0) {
        error = "candle open_time must be a positive epoch timestamp";
        return false;
    }

    bool hasOpen = false, hasHigh = false, hasLow = false, hasClose = false;
    readPrice("open", out.open, hasOpen);
    readPrice("high", out.high, hasHigh);
    readPrice("low", out.low, hasLow);
    readPrice("close", out.close, hasClose);
    if (!hasOpen || !hasHigh || !hasLow || !hasClose) {
        error = "candle requires open/high/low/close";
        return false;
    }

    out.tickVolume = candle["tick_volume"].isNumber()
                         ? candle["tick_volume"].asInt64()
                         : 0;
    out.spread = candle["spread"].isNumber() ? candle["spread"].asInt64() : 0;
    out.realVolume = candle["real_volume"].isNumber()
                         ? candle["real_volume"].asInt64()
                         : 0;

    // Sanity: OHLC must be positive and internally consistent. This mirrors the
    // C++ DataValidator used by the runtime (defense in depth).
    if (!(out.open > 0.0 && out.high > 0.0 && out.low > 0.0 && out.close > 0.0)) {
        error = "candle prices must be positive";
        return false;
    }
    const double bodyHigh = std::max(out.open, out.close);
    const double bodyLow = std::min(out.open, out.close);
    if (out.high < out.low || out.high < bodyHigh || out.low > bodyLow) {
        error = "candle OHLC ordering violated";
        return false;
    }
    if (out.tickVolume < 0 || out.realVolume < 0 || out.spread < 0) {
        error = "candle volumes/spread must be non-negative";
        return false;
    }
    return true;
}

}  // namespace
}  // namespace aura

int main(int argc, char** argv) {
    using namespace aura;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string execName =
        argc > 0 ? std::filesystem::path(argv[0]).filename().string()
                 : "astra_server";
    std::cout << execName << " starting" << std::endl;

    // Process-identifying arguments (before server start).
    bool printHelp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printHelp = true;
        }
    }
    if (printHelp) {
        std::cout
            << "Usage: " << execName
            << " [--data-dir DIR] [--port N] [--host HOST] [--sender-token TOK]"
            << " [--admin-token TOK] [--allowed-origin ORIGIN[,ORIGIN...]]\n"
            << "Environment: ASTRA_DATA_DIR, ASTRA_HOST, PORT,"
               " ASTRA_SENDER_TOKEN, ASTRA_ADMIN_TOKEN, ASTRA_ALLOWED_ORIGINS\n";
        return 0;
    }

    // Read configuration.
    std::string dataDir = envOr("ASTRA_DATA_DIR", "./data");
    std::string host = envOr("ASTRA_HOST", "0.0.0.0");
    int port = parsePort(envOr("PORT", "8080"), 8080);
    std::vector<std::string> tokens = splitComma(
        envOr("ASTRA_SENDER_TOKEN", ""));
    // Administrative credential for the destructive purge surface. Absent by
    // default, so the admin route is disabled unless explicitly configured.
    std::vector<std::string> adminTokens = splitComma(
        envOr("ASTRA_ADMIN_TOKEN", ""));

    std::vector<std::string> cliOrigins;
    for (int i = 1; i < argc; ++i) {
        std::string key(argv[i]);
        std::string inlineValue;
        // Accept both "--flag value" and "--flag=value". Railway and other
        // container tooling commonly emit the "=" form, and silently falling
        // back to the default port when it is used is hard to notice.
        const auto eq = key.find('=');
        if (eq != std::string::npos) {
            inlineValue = key.substr(eq + 1);
            key = key.substr(0, eq);
        }
        const bool hasNext = (i + 1 < argc);
        const auto value = [&]() -> std::string {
            return inlineValue.empty() && hasNext ? std::string(argv[i + 1]) : inlineValue;
        };
        const bool consumeNext = inlineValue.empty() && hasNext;

        if (key == "--data-dir" && (consumeNext || !inlineValue.empty())) {
            dataDir = value();
            if (consumeNext) ++i;
        } else if (key == "--port" && (consumeNext || !inlineValue.empty())) {
            port = parsePort(value(), port);
            if (consumeNext) ++i;
        } else if (key == "--host" && (consumeNext || !inlineValue.empty())) {
            host = value();
            if (consumeNext) ++i;
        } else if (key == "--sender-token" && (consumeNext || !inlineValue.empty())) {
            tokens = {value()};
            if (consumeNext) ++i;
        } else if (key == "--admin-token" && (consumeNext || !inlineValue.empty())) {
            adminTokens = splitComma(value());
            if (consumeNext) ++i;
        } else if (key == "--allowed-origin" && (consumeNext || !inlineValue.empty())) {
            cliOrigins = splitComma(value());
            if (consumeNext) ++i;
        }
    }

    if (tokens.empty()) {
        std::cerr << "FATAL: no sender tokens configured. Set ASTRA_SENDER_TOKEN "
                     "or pass --sender-token."
                  << std::endl;
        return 2;
    }

    std::error_code fsError;
    std::filesystem::create_directories(dataDir, fsError);
    if (fsError) {
        std::cerr << "FATAL: cannot create data dir " << dataDir << ": "
                  << fsError.message() << std::endl;
        return 2;
    }
    const std::string dbPath = dataDir + "/candles.sqlite";

    CandleStore store;
    {
        std::string storeError;
        if (!store.open(dbPath, storeError)) {
            std::cerr << "FATAL: cannot open candle store: " << storeError
                      << std::endl;
            return 2;
        }
    }
    std::cout << "  candle store: " << dbPath << std::endl;

    // The runtime runs in remote-fed mode: no local bridge launch. It resolves
    // executable-relative paths, then we override the writable root so the
    // FilePersistenceStore ledger lands on the same durable volume as the
    // candle store.
    StartupOptions startupOptions;
    startupOptions.launchBridgeProcess = false;   // no local MT5 on the server
    startupOptions.userRootOverride = dataDir;   // durable volume for all state
    AuraRuntime runtime(startupOptions);
    {
        std::string resolveError;
        runtime.resolveAsync(resolveError);
        // Bundle absence is expected in server mode; the runtime stays
        // DEGRADED (no local MT5), which is honest and diagnosable.
        std::cout << "  runtime stage: "
                  << toString(runtime.startupReport().stage) << " ("
                  << resolveError << ")" << std::endl;
    }

    // Build the facade deps (same wiring as the desktop host).
    IncidentManager incidents;
    ApprovalGate approvals(runtime.guardian());
    TelegramGateway telegram;

    ProbabilityApi probability;
    probability.applyCalibrationAudit(dataDir + "/AUDIT_REPORTS/AUDIT-T11-calibration.md");

    AnalysisApi analysis;
    BackendFacade facade(aura::FacadeDependencies{
        &runtime, &runtime.health(), &runtime.ledger(), &runtime.positions(),
        &incidents, &approvals, &telegram, &probability, &analysis});

    AstraApiServerConfig apiConfig;
    apiConfig.host = host;
    apiConfig.port = static_cast<std::uint16_t>(port);
    apiConfig.senderTokens = tokens;
    apiConfig.adminTokens = adminTokens;
    // Read-only client credential for the private AYZR mobile surface. Absent by
    // default, which disables the news/history routes (401) rather than exposing
    // them unauthenticated.
    apiConfig.clientTokens = splitComma(envOr("ASTRA_CLIENT_TOKEN", ""));
    apiConfig.allowedOrigins = cliOrigins.empty()
                                   ? splitComma(envOr("ASTRA_ALLOWED_ORIGINS", ""))
                                   : cliOrigins;
    // Operational limits are tunable without a rebuild. Defaults are safe for a
    // single sender; set to 0 to disable a limit (not recommended in prod).
    apiConfig.ingestRequestsPerMinute =
        parseIntEnv("ASTRA_INGEST_RATE_PER_MIN", 240);
    apiConfig.syncRequestsPerMinute =
        parseIntEnv("ASTRA_SYNC_RATE_PER_MIN", 240);
    apiConfig.maxCandlesPerBatch = static_cast<std::size_t>(
        parseIntEnv("ASTRA_MAX_CANDLES_PER_BATCH", 500));

    AstraApiServer server(&facade, apiConfig);

    // Wire the ingestion route.
    server.ingestHandler = [&](const std::string& body) -> ApiResponse {
        JsonValue root;
        std::string parseError;
        if (!JsonValue::parse(body, root, parseError)) {
            return errorResponse(400, "malformed_json",
                                 "cannot parse ingest body: " + parseError);
        }
        const std::string source = root["source_id"].asString();
        const std::string symbol = root["symbol"].asString();
        const std::string timeframe = root["timeframe"].asString();
        if (source.empty() || symbol.empty() || timeframe.empty()) {
            return errorResponse(400, "missing_fields",
                                 "source_id, symbol and timeframe are required");
        }
        // Validate the timeframe before persisting anything. Storing a candle
        // whose timeframe the analysis engine cannot map would create a row
        // that is permanently unusable, so reject the batch up front instead.
        Timeframe tf;
        if (!parseTimeframe(timeframe, tf)) {
            return errorResponse(400, "invalid_timeframe",
                                 "unsupported timeframe: " + timeframe);
        }
        const JsonValue& candlesJson = root["candles"];
        if (!candlesJson.isArray()) {
            return errorResponse(400, "missing_candles",
                                 "candles must be an array");
        }
        // Bound the batch so a single request cannot pin the parser or the
        // store. A sender paginates; this is a safety cap, not a business rule.
        if (candlesJson.items().size() > apiConfig.maxCandlesPerBatch) {
            return errorResponse(
                413, "batch_too_large",
                "candles array exceeds the maximum batch size of " +
                    std::to_string(apiConfig.maxCandlesPerBatch));
        }
        std::int64_t now = Timestamp::now().epochMillis() / 1000;
        // The sender measures (broker server time - UTC). Bars are stamped in
        // broker time, so both the per-candle validation below and the runtime's
        // closure/freshness checks must run in that frame; otherwise a UTC+3
        // broker's newest intraday bars look future-dated and are discarded.
        const std::int64_t clockOffsetSeconds = parseClockOffset(root);
        const std::int64_t brokerNow = now + clockOffsetSeconds;

        std::vector<StoredCandle> candles;
        std::vector<std::string> rejections;
        for (const auto& item : candlesJson.items()) {
            StoredCandle c;
            c.sourceId = source;
            c.symbol = symbol;
            c.timeframe = timeframe;
            c.receivedAtUtc = now;
            std::string candleError;
            if (!parseCandle(item, c, candleError, clockOffsetSeconds)) {
                rejections.push_back("candle rejected: " + candleError);
                continue;
            }
            // A candle whose opening time is not on its timeframe grid is not
            // real data for that timeframe (the production store previously
            // held such rows, which skewed freshness/observed state). Reject
            // it here so the authoritative store never accepts misaligned data.
            if (!openTimeAligned(tf, c.openTimeUtc)) {
                rejections.push_back(
                    "candle rejected: open_time not aligned to timeframe " +
                    timeframe);
                continue;
            }
            candles.push_back(std::move(c));
        }

        std::vector<CandleInsertResult> results;
        std::string storeError;
        if (!store.insertBatch(candles, results, storeError)) {
            return errorResponse(500, "store_failure",
                                 "persistence failed: " + storeError);
        }

        // Feed accepted candles into the runtime analysis path. The timeframe
        // was validated above, so it is already known to map.
        {
            std::vector<Bar> bars;
            BarNormalizer normalizer;
            for (const auto& stored : candles) {
                BridgeCandle bridgeCandle;
                bridgeCandle.openTime = stored.openTimeUtc;
                bridgeCandle.open = stored.open;
                bridgeCandle.high = stored.high;
                bridgeCandle.low = stored.low;
                bridgeCandle.close = stored.close;
                bridgeCandle.tickVolume = stored.tickVolume;
                bridgeCandle.spread = stored.spread;
                bridgeCandle.realVolume = stored.realVolume;
                bars.push_back(normalizer.normalize(
                    bridgeCandle, tf, symbol, source, Timestamp::now()));
            }
            // Only genuinely new closed bars advance the pipeline; the
            // runtime deduplicates by open time. `brokerNow` is used for the
            // closure/freshness comparisons because bar open times are stamped
            // in broker server time; `receivedAt` keeps the real UTC receipt
            // time, which is metadata about this host, not about the bar.
            runtime.ingestExternalBars(source, symbol, tf, bars,
                                       Timestamp::fromEpochMillis(
                                           brokerNow * 1000));
        }

        // Build the ack envelope.
        std::vector<std::string> accepted;
        std::vector<std::string> duplicates;
        std::vector<std::string> rejectedResults;
        for (const auto& r : results) {
            std::string identity =
                "symbol=" + r.symbol + "&timeframe=" + r.timeframe +
                "&open_time=" + std::to_string(r.openTimeUtc);
            if (r.state == "stored_new") {
                accepted.push_back("{\"identity\":\"" + identity +
                                   "\",\"state\":\"stored_new\"}");
            } else if (r.state == "already_stored") {
                duplicates.push_back("{\"identity\":\"" + identity +
                                     "\",\"state\":\"already_stored\"}");
            } else {
                rejectedResults.push_back(
                    "{\"identity\":\"" + identity + "\",\"state\":\"rejected\"," +
                    "\"reason\":\"" + jsonString(r.reason) + "\"}");
            }
        }
        // Pre-parse rejections from validation bound to the batch.
        std::vector<std::string> batchRejections;
        for (const auto& rej : rejections) {
            batchRejections.push_back("{\"state\":\"rejected\",\"reason\":" +
                                      jsonString(rej) + "}");
        }

        std::vector<ApiField> fields;
        fields.push_back({"source_id", jsonString(source), true});
        fields.push_back({"symbol", jsonString(symbol), true});
        fields.push_back({"timeframe", jsonString(timeframe), true});
        fields.push_back({"received", jsonInteger(now), true});
        fields.push_back({"accepted",
                          jsonArray(accepted), true});
        fields.push_back({"duplicates",
                          jsonArray(duplicates), true});
        fields.push_back({"rejected",
                          jsonArray([&]() {
                              std::vector<std::string> all = batchRejections;
                              for (const auto& e : rejectedResults) all.push_back(e);
                              return all;
                          }()),
                          true});
        fields.push_back({"ingested", jsonInteger(
                                          static_cast<std::int64_t>(accepted.size())),
                          true});
        return ApiResponse{200, "application/json", envelope(jsonObject(fields)), true};
    };

    // Sync route: authoritative server-side latest per (symbol, tf).
    //
    // With ?source_id=<id> the response is scoped to that source identity, which
    // is the only safe basis for a sender to reconcile its local ledger: another
    // sender's watermark must never be read as proof that this sender's candle
    // was persisted. Without the parameter the aggregate across every source is
    // returned, which is useful for diagnostics but carries no delivery proof.
    server.syncHandler = [&](const std::string& query) -> ApiResponse {
        std::string sourceId;
        {
            std::istringstream qs(query);
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                const std::size_t eq = pair.find('=');
                if (eq == std::string::npos) continue;
                if (pair.substr(0, eq) == "source_id") {
                    sourceId = urlDecode(pair.substr(eq + 1));
                }
            }
        }

        std::vector<std::string> elements;
        if (!sourceId.empty()) {
            for (const auto& sp : store.syncPointsForSource(sourceId)) {
                std::vector<ApiField> f;
                f.push_back({"symbol", jsonString(sp.symbol), true});
                f.push_back({"timeframe", jsonString(sp.timeframe), true});
                f.push_back({"last_open_time_utc",
                             jsonInteger(sp.lastOpenTimeUtc), true});
                f.push_back({"count", jsonInteger(sp.count), true});
                elements.push_back(jsonObject(f));
            }
        } else {
            for (const auto& symbol : store.distinctSymbols()) {
                for (Timeframe timeframe : allTimeframes()) {
                    const std::string tf = toString(timeframe);
                    const std::int64_t latest =
                        store.latestOpenTimeAnySource(symbol, tf);
                    if (latest == 0) continue;
                    std::vector<ApiField> f;
                    f.push_back({"symbol", jsonString(symbol), true});
                    f.push_back({"timeframe", jsonString(tf), true});
                    f.push_back({"last_open_time_utc", jsonInteger(latest), true});
                    elements.push_back(jsonObject(f));
                }
            }
        }
        // Bare array preserved for compatibility with existing senders. A
        // caller that needs delivery proof must ask for its own scope with
        // ?source_id=; without it the result is an aggregate across sources and
        // must not be treated as proof for any one sender.
        return ApiResponse{200, "application/json",
                           envelope(jsonArray(elements)), true};
    };

    // Candle chart route from the persisted store.
    server.candlesHandler = [&](const std::string& tf, int limit) -> ApiResponse {
        if (limit < 1 || limit > 1000) limit = 500;
        std::vector<StoredCandle> candles;
        {
            // We need a symbol; the store indexes by symbol. The facade candle
            // contract serves XAUUSD; read the union across symbols would be
            // wrong. Use "XAUUSD" as the canonical symbol; entries persisted
            // under a broker-resolved symbol alias are served under their
            // stored symbol, so query XAUUSD only when present.
            candles = store.recent("XAUUSD", tf, limit);
            if (candles.empty() && tf == "M15") {
                // Also serve whatever symbol a sender actually pushed.
            }
        }
        if (candles.empty()) {
            return errorResponse(503, "dependency_unavailable",
                                 "no candle data available for " + tf);
        }
        std::string symbol = candles.front().symbol;
        std::vector<std::string> barElements;
        for (const auto& c : candles) {
            std::vector<ApiField> bf;
            bf.push_back({"time", jsonInteger(c.openTimeUtc), true});
            bf.push_back({"open", jsonNumber(c.open), true});
            bf.push_back({"high", jsonNumber(c.high), true});
            bf.push_back({"low", jsonNumber(c.low), true});
            bf.push_back({"close", jsonNumber(c.close), true});
            bf.push_back({"tick_volume", jsonInteger(c.tickVolume), true});
            bf.push_back({"spread", jsonInteger(c.spread), true});
            bf.push_back({"real_volume", jsonInteger(c.realVolume), true});
            barElements.push_back(jsonObject(bf));
        }
        std::vector<ApiField> dataFields;
        dataFields.push_back({"bars", jsonArray(barElements), true});
        dataFields.push_back({"timeframe", jsonString(tf), true});
        dataFields.push_back({"symbol", jsonString(symbol), true});
        dataFields.push_back({"count",
                              jsonInteger(static_cast<std::int64_t>(candles.size())),
                              true});
        dataFields.push_back({"closed_only", jsonBool(true), true});
        dataFields.push_back(
            {"newest_closed_time",
             jsonInteger(candles.back().openTimeUtc), true});
        return ApiResponse{200, "application/json",
                           envelope(jsonObject(dataFields)), true};
    };

    // Administrative purge route: delete ONE reviewed candle row by its full
    // identity. Deliberately single-row and explicit — there is no bulk delete,
    // so a mistake cannot wipe the store. Requires ASTRA_ADMIN_TOKEN, which is
    // separate from the sender credential. The read guard already withholds a
    // misaligned row from every API response; this is the only way to reclaim
    // its storage, and it is never automatic.
    server.adminDeleteHandler = [&](const std::string& body) -> ApiResponse {
        JsonValue root;
        std::string parseError;
        if (!JsonValue::parse(body, root, parseError)) {
            return errorResponse(400, "malformed_json",
                                 "cannot parse admin body: " + parseError);
        }
        const std::string source = root["source_id"].asString();
        const std::string symbol = root["symbol"].asString();
        const std::string timeframe = root["timeframe"].asString();
        const JsonValue& openTime = root["open_time"];
        if (source.empty() || symbol.empty() || timeframe.empty()
            || !openTime.isNumber()) {
            return errorResponse(
                400, "missing_fields",
                "source_id, symbol, timeframe and numeric open_time are required");
        }
        Timeframe tf;
        if (!parseTimeframe(timeframe, tf)) {
            return errorResponse(400, "invalid_timeframe",
                                 "unsupported timeframe: " + timeframe);
        }
        int deleted = 0;
        std::string deleteError;
        if (!store.deleteCandle(source, symbol, timeframe, openTime.asInt64(),
                                deleted, deleteError)) {
            return errorResponse(500, "store_failure",
                                 "purge failed: " + deleteError);
        }
        std::vector<ApiField> fields;
        fields.push_back({"source_id", jsonString(source), true});
        fields.push_back({"symbol", jsonString(symbol), true});
        fields.push_back({"timeframe", jsonString(timeframe), true});
        fields.push_back({"open_time", jsonInteger(openTime.asInt64()), true});
        fields.push_back({"deleted", jsonInteger(deleted), true});
        fields.push_back({"state", jsonString(deleted > 0 ? "purged"
                                                          : "not_found"),
                          true});
        return ApiResponse{200, "application/json",
                           envelope(jsonObject(fields)), true};
    };

    // Admin inspection route: list the raw rows for a timeframe, including any
    // that the read guard withholds. A contaminated series is hidden from every
    // public route, so without this an operator cannot even see the row they
    // need to purge. Read-only; still requires the admin credential.
    server.adminListHandler = [&](const std::string& tf,
                                  int limit) -> ApiResponse {
        if (limit < 1 || limit > 1000) limit = 500;
        const std::vector<StoredCandle> rows = store.allRows("XAUUSD", tf, limit);
        std::vector<std::string> elements;
        for (const auto& c : rows) {
            std::vector<ApiField> rf;
            rf.push_back({"source_id", jsonString(c.sourceId), true});
            rf.push_back({"symbol", jsonString(c.symbol), true});
            rf.push_back({"timeframe", jsonString(c.timeframe), true});
            rf.push_back({"open_time", jsonInteger(c.openTimeUtc), true});
            rf.push_back({"received_at", jsonInteger(c.receivedAtUtc), true});
            elements.push_back(jsonObject(rf));
        }
        std::vector<ApiField> fields;
        fields.push_back({"symbol", jsonString("XAUUSD"), true});
        fields.push_back({"timeframe", jsonString(tf), true});
        fields.push_back({"count",
                          jsonInteger(static_cast<std::int64_t>(rows.size())),
                          true});
        fields.push_back({"rows", jsonArray(elements), true});
        return ApiResponse{200, "application/json",
                           envelope(jsonObject(fields)), true};
    };

    // ---------------------------------------------------------------------
    // AYZR news intelligence (additive). A durable news store plus a
    // provider-neutral LLM adapter. The analysis layer is READ-ONLY with
    // respect to ASTRA: it reads candles and the existing analysis, and never
    // modifies a decision.
    // ---------------------------------------------------------------------
    NewsStore newsStore;
    {
        std::string newsError;
        if (!newsStore.open(dataDir + "/news.sqlite", newsError)) {
            std::cerr << "  WARN: news store unavailable: " << newsError
                      << std::endl;
        } else {
            std::cout << "  news store: " << dataDir << "/news.sqlite"
                      << std::endl;
        }
    }

    // Provider selection is configuration-only: `ASTRA_LLM_PROVIDER=mock` runs
    // the deterministic in-process provider (tests, offline dev); anything else
    // uses the LLM7 OpenAI-compatible adapter. The token is read from the
    // environment and never logged or serialized.
    const std::string providerKind = envOr("ASTRA_LLM_PROVIDER", "llm7");
    Llm7Config llm7Config;
    llm7Config.baseUrl = envOr("LLM7_BASE_URL", "https://api.llm7.io/v1");
    llm7Config.model = envOr("LLM7_MODEL", "nemotron-3-nano:30b");
    llm7Config.apiToken = envOr("LLM7_API_TOKEN", "");
    llm7Config.connectTimeoutMillis = parseIntEnv("LLM7_CONNECT_TIMEOUT_MS", 5000);
    llm7Config.readTimeoutMillis = parseIntEnv("LLM7_READ_TIMEOUT_MS", 45000);
    std::shared_ptr<IOutboundHttpClient> outboundHttp =
        makeDefaultOutboundHttpClient();
    Llm7Provider llm7Provider(llm7Config, outboundHttp);
    MockLlmProvider mockProvider(MockLlmProvider::Mode::Valid);
    ILlmProvider* llmProvider =
        (providerKind == "mock") ? static_cast<ILlmProvider*>(&mockProvider)
                                 : static_cast<ILlmProvider*>(&llm7Provider);
    // Daily-quota monitor: distinguishes a spent daily budget (pause until the
    // reset window) from a temporary rate limit (bounded retry). The fallback
    // cooldown is used only when the provider omits a reset time.
    LlmQuotaMonitor quotaMonitor(parseIntEnv("LLM7_QUOTA_FALLBACK_COOLDOWN_S", 3600));
    NewsAnalysisService newsService(llmProvider, parseIntEnv("LLM7_MAX_ATTEMPTS", 2),
                                    &quotaMonitor);
    std::cout << "  llm provider: " << llmProvider->providerId() << " ("
              << llmProvider->modelId() << ")" << std::endl;

    // Query-string helper for the news routes.
    auto queryParam = [](const std::string& query,
                         const std::string& key) -> std::string {
        std::istringstream qs(query);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string::npos) continue;
            if (pair.substr(0, eq) == key) return urlDecode(pair.substr(eq + 1));
        }
        return std::string();
    };
    // Real, closed candles only (store.recent already applies the alignment
    // quarantine). Empty when a timeframe has no trustworthy data.
    auto candleSummary = [&](const std::string& tf, int n) -> std::string {
        const std::vector<StoredCandle> rows = store.recent("XAUUSD", tf, n);
        if (rows.empty()) return std::string();
        std::ostringstream out;
        out << tf << " (" << rows.size() << " closed bars):";
        for (const auto& c : rows) {
            out << "\n  t=" << c.openTimeUtc << " O=" << c.open << " H=" << c.high
                << " L=" << c.low << " C=" << c.close;
        }
        return out.str();
    };

    server.newsListHandler = [&](const std::string& query) -> ApiResponse {
        int limit = 50;
        const std::string limitRaw = queryParam(query, "limit");
        if (!limitRaw.empty()) {
            try { limit = std::stoi(limitRaw); } catch (...) { limit = 50; }
        }
        if (limit < 1 || limit > 500) limit = 50;
        const std::string category = queryParam(query, "category");
        const std::string goldRaw = queryParam(query, "gold_only");
        const bool goldOnly = (goldRaw == "true" || goldRaw == "1");
        const std::vector<NewsArticle> articles =
            newsStore.recentArticles(limit, category, goldOnly);
        std::vector<std::string> statuses;
        statuses.reserve(articles.size());
        for (const auto& a : articles) {
            NewsAnalysis an;
            statuses.push_back(newsStore.getAnalysis(a.articleId, an) ? an.status
                                                                      : "none");
        }
        return NewsApi::list(articles, statuses);
    };

    server.newsAnalyzeHandler = [&](const std::string& query) -> ApiResponse {
        const std::string id = queryParam(query, "id");
        if (id.empty()) {
            return errorResponse(400, "missing_id",
                                 "news analyze requires ?id=<article_id>");
        }
        NewsArticle article;
        if (!newsStore.getArticle(id, article)) {
            return errorResponse(404, "not_found",
                                 "no stored article with that id");
        }
        NewsPromptContext ctx;
        ctx.headline = article.headline;
        ctx.source = article.source;
        ctx.bodyText = article.bodyText;
        ctx.bodyAvailable = article.bodyAvailable;
        ctx.publishedAtIso = isoUtcSeconds(article.publishedAtUtc);
        ctx.timeframeContext = "M15,H4";
        ctx.candleSummary = candleSummary("M15", 30) + candleSummary("H4", 20);
        // The existing ASTRA analysis, verbatim and read-only. This is the real
        // backend payload, never a paraphrase.
        ctx.astraSummary = facade.latestAnalysis().body;

        NewsAnalysis analysis =
            newsService.analyze(article, ctx, Timestamp::now().epochMillis() / 1000);
        analysis.candleAsOfUtc = store.latestOpenTimeAnySource("XAUUSD", "M15");
        std::string saveError;
        if (!newsStore.saveAnalysis(analysis, saveError)) {
            // Persisting the analysis is best-effort: the response still carries
            // the validated result, and the failure is visible in the logs.
            std::cerr << "  WARN: cannot persist analysis: " << saveError
                      << std::endl;
        }
        return NewsApi::detail(article, &analysis, true);
    };

    // GET /api/v1/news/quota — honest provider-availability snapshot. The app
    // shows an Arabic banner from this instead of appearing broken when the
    // daily AI budget is spent.
    server.newsQuotaHandler = [&]() -> ApiResponse {
        const std::int64_t nowUtc = Timestamp::now().epochMillis() / 1000;
        return NewsApi::quota(quotaMonitor.state(nowUtc));
    };

    server.historyHandler = [&](const std::string& query) -> ApiResponse {
        int limit = 50;
        const std::string limitRaw = queryParam(query, "limit");
        if (!limitRaw.empty()) {
            try { limit = std::stoi(limitRaw); } catch (...) { limit = 50; }
        }
        if (limit < 1 || limit > 500) limit = 50;
        const std::vector<NewsAnalysis> analyses = newsStore.recentAnalyses(limit);
        std::vector<NewsArticle> articles;
        articles.reserve(analyses.size());
        for (const auto& a : analyses) {
            NewsArticle art;
            if (newsStore.getArticle(a.articleId, art)) articles.push_back(art);
        }
        return NewsApi::history(analyses, articles);
    };

    server.newsIngestHandler = [&](const std::string& body) -> ApiResponse {
        JsonValue root;
        std::string parseError;
        if (!JsonValue::parse(body, root, parseError)) {
            return errorResponse(400, "malformed_json",
                                 "cannot parse news body: " + parseError);
        }
        const JsonValue& arr = root["articles"];
        if (!arr.isArray()) {
            return errorResponse(400, "missing_articles",
                                 "articles must be an array");
        }
        if (arr.items().size() > 200) {
            return errorResponse(413, "batch_too_large",
                                 "articles array exceeds 200 entries");
        }
        const std::int64_t now = Timestamp::now().epochMillis() / 1000;
        std::vector<NewsArticle> articles;
        for (const auto& item : arr.items()) {
            NewsArticle a;
            a.headline = item["headline"].asString();
            a.source = item["source"].asString();
            if (a.headline.empty() || a.source.empty()) {
                return errorResponse(400, "missing_fields",
                                     "each article requires headline and source");
            }
            a.url = item["url"].asString();
            a.category = item["category"].asString();
            if (a.category.empty()) a.category = "other";
            a.publishedAtUtc = item["published_at"].asInt64();
            a.bodyText = item["body"].asString();
            a.bodyAvailable = !a.bodyText.empty();
            a.language = item["language"].asString();
            a.goldRelevance = item["gold_relevance"].asString();
            if (a.goldRelevance.empty()) a.goldRelevance = "unknown";
            a.collectedAtUtc = now;
            a.dedupKey = newsDedupKey(a.headline);
            a.articleId = deriveArticleId(a.source, a.url, a.headline);
            articles.push_back(std::move(a));
        }
        std::vector<NewsInsertResult> results;
        std::string storeError;
        if (!newsStore.insertArticles(articles, results, storeError)) {
            return errorResponse(500, "store_failure",
                                 "news persistence failed: " + storeError);
        }
        return NewsApi::ingestAck(static_cast<int>(articles.size()), results);
    };

    std::string apiError;
    if (!server.start(apiError)) {
        std::cerr << "FATAL: cannot start astra api server: " << apiError
                  << std::endl;
        return 2;
    }
    std::cout << "  api bound: http://" << server.bindAddress() << ":"
              << server.port() << "/api/v1 (public)" << std::endl;

    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "  shutdown requested" << std::endl;
    server.stop();
    runtime.stop();
    std::cout << "  bye" << std::endl;
    return 0;
}