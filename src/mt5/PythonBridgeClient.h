#pragma once
// DAT-0002 - Client interface for the loopback Python MT5 bridge.
//
// The transport (HTTP over 127.0.0.1) is hidden behind this interface so the
// higher-level data contracts never depend on it. A test double can implement
// this interface without a live bridge.

#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aura {

struct BridgeClientConfig {
    std::string host = "127.0.0.1";   // loopback only
    std::uint16_t port = 8791;
    int connectTimeoutMillis = 2000;
    int readTimeoutMillis = 5000;
    ProtocolVersion protocolVersion;
    SchemaVersion schemaVersion;
};

class IPythonBridgeClient {
public:
    virtual ~IPythonBridgeClient() = default;

    virtual const BridgeClientConfig& config() const noexcept = 0;

    virtual BridgeResult<HandshakeInfo> handshake() = 0;
    virtual BridgeResult<BridgeHealth> health() = 0;
    virtual BridgeResult<std::vector<BridgeCandle>> candles(
        const std::string& symbol, Timeframe timeframe, int count,
        bool closedOnly = true) = 0;
    virtual BridgeResult<BridgeTick> tick(const std::string& symbol) = 0;
    virtual BridgeResult<SymbolSpecification> symbolSpecification(
        const std::string& symbol) = 0;

    // True if the client is bound to a loopback address only.
    virtual bool isLoopbackOnly() const noexcept = 0;
};

// Concrete HTTP/JSON client. `config.host` must be a loopback address.
std::unique_ptr<IPythonBridgeClient> makePythonBridgeClient(
    const BridgeClientConfig& config);

}  // namespace aura
