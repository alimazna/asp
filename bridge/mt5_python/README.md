# AURA MT5 Python Bridge

PY-0006 — bundled market-data acquisition boundary for AURA/ASTRA.

## Role

```text
Broker / Valetax
   -> MT5 Terminal (user-installed, logged in)
   -> this bridge (Python + official MetaTrader5 package)
   -> 127.0.0.1 loopback JSON API
   -> AURA C++ PythonBridgeClient -> ingestion -> validation -> decision chain
```

The bridge is a **separate child process** managed by AURA. It is shipped with
the application; end users never run Python or CMD manually.

## Files

| File | Purpose |
|---|---|
| `schemas.py` | Wire vocabulary, versioned envelope, structured error codes |
| `mt5_client.py` | All MetaTrader5 contact; reports unavailability instead of faking data |
| `read_candles.py` | Nine canonical timeframe streams, closed-bar only |
| `bridge_service.py` | Loopback JSON HTTP service AURA starts and supervises |
| `requirements.txt` | Runtime dependencies (MetaTrader5, numpy) |

## Running

Development only (packaged runtime starts this automatically):

```text
python bridge_service.py --host 127.0.0.1 --port 8791 --symbol XAUUSD
```

## Endpoints

```text
GET /v1/health
GET /v1/handshake
GET /v1/symbol
GET /v1/candles?symbol=XAUUSD&timeframe=M15&count=500&closed_only=true
GET /v1/tick?symbol=XAUUSD
```

## Guarantees

- **Loopback only.** `create_server` rejects any host other than `127.0.0.1`.
- **Closed-bar causality.** Candle retrieval skips the forming bar
  (`start_pos=1`); the current bar is never a decision bar.
- **No fabrication.** If MT5 is unavailable the bridge returns a structured
  error (`MT5_TERMINAL_UNAVAILABLE`) and never invents candles.
- **Versioned.** Every response carries `protocol_version` and
  `schema_version`; mismatched client versions are rejected.
- **Explicit symbol resolution.** Exact `XAUUSD`, then `XAUUSD*`, then any
  `XAU*`; the resolved name is recorded and reported.
- **Graceful degradation.** The service imports and handshakes even when the
  Windows-only MetaTrader5 package is absent, so health is always observable.

## Environment

- Windows 10/11 with MT5 Terminal installed, open, and logged in.
- Python 3.8–3.11 (MetaTrader5 does not support 3.12+).
- The packaged application supplies a private runtime; a system Python is not
  a product prerequisite.
