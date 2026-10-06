#pragma once
// DAT-0001 - The data contract shared between AURA and the Python MT5 bridge.
//
// These are the C++ mirror of bridge/mt5_python/schemas.py. Keeping them here
// (rather than inline in the client) means ingestion, validation, and tests all
// speak the same versioned vocabulary.

#include "foundation/DataQualityState.h"
#include "foundation/ProtocolVersion.h"
#include "foundation/SchemaVersion.h"
#include "foundation/Timestamp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// --- Timeframes ------------------------------------------------------------

enum class Timeframe {
    M1,
    M5,
    M15,
    M30,
    H1,
    H4,
    D1,
    W1,
    MN1,
};

inline constexpr std::size_t kTimeframeCount = 9;

inline const std::array<Timeframe, kTimeframeCount>& allTimeframes() {
    static const std::array<Timeframe, kTimeframeCount> kAll = {
        Timeframe::M1, Timeframe::M5, Timeframe::M15, Timeframe::M30,
        Timeframe::H1, Timeframe::H4, Timeframe::D1, Timeframe::W1,
        Timeframe::MN1};
    return kAll;
}

inline const char* toString(Timeframe tf) noexcept {
    switch (tf) {
        case Timeframe::M1:  return "M1";
        case Timeframe::M5:  return "M5";
        case Timeframe::M15: return "M15";
        case Timeframe::M30: return "M30";
        case Timeframe::H1:  return "H1";
        case Timeframe::H4:  return "H4";
        case Timeframe::D1:  return "D1";
        case Timeframe::W1:  return "W1";
        case Timeframe::MN1: return "MN1";
    }
    return "UNKNOWN";
}

inline bool parseTimeframe(const std::string& text, Timeframe& out) noexcept {
    if (text == "M1")  { out = Timeframe::M1;  return true; }
    if (text == "M5")  { out = Timeframe::M5;  return true; }
    if (text == "M15") { out = Timeframe::M15; return true; }
    if (text == "M30") { out = Timeframe::M30; return true; }
    if (text == "H1")  { out = Timeframe::H1;  return true; }
    if (text == "H4")  { out = Timeframe::H4;  return true; }
    if (text == "D1")  { out = Timeframe::D1;  return true; }
    if (text == "W1")  { out = Timeframe::W1;  return true; }
    if (text == "MN1") { out = Timeframe::MN1; return true; }
    return false;
}

// Nominal bar duration in milliseconds. MN1 uses a 30-day approximation; it is
// a context timeframe and never drives execution timing.
inline std::int64_t intervalMillis(Timeframe tf) noexcept {
    switch (tf) {
        case Timeframe::M1:  return 60LL * 1000;
        case Timeframe::M5:  return 5LL * 60 * 1000;
        case Timeframe::M15: return 15LL * 60 * 1000;
        case Timeframe::M30: return 30LL * 60 * 1000;
        case Timeframe::H1:  return 60LL * 60 * 1000;
        case Timeframe::H4:  return 4LL * 60 * 60 * 1000;
        case Timeframe::D1:  return 24LL * 60 * 60 * 1000;
        case Timeframe::W1:  return 7LL * 24 * 60 * 60 * 1000;
        case Timeframe::MN1: return 30LL * 24 * 60 * 60 * 1000;
    }
    return 0;
}

// V4-04 authority.
inline bool isPrimaryOperational(Timeframe tf) noexcept { return tf == Timeframe::M15; }
inline bool isPrimaryStructural(Timeframe tf) noexcept { return tf == Timeframe::H4; }

// --- Candle / tick ---------------------------------------------------------

struct BridgeCandle {
    std::int64_t openTime = 0;    // source bar open time (epoch seconds)
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t tickVolume = 0;
    std::int64_t realVolume = 0;
    std::int64_t spread = 0;
};

struct BridgeTick {
    std::int64_t time = 0;
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    double volume = 0.0;
    std::int64_t flags = 0;
};

struct SymbolSpecification {
    std::string name;
    int digits = 0;
    double point = 0.0;
    double tickSize = 0.0;
    double tickValue = 0.0;
    double contractSize = 0.0;
    double volumeMin = 0.0;
    double volumeMax = 0.0;
    double volumeStep = 0.0;
    int stopsLevel = 0;
    int freezeLevel = 0;
};

// --- Envelope / health -----------------------------------------------------

struct BridgeError {
    std::string code;
    std::string state;
    std::string message;
    std::string recovery;
    std::string context;

    bool empty() const noexcept { return code.empty(); }
};

struct HandshakeInfo {
    ProtocolVersion protocolVersion;
    SchemaVersion schemaVersion;
    std::string service;
    std::vector<std::string> timeframes;
    std::string primaryOperationalTimeframe;
    std::string primaryStructuralTimeframe;
    bool loopbackOnly = false;
    std::int64_t startedAtUtc = 0;
    bool mt5Ready = false;
    std::string resolvedSymbol;
};

struct BridgeHealth {
    bool packageAvailable = false;
    bool initialized = false;
    bool mt5Ready = false;
    std::string processState;
    std::string broker;
    std::string server;
    std::string resolvedSymbol;
    std::string lastError;
    Timestamp lastSuccessfulRequest;
    std::int64_t generatedAtUtc = 0;
};

// Generic bridge result. `ok` is the single source of truth; callers must not
// infer success from an empty payload.
template <typename T>
struct BridgeResult {
    bool ok = false;
    T value{};
    BridgeError error;
    int httpStatus = 0;

    static BridgeResult failure(BridgeError e, int status = 0) {
        BridgeResult r;
        r.ok = false;
        r.error = std::move(e);
        r.httpStatus = status;
        return r;
    }
    static BridgeResult success(T v, int status = 200) {
        BridgeResult r;
        r.ok = true;
        r.value = std::move(v);
        r.httpStatus = status;
        return r;
    }
};

}  // namespace aura
