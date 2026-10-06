# MT5 Python Bridge V1 — AURA Backend Contract

## Purpose
Replace the old MQL5/EA candle-ingestion boundary with a Python MetaTrader5 bridge that is packaged with the application and managed as a child process.

## Runtime topology

```text
Broker
  -> MT5 Terminal (Windows)
  -> Python + MetaTrader5
  -> 127.0.0.1 loopback API
  -> AURA C++ PythonBridgeClient
```

## Process rules
- Bridge process is started by AURA.
- Bridge binds to loopback only.
- Bridge is never exposed to LAN/WAN.
- Bridge reports protocol/version/capabilities during startup handshake.
- Bridge must fail explicitly when MT5 is unavailable.
- Bridge must not fabricate candles.

## Broker/terminal assumptions
MT5 Terminal is a user-installed external dependency and must be available and appropriately connected. The product should report its state rather than hiding the dependency.

## Canonical data
### Candle request
The C++ client can request:
- symbol;
- timeframe;
- count;
- `closed_only=true`.

### Candle fields
Minimum fields:
```text
bar_id (or deterministic bar identity derivable from symbol/timeframe/open_time)
symbol
timeframe
open_time
close_time where derivable
open
high
low
close
tick_volume
real_volume when available
spread where available
source
broker_id / broker name
receive_time / ingestion_time where applicable
quality_state
```

### Tick/quote fields
Where available:
```text
source timestamp
receive timestamp
sequence if available
bid
ask
last
volume
flags
quality state
```

### Symbol specification
Where available:
```text
broker
server
symbol
digits
point
tick_size
tick_value
contract_size
volume min/max/step
stops level
freeze level
trade mode
execution/filling mode
margin mode
sessions
swap rules
```

## Closed-bar rule
Decision-grade candle retrieval must not use the currently forming bar. The reference implementation uses the equivalent of `start_pos=1` semantics from the supplied Python guide.

## Suggested local API surface
The implementation contract is intentionally small:

```text
GET /v1/health
GET /v1/handshake
GET /v1/symbol
GET /v1/candles?symbol=XAUUSD&timeframe=M15&count=500&closed_only=true
GET /v1/tick?symbol=XAUUSD
```

Transport is HTTP-style JSON over `127.0.0.1`. The port is configurable.

The C++ abstraction must hide transport details behind a stable client interface.

## Response envelope
A response should be versioned and explicit:

```json
{
  "protocol_version": "1",
  "schema_version": "1",
  "request_id": "...",
  "status": "OK",
  "source": "mt5-python-bridge",
  "generated_at_utc": 0,
  "broker": "...",
  "symbol": "XAUUSD",
  "timeframe": "M15",
  "quality": "VALID",
  "payload": {}
}
```

Errors must include structured code/state/message/context/recovery information.

## Symbol resolution
The supplied Python guide resolves the broker symbol by preferring exact `XAUUSD`, then names beginning with `XAUUSD`, then a fallback XAU symbol. The production bridge should retain explicit resolution behavior and record the resolved symbol instead of pretending every broker uses the bare `XAUUSD` name.

## Health
The bridge should report at minimum:
```text
process state
MT5 initialize state
terminal version
broker/server identity
resolved symbol
data freshness
last successful request
last error
```

## Recovery
AURA may restart the bridge only under bounded policy. Repeated failures must transition the service to DEGRADED/OFFLINE and block capabilities that require fresh market data.

## Security boundary
- bind only 127.0.0.1;
- do not accept arbitrary remote addresses;
- validate protocol/schema versions;
- reject malformed/future-dated/duplicate/unknown-source messages as appropriate;
- do not log credentials/secrets.

## Packaging
Preferred packaged layout:

```text
ASTRA/
  ASTRA.exe
  backend/
    AURABackendHost.exe (or equivalent backend host)
  bridge/
    python-runtime/
    mt5_python/
      bridge_service.py
      read_candles.py
      mt5_client.py
      schemas.py
      requirements.txt
  config/
  data/
  logs/
```

Exact filenames may be refined during implementation only through the task manifest and integration review.
