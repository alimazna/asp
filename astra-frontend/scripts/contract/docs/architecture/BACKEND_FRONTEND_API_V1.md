# BACKEND_FRONTEND_API_V1

> **Status: FROZEN (T17) — API v1, schema 1.0, tag `api-v1.0`.** Frozen on
> 2026-10-07 after T16 landed. The machine-readable contract is
> `docs/architecture/API_V1_SCHEMA.json`; it is authoritative where this prose
> and the schema disagree. The decision-support surface is documented for the
> frontend in `docs/frontend/FRONTEND_HANDOFF_GUIDE.md` (T18).
>
> **Additive changes only within v1.** A new field or a new route may be added;
> no existing field may be removed, renamed, retyped, or made required. Breaking
> changes require `v2`, and `v1` keeps serving until clients migrate.

## Purpose
Provide a stable contract for Alpha to build the ASTRA desktop application without depending on backend internals.

## Contract categories
1. Runtime state
2. Data health
3. Timeframe snapshots
4. Decision records
5. Risk proposals
6. Shadow positions/outcomes
7. Research status
8. Governance/approval state
9. Audit/incident state

## Minimum frontend-safe endpoints/contracts
The implementation may expose these through a local application API, in-process façade, or approved transport. The transport itself is a backend concern.

```text
GET  /api/v1/system/state
GET  /api/v1/health
GET  /api/v1/health/v1
GET  /api/v1/timeframes
GET  /api/v1/timeframes/{tf}/snapshot
GET  /api/v1/candles?tf=M15&limit=500
GET  /api/v1/signals/latest
GET  /api/v1/probability/latest
GET  /api/v1/risk/latest
GET  /api/v1/shadow/positions
GET  /api/v1/shadow/outcomes
GET  /api/v1/research/status
GET  /api/v1/governance/status
GET  /api/v1/audit/recent
GET  /api/v1/bridge/status
GET  /api/v1/analysis/latest
GET  /api/v1/analysis/history?limit=N
GET  /api/v1/context/latest
POST /api/v1/command
```

## Decision-support surface (frozen in v1)

`/analysis/latest`, `/analysis/history`, `/context/latest`, and `/health/v1` are
the frozen decision-support surface for ASTRA. Field-level semantics are in
`docs/frontend/FRONTEND_HANDOFF_GUIDE.md`; the schema is authoritative.

Honesty contract for this surface (binding):

- **RULE C.** `signal.probability` is non-null **only** when the value is
  calibrated *and* the calibration has been audited. Otherwise it is `null`,
  `signal.probability_calibrated` is `false`, and `meta.score_is_probability` is
  `false`. A value is never shown as a probability unless it passes the gate.
- **Unavailable is not zero.** A field the backend cannot source is `null` /
  `UNKNOWN`. In this release `signal.horizon`, `signal.confidence_lo/hi`,
  `signal.model_version`, `levels.sl_method`, `levels.tp_method`,
  `meta.data_freshness_sec`, and `context.mtf_agreement` are `null` until the
  decision model (T15) and calibration (T11) freeze them. They will be populated
  additively; their absence is not an error.
- **Levels are suggestions.** `levels.*` come from the live risk proposal and are
  never recomputed by the API. They are not orders, and the backend never
  executes.
- **History.** `/analysis/history` returns most-recent-first, `limit` default 50,
  max 500. Per-entry context and levels are not persisted and are reported as
  `null`/`UNKNOWN` in history entries.

## Candle series (additive in v1)

`GET /api/v1/candles?tf={tf}&limit={N}` returns the candle series the ASTRA
chart plots. `tf` is one of the nine canonical timeframes (required); `limit`
is `1..1000` (default `500`). The route proxies the Python bridge
(`127.0.0.1:8791`), which is the only component that talks to MT5, and wraps
the bridge series in the standard envelope:

```json
{
  "api": "v1",
  "schema": "1.0",
  "data": {
    "bars": [
      {"time": 1760001900, "open": 2650.75, "high": 2651.90,
       "low": 2650.10, "close": 2651.60,
       "tick_volume": 967, "spread": 21, "real_volume": 0}
    ],
    "timeframe": "M15",
    "symbol": "XAUUSD",
    "count": 1,
    "closed_only": true,
    "newest_closed_time": 1760001900,
    "freshness": "FRESH"
  }
}
```

Semantics:

- **Closed bars only.** The forming bar is never returned, so the chart never
  plots a bar the decision chain has not accepted.
- **Validation is layered.** An unknown `tf` → `400 unknown_timeframe`; a
  missing `tf` → `400 missing_timeframe`; an out-of-range or non-integer
  `limit` → `400 invalid_limit`. Both the backend and the bridge validate.
- **Dependency outage.** An unreachable bridge → `503 dependency_unavailable`
  (`"python bridge not reachable"`); an unavailable terminal/symbol is
  propagated as a `503` with the bridge's code. A `503` is never fabricated
  data.
- **No backend caching.** The bridge caches successful reads for 5s per
  `(symbol, tf, limit)`; the backend proxies each request.

`API_V1_SCHEMA.json` carries the machine-readable shape
(`tests/fixtures/api_v1/valid/candles.json` is the conformant fixture). This is
an additive v1 change: no existing route, field, or behaviour is altered.

## Error schema

Errors are **flat** (not enveloped) and always carry `error`, `code`, `message`:

```json
{"error": "true", "code": "not_found", "message": "unknown route: /api/v1/nope"}
```

Status codes: `404 not_found` (unknown route), `405 method_not_allowed` (write
method on a read route), `400` malformed request, `413 request_too_large`,
`503 dependency_unavailable` (a required component is absent). A `503` is never
fabricated data; it is an explicit "unavailable".

The concrete transport is a loopback-only HTTP/JSON server
(`aura::LoopbackApiServer`) on `127.0.0.1:8790`; the Python bridge is a separate
internal component on `127.0.0.1:8791` that the frontend never contacts.

Human control actions use explicit versioned command contracts and backend-side
authorization/policy checks. The command surface is an allow-list: only
`notify` and `request_approval` are accepted; every execution-like command
(including `live.execute`) is rejected with `403 command_not_permitted`. Every
command must carry an `actor`. Live execution remains prohibited.

## State model
Frontend must distinguish service states from system operating modes.

Service:
`STARTING, ONLINE, DEGRADED, OFFLINE, RECOVERING, PAUSED, BLOCKED, ERROR`

System:
`STARTING, RECOVERY, NORMAL, DEGRADED, SHADOW, PAUSED, MANUAL, EMERGENCY, HALTED`

## Timeframe display contract
Frontend should be able to show:
- M1/M5/M15/M30/H1/H4/D1/W1/MN1;
- freshness/quality;
- last successful update;
- exact failed timeframe;
- capability impact.

## Decision display contract
A decision should expose:
```text
decision_id
symbol
trigger_time
trigger_timeframe
closed_bar_id
data_state
structure_state
regime_state
eligibility_state
signal
score
confidence
probability (N/A until calibrated)
risk_proposal
system_mode
strategy_version
configuration_version
```

## Frontend invariants
- never fabricate data;
- never infer a missing value as zero/safe;
- never call the Python bridge directly;
- never write persistence records directly;
- never bypass backend policy;
- never place live orders.

## Freeze record (T17)

| item | value |
|---|---|
| API version | `v1` |
| schema version | `1.0` |
| tag | `api-v1.0` |
| machine-readable contract | `docs/architecture/API_V1_SCHEMA.json` |
| served by | `aura::BackendFacade` (routes) over `aura::LoopbackApiServer` |
| offline mock | `scripts/mock_api.py` (validates against the schema) |
| contract test | `tests/integration/test_mock_api_t19.py` |

**Change process.** Within `v1`, only additive changes are permitted: a new
route, or a new optional field. Removing, renaming, retyping, or newly requiring
an existing field is breaking and requires `v2`. Every additive change must (a)
update `API_V1_SCHEMA.json`, (b) keep `scripts/mock_api.py --check` green, and
(c) be noted here. The schema file is authoritative; if this prose and the schema
disagree, the schema wins.
