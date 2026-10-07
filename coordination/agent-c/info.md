# Agent-C - Important Info
> Living document. Updated, not appended.

## Role
Backend & Live Integration. Owns the MT5 bridge, Python bundling, Windows packaging, and the probability API. Keeps production protected and live trading disabled.

## Owned files
- src/api/
- bridge/
- packaging/
- coordination/agent-c/

## Forbidden files
- The baseline (base9 / baseold) - READ-ONLY.
- src/foundation/ (frozen contracts), src/models/, src/analysis/features/.
- coordination/<other-agent>/ folders.

## Features / deliverables
- T06 MT5 bridge hardening (IN PROGRESS -> REVIEW): loopback/9-timeframe/closed-bar/health/
  handshake/SIGTERM all verified; added staleness (MARKET_DATA_STALE), INSUFFICIENT_HISTORY,
  and bootstrap-error surfacing. Integration test: tests/integration/test_bridge_t06.py.

## Key findings
- Bridge verified empirically on 127.0.0.1:8791 (no real MT5/broker here).
- QUALITY_STALE existed but was never used before T06; now wired into /v1/candles.
- Bridge error codes now align with C++ ErrorCode.h: MARKET_DATA_STALE matches an existing
  enum member; INSUFFICIENT_HISTORY is a new bridge-only code (maps to UNKNOWN_ERROR if
  parsed by the C++ enum; the C++ client passes error.code through as a free string).
- MT5 absence path: handshake still OK, health quality UNKNOWN, candles ->
  MT5_TERMINAL_UNAVAILABLE (now surfaced from bootstrap state).

## Open questions
- Should the C++ ErrorCode.h gain an INSUFFICIENT_HISTORY member? It is a frozen contract
  (src/foundation/, not my zone) — raised to Lead/Agent-D, not changed by me.
- Real MT5 candle retrieval still unexercised (no broker terminal in container).

## Useful commands
(empty)
