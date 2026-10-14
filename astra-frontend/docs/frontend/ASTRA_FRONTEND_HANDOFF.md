# ASTRA Frontend Handoff (Alpha)

Authoritative, self-contained handoff for the next (frontend) agent. It is
derived from the **actual backend implementation** in this repository
(`src/api/BackendFacade.cpp`, `src/api/LoopbackApiServer.cpp`,
`src/api/BackendApiSchema.h`) and the frozen state contracts
(`src/foundation/SystemMode.h`, `ServiceState.h`, `DataQualityState.h`,
`src/resilience/FreshnessState.h`, `src/mt5/Mt5BridgeContract.h`).

Every route, field, and value below was observed from a live backend probe.
No route, field, or state is invented. Where the backend does not know a value,
it emits explicit `null` / `UNKNOWN` and the frontend must render it as
unavailable, never as healthy, zero, or safe.

---

## 1. Project identity

- **ASTRA** — desktop product / visual identity (the frontend you build).
- **AURA** — technical/backend identity (this repository).
- **Asset** — `XAUUSD` only. Do not imply any other asset.
- **Mode** — `SHADOW` first. Live trading is **never** authorised.

## 2. Frontend boundary

```text
ASTRA Desktop Frontend
        |
        v
AURA Backend Loopback API   (http://127.0.0.1:8790/api/v1/)
        |
        v
AURA C++ Backend
```

The frontend talks **only** to the backend loopback API. It must never connect
directly to MT5, the broker, the Python `MetaTrader5` package, the Python
bridge, or any broker API. It must never write persistence records directly and
must never bypass backend policy.

## 3. API base

```text
http://127.0.0.1:8790/api/v1/
```

- Transport: loopback-only HTTP/1.1, JSON. One request per connection.
- The server binds `127.0.0.1` only; non-loopback binds are refused.
- The host starts/stops the server; the user never starts it manually.

## 4. Python bridge (internal)

```text
http://127.0.0.1:8791   (AURA Python MT5 bridge)
```

This is an **internal backend component**. The frontend must **NOT** connect to
it directly. Bridge health is surfaced through `GET /api/v1/bridge/status`.

## 5. Startup model

- The packaged application starts the backend and its supervised Python bridge
  itself. Normal use requires **no manual Python/CMD step** and must not depend
  on the current working directory.
- The frontend should assume the API may be briefly unavailable during
  startup and must surface that as `STARTING`/unavailable, not as an error to
  hide.

## 6. Response envelope and errors

Every success body is wrapped:

```json
{ "api": "v1", "schema": "1.0", "data": <payload> }
```

Every non-2xx body is an error object:

```json
{ "error": true, "code": "not_found", "message": "unknown route: /api/v1/nope" }
```

Observed error codes and statuses:

| Status | code | Meaning |
|--------|------|---------|
| 400 | `unknown_timeframe` | snapshot requested for a non-canonical timeframe |
| 400 | `actor_required` | command sent without an actor |
| 403 | `command_not_permitted` | command not on the allow-list (e.g. `live.execute`) |
| 404 | `not_found` | unknown route |
| 405 | `method_not_allowed` | wrong HTTP method for the route |
| 413 | `request_too_large` | request body exceeds the limit |
| 429 | `queue_full` | notification queue full |
| 503 | `dependency_unavailable` | a backend component needed by the route is absent |

## 7. GET routes (all currently exposed)

Read routes are **GET-only**. Any other method returns `405`.

### 7.1 `GET /api/v1/system/state`
Purpose: overall operating mode and readiness.
Fields: `mode` (SystemMode), `shadow_only` (always `true`), `ready` (bool),
`bridge_state` (ServiceState), `startup_stage`, `api`, `schema`.

### 7.2 `GET /api/v1/health`
Purpose: aggregate health and decision-grade data status.
Fields: `aggregate` (ServiceState), `decision_grade_data` (bool),
`degraded_reasons` (string[]), `observed_at` (epoch ms),
`unknown_is_not_safe` (always `true`).

### 7.3 `GET /api/v1/timeframes`
Purpose: every canonical timeframe in canonical order.
Payload: array, one object per timeframe in the order
`M1, M5, M15, M30, H1, H4, D1, W1, MN1` (always all nine, even if unobserved).
Fields per element: `timeframe`, `observed` (bool), `has_closed_bar` (bool),
`quality` (`{state, decision_grade}`), `decision_grade` (bool),
`freshness` (`{state, is_fresh, last_update, age_millis, max_age_millis}` or
`null`), `last_successful_update` (epoch sec/ms or `null`),
`last_closed_bar_open` (epoch sec, observed only), `sequence` (observed only),
`capability_impact` (array of `{capability, impact, reason}`).

### 7.4 `GET /api/v1/timeframes/{tf}/snapshot`
Purpose: latest closed-bar snapshot for one timeframe.
`{tf}` must be one of the nine canonical names. Unknown name → `400`.
Fields: `timeframe`, `observed`, `has_closed_bar`, `quality`, `freshness`,
`last_successful_update`, `sequence`, `open`, `high`, `low`, `close`,
`open_time` (observed only), `capability_impact`.

### 7.5 `GET /api/v1/signals/latest`
Purpose: the most recent decision record (decision display contract).
When no decision exists: `{"available": false, "reason": "..."}`.
When present: `available` (`true`), `decision_id`, `trigger_timeframe`,
`closed_bar_id`, `signal` (LONG/SHORT/NONE), `score` (number),
`score_is_probability` (always `false`), `confidence` (number),
`probability` (always `null` until calibrated), `probability_calibrated`
(always `false`), `reference_price`, `system_mode`, `has_outcome`, plus the
decision context: `symbol`, `trigger_time`, `data_state`, `structure_state`,
`regime_state`, `eligibility_state`, `strategy_version`,
`configuration_version`. Context fields are explicit `null`/`UNKNOWN` when the
live context is not available in this process (never guessed).

### 7.6 `GET /api/v1/risk/latest`
Purpose: portfolio risk and the per-decision risk proposal.
Fields: `available`, `open_positions`, `aggregate_open_risk_fraction`,
`risk_bounded_by_guardian` (always `true`), `proposal_available` (bool),
`proposal` (object or `null`), `proposal_reason` (when no proposal).
Proposal object: `decision_id`, `direction`, `entry_price`, `stop_price`,
`target_price`, `risk_fraction`, `risk_amount`, `position_size_lots`,
`reward_risk_ratio`, `decision`, `reason`, `valid`.

### 7.7 `GET /api/v1/shadow/positions`
Purpose: shadow positions (open and closed).
Payload: array. Fields: `position_id`, `decision_id`, `direction`, `state`
(OPEN / CLOSED_TARGET / CLOSED_STOP / CLOSED_MANUAL / CLOSED_EXPIRED),
`entry_price`, `stop_price`, `target_price`, `lots`, `risk_fraction`,
`timeframe`, `opened_at`, `opened_bar_open`, `exit_price`, `realized_pnl`,
`realized_r`, `close_reason`, `closed_at` (`null` while open),
`closed_bar_open` (`null` while open), `shadow_only` (always `true`).

### 7.8 `GET /api/v1/shadow/outcomes`
Purpose: recorded shadow outcomes.
Payload: array. Fields: `outcome_id`, `position_id`, `decision_id`,
`direction`, `timeframe`, `exit_state`, `entry_price`, `exit_price`, `lots`,
`realized_pnl`, `realized_r`, `risk_fraction`, `bars_held`, `outcome_class`
(WIN / LOSS / BREAKEVEN / UNKNOWN), `recorded_at`, `note`,
`shadow_only` (always `true`).

### 7.9 `GET /api/v1/research/status`
Purpose: research experiments and failure memory.
Fields: `available`, `mode` (`SHADOW`), `note` (research never grants
execution authority), `experiment_count`, `experiments` (array),
`failure_count`, `failures` (array). Experiment: `experiment_id`,
`hypothesis_id`, `method`, `outcome`, `sample_size`, `result_metric`,
`started_at`. Failure: `failure_id`, `category`, `summary`, `occurrences`,
`resolved`, `last_seen`.

### 7.10 `GET /api/v1/governance/status`
Purpose: approval queue and full request history.
Fields: `pending_count`, `pending` (array), `history` (array),
`live_trading_authorised` (always `false`). Pending: `request_id`, `kind`,
`subject_id`, `requested_at`. History: `request_id`, `kind`, `status`,
`requested_by`, `requested_at`, `decided_at` (`null` until decided).

### 7.11 `GET /api/v1/bridge/status`
Purpose: Python bridge and MT5/broker readiness (the only MT5 view the
frontend gets).
Fields: `bridge_process_state` (ServiceState), `startup_stage`, `handshake_ok`,
`mt5_ready`, `resolved_symbol`, `transport` (`http_loopback`), `host`
(`127.0.0.1`), `loopback_only` (always `true`), `managed_by_application`
(always `true`), `requires_manual_cmd` (always `false`), `package_available`,
`initialized`, `mt5_ready_live`, `broker`, `server`, `bridge_symbol`,
`process_state`, `last_error`, `last_successful_request`, `observed`.
When the bridge is unreachable, identity fields are explicit `null` and
`process_state` is `OFFLINE` — never a fabricated broker.

### 7.12 `GET /api/v1/audit/recent`
Purpose: append-only, hash-chained audit stream plus active incidents.
Fields: `audit_stream_size`, `audit_records` (newest first, bounded to 200),
`active_incidents`, `count`. Audit record: `sequence`, `event_id`, `action`,
`outcome`, `service_state`, `occurred_at`, `actor`, `subject`, `details`,
`previous_hash`, `record_hash`. Incident: `incident_id`, `severity`, `state`,
`title`.

## 8. Command API

### `POST /api/v1/command`

Body (flat JSON object):

```json
{ "command": "<name>", "actor": "<who>", "payload": "<free text>" }
```

- **Allow-list only.** The backend supports exactly two commands:
  - `notify` → enqueues a Telegram notification.
    Success `202`: `{"accepted": true, "message_id": "tg-1"}`.
  - `request_approval` → creates a policy-change approval request.
    Success `202`: `{"accepted": true, "request_id": "approval-1", "status": "PENDING"}`.
- **Authorization.** Every command must carry a non-empty `actor` (attributable);
  missing actor → `400 actor_required`. Commands not on the allow-list →
  `403 command_not_permitted`.
- **Live execution is prohibited.** `live.execute` and every execution-like
  command are rejected by construction. There is no command that can enable
  live trading or bypass policy. Do not attempt to add one.

## 9. Data and state semantics

Service state (`ServiceState`):
`STARTING, ONLINE, DEGRADED, OFFLINE, RECOVERING, PAUSED, BLOCKED, ERROR`.

System mode (`SystemMode`):
`STARTING, RECOVERY, NORMAL, DEGRADED, SHADOW, PAUSED, MANUAL, EMERGENCY, HALTED`.

Data quality (`DataQualityState`):
`VALID, DEGRADED, INVALID, UNKNOWN, STALE, MISSING, OUT_OF_ORDER, DUPLICATE, INCOMPLETE`.
Only `VALID` is decision-grade.

Freshness (`FreshnessState`): `FRESH, STALE, UNKNOWN`.

Rules the frontend must honour:

- **UNKNOWN is not healthy.** Render it as "unknown / not available".
- **STALE is not FRESH.** A stale value is not a current value.
- **MISSING must never be shown as 0 or as safe.**
- `DEGRADED`, `OFFLINE`, `BLOCKED`, `RECOVERING` are distinct and must be
  labelled distinctly. `RECOVERING` means the subsystem is coming back, not
  that it is healthy.
- `SHADOW` means the system is running in simulation-only mode. It never means
  live trading.

## 10. Timeframes

Canonical order: `M1, M5, M15, M30, H1, H4, D1, W1, MN1`.

- **M15** = primary operational/setup timeframe.
- **H4** = primary structural authority.
- M5/M1 = execution/microstructure context.

The API always returns all nine in this order. Never re-sort or drop
unobserved entries; an unobserved timeframe is `observed: false` with
`quality.state = UNKNOWN`.

## 11. Signal semantics

A decision exposes `signal` (LONG/SHORT/NONE), `trigger_timeframe`, `score`,
`confidence`, and `probability`.

- **SCORE IS NOT PROBABILITY.** `score_is_probability` is always `false`.
  The frontend must never convert score into a probability.
- **Probability is N/A until calibrated.** `probability` is `null` and
  `probability_calibrated` is `false`. Display "N/A (uncalibrated)".

## 12. Risk

All risk values are backend-owned. The frontend displays them; it never
computes position sizing, stops, targets, or risk fractions. Risk is reported
as bounded by the Guardian (`risk_bounded_by_guardian: true`).

## 13. Shadow lifecycle

Shadow positions move OPEN → one of the closed states, producing an outcome.
The frontend can show entry/stop/target, realized P&L and R, close reason, and
open/close bar timestamps. Everything is `shadow_only`. There is no live
order anywhere in this data.

## 14. Error model (frontend behaviour)

- **Connection refused / server not listening** → backend not up yet; show
  `OFFLINE`/`STARTING`, allow retry. Do not fabricate data.
- **Timeout** → treat as unavailable; do not cache a stale value as current.
- **Invalid / unparseable response** → treat as an error, not as empty data.
- **`503 dependency_unavailable`** → that capability is off; disable the
  dependent UI only, keep the rest working.
- **`403 command_not_permitted`** → the action is not allowed; surface it
  plainly. Never retry as a different command to "make it work".
- **Degraded backend state** → show the specific degraded reason(s) from
  `/health` and the capability impact from `/timeframes`.

## 15. Example calls

```bash
# Overall state
curl http://127.0.0.1:8790/api/v1/system/state
# {"api":"v1","schema":"1.0","data":{"mode":"EMERGENCY","shadow_only":true,
#  "ready":false,"bridge_state":"ERROR","startup_stage":"FAILED","api":"v1","schema":"1.0"}}

# Timeframes (canonical order, all nine)
curl http://127.0.0.1:8790/api/v1/timeframes

# One snapshot
curl http://127.0.0.1:8790/api/v1/timeframes/M15/snapshot
# {"api":"v1","schema":"1.0","data":{"timeframe":"M15","has_closed_bar":false,
#  "quality":"UNKNOWN","observed":false,"freshness":null,
#  "last_successful_update":null,
#  "capability_impact":[{"capability":"primary_operational_decisions",
#  "impact":"BLOCKED","reason":"M15 not decision-grade (UNKNOWN)"}]}}

# Bridge / MT5 readiness
curl http://127.0.0.1:8790/api/v1/bridge/status

# Permitted command
curl -X POST -H 'Content-Type: application/json' \
  -d '{"command":"notify","actor":"operator","payload":"hello"}' \
  http://127.0.0.1:8790/api/v1/command
# {"api":"v1","schema":"1.0","data":{"accepted":true,"message_id":"tg-1"}}

# Prohibited command (always rejected)
curl -X POST -d '{"command":"live.execute","actor":"operator"}' \
  http://127.0.0.1:8790/api/v1/command
# {"error":true,"code":"command_not_permitted",
#  "message":"command is not on the allow-list: live.execute"}
```

## 16. Files Alpha should read

Backend/frontend boundary and contract:

- `docs/frontend/ASTRA_FRONTEND_HANDOFF.md` (this file)
- `project-control/FRONTEND_INTEGRATION_MAP.md`
- `project-control/HANDOFF.md`
- `docs/architecture/BACKEND_FRONTEND_API_V1.md`
- `docs/brand/ASTRA_VISUAL_IDENTITY.md`

Backend implementation (inspect, do not modify without cause):

- `src/api/LoopbackApiServer.h`, `src/api/LoopbackApiServer.cpp`
- `src/api/BackendFacade.h`, `src/api/BackendFacade.cpp`
- `src/api/BackendApiSchema.h`
- `src/api/RuntimeManifest.json`
- `src/foundation/SystemMode.h`, `src/foundation/ServiceState.h`,
  `src/foundation/DataQualityState.h`
- `src/resilience/FreshnessState.h`, `src/resilience/TimeframeCapabilityImpact.h`
- `src/mt5/Mt5BridgeContract.h`

## 17. Visual constraints (from `ASTRA_VISUAL_IDENTITY.md`)

Deep navy / near-black dark surfaces; cool light-gray / off-white light
surfaces; silver/white geometric mark on dark; restrained grayscale accents;
large clean typography; generous spacing; institutional/aerospace/precision
mood; minimal decorative noise. The UI should read as PRECISION, CALM,
INSTITUTIONAL, TECHNICAL, PREMIUM, AUDITABLE. Derive final design tokens from
the supplied brand asset and keep them centralized.
