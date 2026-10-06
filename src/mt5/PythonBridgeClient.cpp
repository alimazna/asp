// DAT-0003 - Loopback HTTP/JSON implementation of IPythonBridgeClient.

#include "mt5/PythonBridgeClient.h"

#include "foundation/HttpClient.h"
#include "foundation/Json.h"

#include <sstream>

namespace aura {

namespace {

BridgeError parseError(const JsonValue& root) {
    BridgeError error;
    const JsonValue& e = root["error"];
    if (!e.isObject()) return error;
    error.code = e["code"].asString();
    error.state = e["state"].asString();
    error.message = e["message"].asString();
    error.recovery = e["recovery"].asString();
    if (e["context"].isObject()) error.context = e["context"].dump();
    return error;
}

ProtocolVersion parseProtocolVersion(const std::string& text) {
    ProtocolVersion v;
    const std::size_t dot = text.find('.');
    if (dot == std::string::npos) return v;
    try {
        v.major = std::stoi(text.substr(0, dot));
        v.minor = std::stoi(text.substr(dot + 1));
    } catch (...) {
    }
    return v;
}

SchemaVersion parseSchemaVersion(const std::string& text) {
    SchemaVersion v;
    const std::size_t dot = text.find('.');
    if (dot == std::string::npos) return v;
    try {
        v.major = std::stoi(text.substr(0, dot));
        v.minor = std::stoi(text.substr(dot + 1));
    } catch (...) {
    }
    return v;
}

bool checkEnvelopeVersions(const JsonValue& root, const BridgeClientConfig& config,
                           BridgeError& error) {
    const std::string proto = root["protocol_version"].asString();
    const std::string schema = root["schema_version"].asString();
    const ProtocolVersion expectedProtocol = config.protocolVersion;
    const SchemaVersion expectedSchema = config.schemaVersion;

    if (!proto.empty() &&
        !expectedProtocol.isCompatibleWith(parseProtocolVersion(proto))) {
        error.code = "BRIDGE_PROTOCOL_MISMATCH";
        error.message = "bridge protocol " + proto + " incompatible with client " +
                        expectedProtocol.toString();
        error.recovery = "align bridge and client protocol versions";
        return false;
    }
    if (!schema.empty() &&
        !expectedSchema.isCompatibleWith(parseSchemaVersion(schema))) {
        error.code = "BRIDGE_SCHEMA_MISMATCH";
        error.message = "bridge schema " + schema + " incompatible with client " +
                        expectedSchema.toString();
        error.recovery = "align bridge and client schema versions";
        return false;
    }
    return true;
}

std::string versionHeaders(const BridgeClientConfig& config) {
    std::ostringstream headers;
    headers << "X-AURA-Protocol: " << config.protocolVersion.toString() << "\r\n"
            << "X-AURA-Schema: " << config.schemaVersion.toString() << "\r\n"
            << "Accept: application/json\r\n";
    return headers.str();
}

std::string buildPath(const std::string& base, const std::string& query) {
    if (query.empty()) return base;
    return base + "?" + query;
}

class HttpPythonBridgeClient : public IPythonBridgeClient {
public:
    explicit HttpPythonBridgeClient(const BridgeClientConfig& config) : config_(config) {}

    const BridgeClientConfig& config() const noexcept override { return config_; }

    bool isLoopbackOnly() const noexcept override {
        return isLoopbackHost(config_.host);
    }

    BridgeResult<HandshakeInfo> handshake() override {
        JsonValue root;
        BridgeError transportError;
        if (!get("/v1/handshake", "", root, transportError)) {
            return BridgeResult<HandshakeInfo>::failure(transportError);
        }
        if (!checkEnvelopeVersions(root, config_, transportError)) {
            return BridgeResult<HandshakeInfo>::failure(transportError);
        }
        const JsonValue& payload = root["payload"];
        HandshakeInfo info;
        info.protocolVersion = parseProtocolVersion(root["protocol_version"].asString());
        info.schemaVersion = parseSchemaVersion(root["schema_version"].asString());
        info.service = payload["service"].asString();
        for (const auto& tf : payload["timeframes"].items()) {
            info.timeframes.push_back(tf.asString());
        }
        info.primaryOperationalTimeframe = payload["primary_operational_timeframe"].asString();
        info.primaryStructuralTimeframe = payload["primary_structural_timeframe"].asString();
        info.loopbackOnly = payload["loopback_only"].asBool();
        info.startedAtUtc = payload["started_at_utc"].asInt64();
        info.mt5Ready = payload["mt5_ready"].asBool();
        info.resolvedSymbol = payload["resolved_symbol"].asString();
        return BridgeResult<HandshakeInfo>::success(std::move(info));
    }

    BridgeResult<BridgeHealth> health() override {
        JsonValue root;
        BridgeError transportError;
        if (!get("/v1/health", "", root, transportError)) {
            return BridgeResult<BridgeHealth>::failure(transportError);
        }
        const JsonValue& payload = root["payload"];
        BridgeHealth health;
        health.packageAvailable = payload["package_available"].asBool();
        health.initialized = payload["initialized"].asBool();
        health.mt5Ready = payload["mt5_ready"].asBool();
        health.processState = payload["process_state"].asString();
        health.broker = payload["broker"].asString();
        health.server = payload["server"].asString();
        health.resolvedSymbol = payload["resolved_symbol"].asString();
        health.lastError = payload["last_error"].asString();
        const std::int64_t lastOk = payload["last_successful_request"].asInt64(-1);
        health.lastSuccessfulRequest = lastOk >= 0 ? Timestamp::fromEpochMillis(lastOk * 1000)
                                                   : Timestamp::unknown();
        health.generatedAtUtc = payload["generated_at_utc"].asInt64();
        return BridgeResult<BridgeHealth>::success(std::move(health));
    }

    BridgeResult<std::vector<BridgeCandle>> candles(const std::string& symbol,
                                                    Timeframe timeframe, int count,
                                                    bool closedOnly) override {
        std::ostringstream query;
        query << "symbol=" << symbol
              << "&timeframe=" << toString(timeframe)
              << "&count=" << count
              << "&closed_only=" << (closedOnly ? "true" : "false");

        JsonValue root;
        BridgeError transportError;
        if (!get("/v1/candles", query.str(), root, transportError)) {
            return BridgeResult<std::vector<BridgeCandle>>::failure(transportError);
        }
        if (root["status"].asString() == "ERROR") {
            return BridgeResult<std::vector<BridgeCandle>>::failure(parseError(root));
        }
        std::vector<BridgeCandle> candles;
        for (const auto& item : root["payload"]["candles"].items()) {
            BridgeCandle candle;
            candle.openTime = item["time"].asInt64();
            candle.open = item["open"].asDouble();
            candle.high = item["high"].asDouble();
            candle.low = item["low"].asDouble();
            candle.close = item["close"].asDouble();
            candle.tickVolume = item["tick_volume"].asInt64();
            candle.realVolume = item["real_volume"].asInt64();
            candle.spread = item["spread"].asInt64();
            candles.push_back(candle);
        }
        return BridgeResult<std::vector<BridgeCandle>>::success(std::move(candles));
    }

    BridgeResult<BridgeTick> tick(const std::string& symbol) override {
        JsonValue root;
        BridgeError transportError;
        if (!get("/v1/tick", "symbol=" + symbol, root, transportError)) {
            return BridgeResult<BridgeTick>::failure(transportError);
        }
        if (root["status"].asString() == "ERROR") {
            return BridgeResult<BridgeTick>::failure(parseError(root));
        }
        const JsonValue& payload = root["payload"];
        BridgeTick tick;
        tick.time = payload["time"].asInt64();
        tick.bid = payload["bid"].asDouble();
        tick.ask = payload["ask"].asDouble();
        tick.last = payload["last"].asDouble();
        tick.volume = payload["volume"].asDouble();
        tick.flags = payload["flags"].asInt64();
        return BridgeResult<BridgeTick>::success(std::move(tick));
    }

    BridgeResult<SymbolSpecification> symbolSpecification(
        const std::string& symbol) override {
        JsonValue root;
        BridgeError transportError;
        if (!get("/v1/symbol", "symbol=" + symbol, root, transportError)) {
            return BridgeResult<SymbolSpecification>::failure(transportError);
        }
        if (root["status"].asString() == "ERROR") {
            return BridgeResult<SymbolSpecification>::failure(parseError(root));
        }
        const JsonValue& payload = root["payload"];
        SymbolSpecification spec;
        spec.name = payload["name"].asString();
        spec.digits = static_cast<int>(payload["digits"].asInt64());
        spec.point = payload["point"].asDouble();
        spec.tickSize = payload["trade_tick_size"].asDouble();
        spec.tickValue = payload["trade_tick_value"].asDouble();
        spec.contractSize = payload["trade_contract_size"].asDouble();
        spec.volumeMin = payload["volume_min"].asDouble();
        spec.volumeMax = payload["volume_max"].asDouble();
        spec.volumeStep = payload["volume_step"].asDouble();
        spec.stopsLevel = static_cast<int>(payload["trade_stops_level"].asInt64());
        spec.freezeLevel = static_cast<int>(payload["trade_freeze_level"].asInt64());
        return BridgeResult<SymbolSpecification>::success(std::move(spec));
    }

private:
    bool get(const std::string& base, const std::string& query, JsonValue& out,
             BridgeError& error) {
        HttpRequest request;
        request.host = config_.host;
        request.port = config_.port;
        request.path = buildPath(base, query);
        request.headers = versionHeaders(config_);
        request.connectTimeoutMillis = config_.connectTimeoutMillis;
        request.readTimeoutMillis = config_.readTimeoutMillis;

        const HttpResponse response = httpGet(request);
        if (!response.ok) {
            error.code = "BRIDGE_UNAVAILABLE";
            error.message = response.error.empty() ? "bridge unreachable" : response.error;
            error.recovery = "verify the bundled bridge process is running";
            return false;
        }

        std::string parseErrorText;
        if (!JsonValue::parse(response.body, out, parseErrorText)) {
            error.code = "BRIDGE_SCHEMA_MISMATCH";
            error.message = "bridge returned invalid JSON: " + parseErrorText;
            error.recovery = "check bridge/client schema versions";
            return false;
        }
        return true;
    }

    BridgeClientConfig config_;
};

}  // namespace

std::unique_ptr<IPythonBridgeClient> makePythonBridgeClient(
    const BridgeClientConfig& config) {
    return std::make_unique<HttpPythonBridgeClient>(config);
}

}  // namespace aura
