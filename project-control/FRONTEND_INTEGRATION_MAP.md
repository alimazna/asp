# ASTRA Frontend Integration Map

Authoritative map for connecting the ASTRA desktop frontend to the completed
AURA backend. Generated from the **actual repository implementation** and the
current canonical control-plane documents. Where documentation and
implementation disagree, the discrepancy is recorded in
[§25 Discrepancies](#25-discrepancies-found-documentation-vs-implementation)
and the implemented behaviour is documented.

Status: authoritative. The nine discrepancies (D1–D9) it originally recorded
were subsequently resolved in the backend; this document reflects the
implemented behaviour. No frontend source exists or was modified.

- Backend API version: `v1`
- API schema version: `1.0`
- Product: ASTRA (desktop identity) on project AURA (backend identity)
- Asset: `XAUUSD` only
- Mode: `SHADOW` only

---

## 1. Frontend role

AURA is the **backend/system identity**: the C++ runtime, data ingestion,
validation, the deterministic decision chain, risk, shadow execution,
persistence, research, governance, and the Python MT5 bridge.

ASTRA is the **desktop product / frontend identity**: a human control and
visualization layer over the AURA backend.

Alpha (the frontend agent) owns:

- the desktop shell, navigation, and visual design;
- presentation of backend state, health, market data, signals, risk, shadow
  lifecycle, outcomes, audit/research status;
- collection and submission of the small allow-listed command set
  (`notify`, `request_approval`) through the versioned command contract;
- clear rendering of degraded/unknown/stale states.

The frontend is **not the trading brain**. It must not compute signals, score,
confidence, probability, risk, position sizing, or shadow fills. Those are
backend-owned and already implemented.

---

## 2. Backend connection model

Actual implemented runtime relationship:

```text
ASTRA Frontend (Alpha)
    ->  LoopbackApiServer (HTTP/JSON, 127.0.0.1:8790 only)
    ->  BackendFacade (v1 API surface, in-process C++ object)
    ->  AURA C++ Runtime (AuraRuntime / DecisionPipeline)
    ->  PythonBridgeClient (HTTP/JSON, 127.0.0.1:8791 only)
    ->  Python MT5 Bridge (bridge/mt5_python/bridge_service.py)
    ->  MT5 Terminal (Windows, user-installed)
    ->  Broker
```

Implemented facts:

- The frontend-facing contract is `aura::BackendFacade`
  (`src/api/BackendFacade.h`). It exposes typed methods and a
  `handle(method, path)` route dispatcher.
- The facade is constructed and held by the backend host
  (`src/platform/windows/AuraBackendHost.cpp`), which owns the runtime.
- The Python bridge is a **separate child process** started by the backend; it
  binds `127.0.0.1` only.
- The bridge transport (HTTP JSON on port `8791`) is used by the **backend**
  (`PythonBridgeClient`), not by the frontend.
- The frontend transport is `aura::LoopbackApiServer`
  (`src/api/LoopbackApiServer.h/.cpp`), a loopback-only HTTP/JSON server the
  host starts over the facade on port `8790`. Alpha polls
  `http://127.0.0.1:8790/api/v1/*` and never touches the bridge.

> **D1 — transport boundary (RESOLVED).** The facade remains an in-process C++
> API, and it is now published to the frontend by a loopback-only HTTP/JSON
> server, `aura::LoopbackApiServer` (`src/api/LoopbackApiServer.h/.cpp`,
> PKG-0007/0008). It binds `127.0.0.1` only (a non-loopback host is refused),
> defaults to port `8790` (the Python bridge uses `8791`), and is started and
> stopped by the backend host (`src/platform/windows/AuraBackendHost.cpp`).
> Routes are exactly the facade contract; the server owns no state and reaches
> no runtime internals. `POST /api/v1/command` carries the same allow-listed,
> actor-attributed command contract as the in-process facade.

---

## 3. Startup model

Implemented startup sequence (`src/platform/windows/StartupCoordinator.cpp`,
`StartupStage` in `StartupCoordinator.h`):

```text
NOT_STARTED
  -> RESOLVING_PATHS      (executable-root path resolution; CWD-independent)
  -> LOCATING_BUNDLE      (find bundled Python + bridge script)
  -> LAUNCHING_BRIDGE     (supervised child process)
  -> WAITING_FOR_HANDSHAKE(loopback handshake + health poll)
  -> READY                (handshake OK)
     or DEGRADED          (bundle/bridge unavailable; backend still runs safely)
     or FAILED            (path resolution or bridge launch failed)
```

- **Executable relationship:** the backend host (`aura_backend_host`) is the
  double-click entry point. It resolves paths from its own executable location,
  never from the working directory.
- **Bridge lifecycle:** the backend launches and supervises the Python bridge
  as a child process (`ProcessSupervisor`); it never asks the user to run
  Python or a CMD script.
- **Readiness:** `StartupReport.ready` is true only on `READY`. The runtime
  `mode()` is `SHADOW` when the operational timeframe is fresh, otherwise
  `DEGRADED`.

What the frontend should display while starting:

- a STARTING state driven by `GET /api/v1/system/state` → `mode` = `STARTING`;
- `bridge_state` (a `ServiceState`) transitioning `OFFLINE → STARTING →
  ONLINE`;
- no market data, no signal, no risk values until data is decision-grade.

When MT5 is unavailable (verified in this environment):

- the bridge still starts and answers; the backend reports explicit errors;
- `mode` becomes `DEGRADED`;
- `/api/v1/timeframes` is empty or non-decision-grade, and timeframe snapshots
  report `quality`/`UNKNOWN`;
- the frontend must show this as **not ready**, never as an empty-but-fine
  dashboard.

The frontend must **never** require the user to run Python, activate a
virtualenv, or open a terminal.

---

## 4. API inventory

Every route below exists in `BackendFacade::handle` (`src/api/BackendFacade.cpp`)
and is covered by `tests/BackendApiContractTests.cpp`.

Standard envelope for success (HTTP 2xx):

```json
{"api":"v1","schema":"1.0","data": <payload>}
```

Standard error object (HTTP 4xx/5xx):

```json
{"error":true,"code":"...","message":"..."}
```

Read routes are **GET only**; any other method returns `405`. All routes are
served over loopback HTTP at `http://127.0.0.1:8790/api/v1/*` (D1 resolved).

| # | Method | Path | Purpose | Request params | Response `data` type | Important fields | Success | Error | Refresh | Availability requirement |
|---|--------|------|---------|----------------|----------------------|-------------------|---------|-------|---------|--------------------------|
| 1 | GET | `/api/v1/system/state` | Operating mode + readiness | none | object | `mode`, `shadow_only`, `ready`, `bridge_state`, `startup_stage`, `api`, `schema` | 200 | 503 `dependency_unavailable` | Poll; state changes on tick | `runtime` dependency |
| 2 | GET | `/api/v1/health` | Aggregate health | none | object | `aggregate`, `decision_grade_data`, `degraded_reasons[]`, `observed_at`, `unknown_is_not_safe` | 200 | 503 `dependency_unavailable` | Poll | `health` monitor |
| 3 | GET | `/api/v1/timeframes` | Per-stream quality summary | none | array of objects (canonical M1..MN1 order) | `timeframe`, `observed`, `has_closed_bar`, `quality` (`{state,decision_grade}`), `decision_grade`, `freshness` (`{state,is_fresh,last_update,age_millis,max_age_millis}`), `last_successful_update`, `last_closed_bar_open`, `sequence`, `capability_impact[]` (`capability`,`impact`,`reason`) | 200 (all 9 streams always present) | 503 | Poll | `runtime` |
| 4 | GET | `/api/v1/timeframes/{TF}/snapshot` | One stream snapshot | `TF` ∈ M1,M5,M15,M30,H1,H4,D1,W1,MN1 | object | `timeframe`, `has_closed_bar`, `observed`, `quality` (string when unobserved; object when observed), `freshness`, `last_successful_update`, `sequence`, `open`, `high`, `low`, `close`, `open_time`, `capability_impact[]` | 200 | 400 `unknown_timeframe`; 503 | Poll | `runtime` |
| 5 | GET | `/api/v1/signals/latest` | Latest decision | none | object | `available`, `decision_id`, `trigger_timeframe`, `closed_bar_id`, `signal`, `score`, `score_is_probability`, `confidence`, `probability`, `probability_calibrated`, `reference_price`, `system_mode`, `has_outcome`, `symbol`, `trigger_time`, `data_state`, `structure_state`, `regime_state`, `eligibility_state`, `strategy_version`, `configuration_version` (decision fields `UNKNOWN`/`null` when no live decision context) | 200 | 503 | Poll | `ledger` |
| 6 | GET | `/api/v1/risk/latest` | Portfolio risk + per-decision proposal | none | object | `available`, `open_positions`, `aggregate_open_risk_fraction`, `risk_bounded_by_guardian`, `proposal_available`, `proposal` (null unless a decision has been evaluated; otherwise `decision_id`,`direction`,`entry_price`,`stop_price`,`target_price`,`risk_fraction`,`risk_amount`,`position_size_lots`,`reward_risk_ratio`,`decision`,`reason`,`valid`) | 200 | 503 | Poll | `positions` |
| 7 | GET | `/api/v1/shadow/positions` | Simulated positions | none | array of objects | `position_id`, `decision_id`, `direction`, `state`, `entry_price`, `stop_price`, `target_price`, `lots`, `risk_fraction`, `timeframe`, `opened_at`, `opened_bar_open`, `exit_price`, `realized_pnl`, `realized_r`, `close_reason`, `closed_at`, `closed_bar_open`, `shadow_only` | 200 (array may be empty) | 503 | Poll | `positions` |
| 8 | GET | `/api/v1/shadow/outcomes` | Completed outcomes | none | array of objects | `outcome_id`, `position_id`, `decision_id`, `direction`, `timeframe`, `exit_state`, `entry_price`, `exit_price`, `lots`, `realized_pnl`, `realized_r`, `risk_fraction`, `bars_held`, `outcome_class`, `recorded_at`, `note`, `shadow_only` | 200 (array may be empty) | 503 | Poll | `runtime` (outcome engine); `ledger` fallback |
| 9 | GET | `/api/v1/research/status` | Research status + history | none | object | `available`, `mode`, `note`, `experiment_count`, `experiments[]` (`experiment_id`,`hypothesis_id`,`method`,`outcome`,`sample_size`,`result_metric`,`started_at`), `failure_count`, `failures[]` (`failure_id`,`category`,`summary`,`occurrences`,`resolved`,`last_seen`) | 200 | — | Poll | `runtime` (optional) |
| 10 | GET | `/api/v1/governance/status` | Approval state + history | none | object | `pending_count`, `pending[]` (`request_id`,`kind`,`subject_id`,`requested_at`), `history[]` (`request_id`,`kind`,`status`,`requested_by`,`requested_at`,`decided_at`), `live_trading_authorised` | 200 | 503 | Poll | `approvals` |
| 11 | GET | `/api/v1/bridge/status` | Python MT5 bridge identity/health | none | object | `bridge_process_state`, `startup_stage`, `handshake_ok`, `mt5_ready`, `resolved_symbol`, `transport`, `host`, `loopback_only`, `managed_by_application`, `requires_manual_cmd`, `observed`, `package_available`, `initialized`, `mt5_ready_live`, `broker`, `server`, `bridge_symbol`, `process_state`, `last_error`, `last_successful_request` (broker/server `null` until observed) | 200 | 503 | Poll | `runtime` |
| 12 | GET | `/api/v1/audit/recent` | Append-only audit stream + incidents | none | object | `audit_stream_size`, `audit_records[]` (`sequence`,`event_id`,`action`,`outcome`,`service_state`,`occurred_at`,`actor`,`subject`,`details`,`previous_hash`,`record_hash`; newest first, ≤200), `active_incidents[]` (`incident_id`,`severity`,`state`,`title`), `count` | 200 | 503 | Poll | `incidents` (+ `runtime` for the stream) |

Commands (not GET routes; invoked through the command contract, §14):

| Method | Command | Payload | Success | Error |
|--------|---------|---------|---------|-------|
| command | `notify` | `actor`, `payload` | 202 `{accepted:true, message_id}` | 403 `command_not_permitted`; 400 `actor_required`; 429 `queue_full`; 503 |
| command | `request_approval` | `actor`, `payload` | 202 `{accepted:true, request_id, status:"PENDING"}` | 403; 400; 503 |

Observed wire examples (captured from the running backend):

```json
GET /api/v1/system/state
{"api":"v1","schema":"1.0","data":{"mode":"STARTING","shadow_only":true,"ready":false,"bridge_state":"OFFLINE","api":"v1","schema":"1.0"}}

GET /api/v1/signals/latest  (no decisions yet)
{"api":"v1","schema":"1.0","data":{"available":false,"reason":"no decisions recorded yet"}}

GET /api/v1/risk/latest
{"api":"v1","schema":"1.0","data":{"available":true,"open_positions":0,"aggregate_open_risk_fraction":0,"risk_bounded_by_guardian":true}}

GET /api/v1/timeframes/M15/snapshot  (observed)
{"api":"v1","schema":"1.0","data":{"timeframe":"M15","has_closed_bar":true,"observed":true,"quality":{"state":"VALID","decision_grade":true},"sequence":1,"open":2000,"high":2005,"low":1995,"close":2001,"open_time":1700000000}}

GET /api/v1/timeframes/M15/snapshot  (not observed)
{"api":"v1","schema":"1.0","data":{"timeframe":"M15","has_closed_bar":false,"quality":"UNKNOWN","observed":false}}
```

---

## 5. System state

`SystemMode` (`src/foundation/SystemMode.h`), exposed at
`/api/v1/system/state` → `mode`. Nine values exist in the enum; the runtime
currently transitions through `STARTING`, `SHADOW`, `DEGRADED`, `EMERGENCY`
(startup failure), and `HALTED` (after `stop()`). The others are contract
values reserved by the enum.

| Mode | Meaning | UI should communicate | Capabilities available | Capabilities disabled |
|------|---------|-----------------------|------------------------|-----------------------|
| STARTING | Backend is starting up | Startup progress; no data yet | Health/state endpoints | Signals, market data, shadow |
| SHADOW | Normal operating mode (shadow only) | Calm "SHADOW / simulated" banner; never "live" | All read routes; shadow lifecycle | Live execution (never exists) |
| DEGRADED | Operational timeframe not fresh / partial failure | Warning; name the failed timeframe | Health, timeframes, audit | Decision-grade signals/risk from affected streams |
| RECOVERY | Restart/recovery in progress (enum value) | Recovering banner | Health/state | Data-dependent views until fresh |
| NORMAL | Enum value; not used by the shadow-first runtime | — | — | — |
| PAUSED | Enum value | Paused banner | Read routes | New decisions |
| MANUAL | Enum value | Manual banner | Read routes | Automated decisions |
| EMERGENCY | Startup failed hard | Error banner; needs operator | Health/audit | All data-dependent capability |
| HALTED | Runtime stopped | Stopped | — | Everything |

The frontend must distinguish **system mode** (`mode`) from **service state**
(`bridge_state`, and health `aggregate`). They are different vocabularies and
must not be conflated.

---

## 6. Service / subsystem health

Health is derived by `HealthMonitor` (`src/health/HealthMonitor.cpp`) and
surfaced at `/api/v1/health`.

- **Service vocabulary** (`ServiceState`): `STARTING, ONLINE, DEGRADED,
  OFFLINE, RECOVERING, PAUSED, BLOCKED, ERROR`.
- **Backend health:** `aggregate` is the most-severe observed service state
  (`HealthStateEngine::aggregateState`). `decision_grade_data` reflects the M15
  stream only. `degraded_reasons[]` lists services that are DEGRADED or not
  usable. `unknown_is_not_safe` is always `true`.
- **Python bridge health:** observed via `observeBridge`. The bridge is
  `ONLINE` only when the package is available and MT5 is ready; otherwise
  `DEGRADED` (bridge up, MT5 not ready) or `OFFLINE` (bridge unreachable).
- **MT5 connectivity state:** folded into the bridge service state; a bridge
  that is up but with MT5 not ready is `DEGRADED`, not `ONLINE`.
- **Timeframe health:** `observeTimeframes` marks each `timeframe-<TF>` service
  `ONLINE` when a closed bar is present and decision-grade, otherwise
  `DEGRADED` or `STARTING` (not yet observed).
- **Data freshness:** tracked internally as `FreshnessState` (`FRESH`, `STALE`,
  `UNKNOWN`) and folded into `quality`. **Freshness is not emitted by the API**
  (see D3).
- **Degradation:** propagated through `degraded_reasons[]` and per-timeframe
  `quality`.
- **Recovery:** bounded by `ProcessSupervisor` / `RestartPolicy`; not directly
  exposed as a route.
- **Error visibility:** structured error objects (see §14).

> **D3 — freshness now exposed (RESOLVED).** Both `/api/v1/timeframes` and
> `/api/v1/timeframes/{TF}/snapshot` emit `freshness`
> (`{state,is_fresh,last_update,age_millis,max_age_millis}`),
> `last_successful_update`, and a per-timeframe `capability_impact[]`
> (`{capability,impact,reason}`). `FreshnessState` is `FRESH | STALE |
> UNKNOWN`; only `FRESH` is current, and an unobserved stream reports
> `freshness:null` rather than a fabricated value. Alpha renders these fields
> directly and still must not compute freshness itself.

---

## 7. Market data

Nine canonical streams are implemented (`Timeframe` enum): `M1, M5, M15, M30,
H1, H4, D1, W1, MN1`.

The frontend obtains market-data information **only** through:

- `/api/v1/timeframes` — per-stream summary (quality, closed-bar presence,
  last closed bar open time, sequence);
- `/api/v1/timeframes/{TF}/snapshot` — last closed bar OHLC for one stream.

Distinctions the frontend must preserve:

- **Current/forming bar:** never exposed. The backend only ever publishes
  bars that have fully closed (`BarFinalizer`: a bar closes when
  `now >= openTime + interval + grace`). There is no forming-candle endpoint.
- **Closed bar:** the bar reported by a snapshot is the last **closed** bar.
- **Last known closed bar:** identified by `open_time` and `last_closed_bar_open`
  (epoch seconds) plus the per-stream `sequence`.
- **Stale / missing / invalid / unknown:** represented through `quality`
  (`VALID, DEGRADED, INVALID, UNKNOWN, STALE, MISSING, OUT_OF_ORDER,
  DUPLICATE, INCOMPLETE`). Only `VALID` is decision-grade
  (`decision_grade:true`). An unobserved stream returns
  `observed:false`, `quality:"UNKNOWN"`.

> The frontend must **never** infer a completed candle from an unconfirmed
> current bar. No such data is provided, and no client-side candle
> construction is permitted.

> **D2 — timeframe list shape and order (RESOLVED).** `/api/v1/timeframes` now
> returns an **array in canonical M1, M5, M15, M30, H1, H4, D1, W1, MN1
> order**, and every element (observed or not) carries the same shape:
> `observed`, `has_closed_bar`, `quality` (`{state,decision_grade}`),
> `decision_grade`, `freshness`, `last_successful_update`,
> `last_closed_bar_open`, `sequence`, `capability_impact[]`. All nine streams
> are always present. `/api/v1/timeframes/{TF}/snapshot` uses the same
> observed shape plus OHLC. (An unobserved snapshot keeps a bare-string
> `quality:"UNKNOWN"` for backward compatibility.) Alpha may rely on array
> order but should still key by the `timeframe` field.

---

## 8. Timeframe authority

Implemented in `src/mt5/Mt5BridgeContract.h`:

- `isPrimaryOperational(tf)` → **M15** is the primary operational/setup
  timeframe.
- `isPrimaryStructural(tf)` → **H4** is the primary structural authority.
- M5 / M1 (and TICK in the master spec) are execution/microstructure context.

Other roles per `src/api/RuntimeManifest.json`:

- `M15` = `primary_operational`
- `H4` = `primary_structural`
- `M5`, `M1` = `execution_context`
- `TICK` = `microstructure_context` (tick is a bridge concept; no
  frontend route exposes ticks)

The backend decision chain currently runs on the **operational timeframe (M15)**
only (`DecisionPipelineConfig::operationalTimeframe`). The frontend should
present M15 as the decision/setup timeframe and H4 as structural context. It
must not itself combine timeframes into a decision.

---

## 9. Signals and decisions

Source: `/api/v1/signals/latest`, backed by `PredictionLedger`
(`src/ledger/PredictionLedger.h`) and the decision pipeline.

Exposed fields and meaning:

| Field | Meaning |
|-------|---------|
| `available` | `false` when no decision has been recorded; then `reason` is present |
| `decision_id` | Deterministic identity: `"<TF>-<barOpenSec>-<DIRECTION>"` |
| `trigger_timeframe` | Timeframe that produced the decision (currently `M15`) |
| `closed_bar_id` | `openTimeSec` of the closed bar (epoch seconds) |
| `signal` | `LONG`, `SHORT`, or `NONE` |
| `score` | Ranking score 0..100 (**not** a probability) |
| `score_is_probability` | Always `false` |
| `confidence` | 0..1 meta-measure of decision quality |
| `probability` | Always the literal JSON `null` in this build |
| `probability_calibrated` | Always `false` |
| `reference_price` | Candidate reference price |
| `system_mode` | Always `"SHADOW"` |
| `has_outcome` | Whether an outcome is linked |

**SCORE != PROBABILITY.** The score is a ranking value; probability is
uncalibrated. The backend emits `probability: null` and
`score_is_probability: false` precisely so the UI cannot present score as a
probability. Alpha must render probability as "N/A (uncalibrated)" and must
never display the score as a probability.

> **D4 — decision display contract fully exposed (RESOLVED).**
> `/api/v1/signals/latest` now emits `symbol`, `trigger_time`, `data_state`,
> `structure_state`, `regime_state`, `eligibility_state`, `strategy_version`,
> and `configuration_version`, taken from the live `DecisionContext` captured
> by the decision chain. When the process holds no live decision context (for
> example a ledger recovered after restart), these fields are emitted as
> explicit `UNKNOWN`/`null`, never guessed.

---

## 10. Risk

Source: `/api/v1/risk/latest`, backed by `PositionSimulator`.

Exposed fields:

| Field | Meaning |
|-------|---------|
| `available` | Always `true` when the position simulator is present |
| `open_positions` | Count of currently open simulated positions |
| `aggregate_open_risk_fraction` | Sum of open-position risk fractions |
| `risk_bounded_by_guardian` | Always `true`; risk is capped by Guardian policy |

Risk is backend-owned. The frontend must not compute position size, risk
fraction, stop distance, or portfolio limits.

> **D5 — per-decision risk proposal exposed (RESOLVED).** `/api/v1/risk/latest`
> now emits the last `RiskProposal` computed by `RiskEngine`
> (`decision_id`, `direction`, `entry_price`, `stop_price`, `target_price`,
> `risk_fraction`, `risk_amount`, `position_size_lots`, `reward_risk_ratio`,
> `decision`, `reason`, `valid`) under `proposal`, gated by
> `proposal_available`. Until a decision has been evaluated in the process,
> `proposal_available` is `false` and `proposal` is `null`. Portfolio-level
> fields are unchanged.

---

## 11. Shadow execution

Frontend-visible shadow information and its sources:

| Concept | Exposed? | Source / field |
|---------|----------|----------------|
| Opportunity / candidate | No | Not exposed (internal signal candidate) |
| Risk proposal | No | See D5 |
| Shadow command | No | `ShadowExecutionEngine` internal; not exposed |
| Simulated fill | Partial | `/api/v1/shadow/positions` `entry_price`, `state` |
| Shadow position | Yes | `/api/v1/shadow/positions` |
| Exit | Partial | `state` (`CLOSED_TARGET/CLOSED_STOP/CLOSED_MANUAL/CLOSED_EXPIRED`), `realized_r` |
| Outcome | Yes | `/api/v1/shadow/outcomes` |
| Reconciliation | No | `ReconciliationEngine` internal; not exposed |

Shadow position fields: `position_id`, `decision_id`, `direction`, `state`,
`entry_price`, `stop_price`, `target_price`, `lots`, `risk_fraction`,
`timeframe`, `opened_at`, `opened_bar_open`, `exit_price`, `realized_pnl`,
`realized_r`, `close_reason`, `closed_at`, `closed_bar_open` (null while
open), `shadow_only` (always `true`).

`PositionState`: `OPEN, CLOSED_TARGET, CLOSED_STOP, CLOSED_MANUAL,
CLOSED_EXPIRED`.

The frontend must **not** implement its own shadow engine, fills, or P&L. All
fills are simulated deterministically by the backend against closed bars
(conservatively: stop takes precedence when a bar touches both stop and
target).

> **D8 — shadow position detail exposed (RESOLVED).** `/api/v1/shadow/positions`
> now emits `realized_pnl`, `exit_price`, `close_reason`, `opened_at`,
> `opened_bar_open`, `closed_at`, `closed_bar_open`, `risk_fraction`, and
> `timeframe`. `/api/v1/shadow/outcomes` is sourced from the outcome engine
> (`OutcomeEngine`) and includes `position_id`, `direction`, `timeframe`,
> `exit_state`, `entry_price`, `exit_price`, `lots`, `realized_pnl`,
> `realized_r`, `risk_fraction`, `bars_held`, `recorded_at`, and `note`.

---

## 12. Outcomes

Source: `/api/v1/shadow/outcomes`, driven by the ledger's linked outcomes.

| Field | Meaning |
|-------|---------|
| `decision_id` | Decision the outcome belongs to |
| `outcome_id` | Deterministic outcome identity |
| `realized_r` | Realized result in units of risk |
| `outcome_class` | `WIN`, `LOSS`, `BREAKEVEN`, `UNKNOWN` |

Only decisions with a linked outcome are returned (`has_outcome == true`).
The array is empty until a simulated position closes and an outcome is
recorded.

> **D6 — append-only audit stream exposed (RESOLVED).** `/api/v1/audit/recent`
> now returns the authoritative append-only, hash-chained audit stream under
> `audit_records` (newest first, bounded to 200) plus `audit_stream_size`, and
> keeps `active_incidents` as a separate, clearly named view. Audit records and
> incidents are distinct and are no longer conflated.

---

## 13. Persistence / audit / research visibility

| Area | Exposed | Notes |
|------|---------|-------|
| Persistence status | No | File-backed store is internal; no status route |
| Audit | Yes | `/api/v1/audit/recent` → `audit_records[]` + `active_incidents[]` |
| Research status | Yes | `/api/v1/research/status` → `available`, `mode`, `note`, `experiments[]`, `failures[]` |
| Experiment information | Yes | `experiments[]` (`experiment_id`,`hypothesis_id`,`method`,`outcome`,`sample_size`,`result_metric`,`started_at`) |
| Failures | Yes | `failures[]` (`failure_id`,`category`,`summary`,`occurrences`,`resolved`,`last_seen`) plus health `degraded_reasons` |
| Recovery | Partial | Surfaced through failure memory (`resolved`, `occurrences`); no dedicated route |
| History | Yes | Approval `history[]`; audit stream; experiment/failure history |

> **D7 — research/governance history exposed (RESOLVED).** `/api/v1/research/status`
> now emits `experiments[]` and `failures[]` from the runtime's
> `ExperimentLedger` and `FailureMemory`. `/api/v1/governance/status` adds a
> full `history[]` of approval requests (pending, approved, rejected,
> withdrawn) alongside the pending queue. Research output still grants no
> execution authority. Dedicated recovery-timeline and evolution routes remain
> out of the v1 contract.

---

## 14. Error model

All errors use `{"error":true,"code":...,"message":...}` with a matching HTTP
status.

| Status | Code | Trigger | Retry? | Disable capability? | Show | User action |
|--------|------|---------|--------|---------------------|------|-------------|
| 400 | `unknown_timeframe` | Snapshot for an unknown TF | No | That view only | ERROR (input) | Fix selection |
| 400 | `actor_required` | Command without actor | Yes, with actor | Command only | ERROR (input) | Provide actor |
| 403 | `command_not_permitted` | Command not on allow-list (e.g. `live.execute`) | No | Command | ERROR (policy) | None; this is by design |
| 404 | `not_found` | Unknown route | No | — | ERROR | None |
| 405 | `method_not_allowed` | Non-GET on a read route | No | — | ERROR | None |
| 429 | `queue_full` | Notification queue full | Yes, later | Notify only | DEGRADED | Optional |
| 503 | `dependency_unavailable` | Facade dependency missing | Yes | The dependent view | DEGRADED (transient) / ERROR (persistent) | None |

Guidance:

- Treat 5xx/503 as **DEGRADED**, not ERROR, unless it persists; never fabricate
  a substitute value.
- Treat 403 `command_not_permitted` as a **policy outcome**, not a failure.
- Never render a safe/zero default for a missing value.

---

## 15. Refresh / update model

Implemented behaviour: **request/response snapshots, no push**.

- There is **no websocket, no server-sent events, and no event stream** in the
  backend.
- The facade returns the current in-memory state on each call.
- The backend host ticks on an interval (`--interval-ms`, default in
  `AuraBackendHost.cpp`); state advances only on tick.

Recommended frontend behaviour (based on the implementation, not invented):

- Poll `system/state` and `health` at a modest interval (e.g. 1–2 s).
- Poll `timeframes` / snapshots, `signals/latest`, `risk/latest`,
  `shadow/positions`, `shadow/outcomes` at a similar cadence.
- Treat every response as a full snapshot; replace state rather than merging.
- No optimistic updates for commands; reflect the returned 202/error.

> **D1 applies:** until a concrete transport exists, Alpha cannot poll an
> external process. See §2 and §19.

---

## 16. Python MT5 bridge

What the frontend needs to know:

- The bridge is a **separate child process**, started and supervised by the
  backend; the frontend never launches or talks to it.
- **Loopback boundary:** the bridge binds `127.0.0.1` only; it is never exposed
  to LAN/WAN.
- **Health:** surfaced indirectly through `bridge_state` in
  `/api/v1/system/state` (a `ServiceState`) and through `/api/v1/health`
  aggregate + `degraded_reasons`.
- **MT5 availability:** a bridge that is up but with MT5 not ready is
  `DEGRADED`; data is then not decision-grade.
- **Timeframe availability:** via `/api/v1/timeframes` and snapshots.
- **Broker/source identity:** exposed at `/api/v1/bridge/status`
  (`broker`, `server`, `bridge_symbol`, `last_error`,
  `last_successful_request`); `null` until the bridge is observed.
- **Degraded state:** any MT5-unavailable condition must be shown explicitly,
  never as an empty-but-fine dashboard.

> **D9 — bridge/broker identity exposed (RESOLVED).** `/api/v1/bridge/status`
> emits the bridge process state, startup stage, handshake/MT5 readiness,
> resolved symbol, transport (`http_loopback`), loopback-only and
> managed-by-application flags, `requires_manual_cmd:false`, and — when the
> bridge has been observed — `package_available`, `initialized`, `broker`,
> `server`, `bridge_symbol`, `process_state`, `last_error`, and
> `last_successful_request`. Before observation these are explicit `null`/
> `false`, never fabricated.

---

## 17. Frontend ownership

### Alpha MUST implement

- Desktop shell, navigation, theming per `ASTRA_VISUAL_IDENTITY.md`.
- Backend connection via the approved facade contract over the loopback HTTP
  transport at `http://127.0.0.1:8790/api/v1/*` (§2; D1 resolved).
- Rendering of system mode, service health, and the distinction between them.
- Market-data display for the nine streams, with explicit quality/freshness
  labels and no fabricated values.
- Signal/decision, score, confidence, and probability-as-N/A presentation.
- Risk display from backend fields only.
- Shadow position and outcome views, clearly labelled **SIMULATED / SHADOW**.
- Structured error rendering with degraded/retry guidance.
- Command submission for the allow-list (`notify`, `request_approval`) with
  actor attribution.
- Startup/readiness UX, including MT5-unavailable handling.

### Alpha MUST NOT implement

- Signal generation, scoring, confidence, or probability computation.
- Risk sizing, stop/target calculation, or portfolio limits.
- Shadow fills, P&L, or position lifecycle logic.
- Candle construction or inference of a closed bar from a forming bar.
- Any market-data provider or direct MT5 access.
- Direct calls to the Python bridge.
- Persistence writes.
- Any live-execution path or attempt to widen authority.
- Bypassing or reinterpreting backend policy.

---

## 18. UI data mapping

| UI area | Backend source | API/route | Field(s) | Update trigger | Fallback/degraded behavior |
|---------|----------------|-----------|----------|----------------|----------------------------|
| Dashboard | Runtime + health | `/api/v1/system/state`, `/api/v1/health` | `mode`, `ready`, `bridge_state`, `aggregate`, `decision_grade_data` | Poll | Show STARTING/DEGRADED/EMERGENCY banner; no fake data |
| Market Status | Timeframe store | `/api/v1/timeframes` | `timeframe`, `quality`, `decision_grade`, `has_closed_bar` | Poll | Non-VALID = not healthy; show STALE/UNKNOWN explicitly |
| Price | Timeframe store | `/api/v1/timeframes/{TF}/snapshot` | `open`,`high`,`low`,`close`,`open_time` | Poll | `observed:false` → no price shown |
| Timeframes | Timeframe store | `/api/v1/timeframes` | per-stream quality, `freshness`, `last_successful_update`, `capability_impact` | Poll | Canonical order is guaranteed |
| Signal | Prediction ledger + decision context | `/api/v1/signals/latest` | `signal`, `decision_id`, `trigger_timeframe`, `closed_bar_id`, `symbol`, `trigger_time`, `data_state`, `structure_state`, `regime_state`, `eligibility_state`, `strategy_version`, `configuration_version` | Poll | `available:false` → "no decision yet"; decision states `UNKNOWN` without a live cycle |
| Score | Prediction ledger | `/api/v1/signals/latest` | `score`, `score_is_probability:false` | Poll | Never present as probability |
| Confidence | Prediction ledger | `/api/v1/signals/latest` | `confidence` | Poll | Show as meta-measure, not probability |
| Risk | Position simulator + decision context | `/api/v1/risk/latest` | `open_positions`, `aggregate_open_risk_fraction`, `risk_bounded_by_guardian`, `proposal_available`, `proposal` | Poll | `proposal:null` until a decision is evaluated |
| Shadow Position | Position simulator | `/api/v1/shadow/positions` | `state`, `entry_price`, `stop_price`, `target_price`, `lots`, `risk_fraction`, `exit_price`, `realized_pnl`, `realized_r`, `close_reason`, `opened_at`, `closed_at`, `shadow_only` | Poll | Label SIMULATED |
| Outcome | Outcome engine | `/api/v1/shadow/outcomes` | `outcome_class`, `realized_r`, `realized_pnl`, `exit_state`, `bars_held` | Poll | Empty until an outcome exists |
| Health | Health monitor | `/api/v1/health` | `aggregate`, `degraded_reasons` | Poll | `unknown_is_not_safe` always true |
| Bridge | Runtime + bridge client | `/api/v1/bridge/status` | `bridge_process_state`, `mt5_ready`, `handshake_ok`, `broker`, `server`, `last_error` | Poll | `broker`/`server` `null` until observed |
| Alerts | Incident manager + audit log | `/api/v1/audit/recent` | `active_incidents`, `audit_records`, `audit_stream_size` | Poll | `audit_records:[]` until events occur |
| System State | Runtime | `/api/v1/system/state` | `mode`, `ready`, `bridge_state`, `startup_stage` | Poll | Distinguish from service state |
| Audit/Research status | Audit log / research / governance | `/api/v1/audit/recent`, `/api/v1/research/status`, `/api/v1/governance/status` | `audit_records`, `experiments`, `failures`, `history` | Poll | Arrays empty until events occur |

---

## 19. No-guessing rule

Alpha must never invent a missing field or API. When required UI information
is not exposed by the backend, mark it:

**BACKEND DATA NOT EXPOSED**

rather than creating a frontend-side calculation or a new endpoint.

All D-gaps previously listed here are now exposed by the backend:

- concrete frontend↔backend transport (D1) → `LoopbackApiServer`, loopback
  HTTP/JSON on `127.0.0.1:8790`;
- per-timeframe freshness / last successful update / capability impact (D3) →
  `/api/v1/timeframes` and `/api/v1/timeframes/{tf}/snapshot`;
- decision `symbol`, `trigger_time`, `data_state`, `structure_state`,
  `regime_state`, `eligibility_state`, `strategy_version`,
  `configuration_version` (D4) → `/api/v1/signals/latest`;
- per-decision risk proposal (D5) → `/api/v1/risk/latest`;
- full append-only audit stream (D6) → `/api/v1/audit/recent`
  (`audit_records` + `audit_stream_size`);
- research/experiment/failure history (D7) → `/api/v1/research/status`;
- shadow exit price, realized P&L, close reason, open/close timestamps (D8) →
  `/api/v1/shadow/positions` and `/api/v1/shadow/outcomes`;
- bridge/broker identity and MT5 readiness detail (D9) →
  `/api/v1/bridge/status`.

Where a value is genuinely not yet known (no decision evaluated in this
process, bridge not observed, timeframe never seen), the backend emits an
explicit `null` or `UNKNOWN`, never a fabricated value. Alpha still must not
invent data.

---

## 20. Files Alpha must read

- `project-control/HANDOFF.md`
- `project-control/FRONTEND_INTEGRATION_MAP.md` (this document)
- `docs/architecture/BACKEND_FRONTEND_API_V1.md`
- `docs/brand/ASTRA_VISUAL_IDENTITY.md`
- `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`
- `docs/architecture/BACKEND_DONE_DEFINITION.md`
- `docs/architecture/MT5_PYTHON_BRIDGE_V1.md`
- `src/api/BackendFacade.h`, `src/api/BackendFacade.cpp`
- `src/api/BackendApiSchema.h`, `src/api/RuntimeManifest.json`
- `src/foundation/DataQualityState.h`, `src/foundation/ServiceState.h`,
  `src/foundation/SystemMode.h`
- `src/mt5/Mt5BridgeContract.h` (timeframe authority, bridge contract)
- `src/ledger/PredictionLedger.h`, `src/shadow/PositionSimulator.h`,
  `src/outcomes/Outcome.h`
- `project-control/BACKEND_REVIEW.md`, `project-control/TEST_LOG.md`

---

## 21. Implementation order for Alpha

1. **Foundation** — shell, routing, theming tokens from the visual identity.
2. **Backend connection** — bind to the approved facade contract; resolve D1
   with the backend owner before coding against a transport.
3. **System state** — `mode` / `ready` / `bridge_state` with startup UX.
4. **Health** — aggregate, degraded reasons, unknown-is-not-safe.
5. **Market data** — nine streams, quality labels, closed-bar semantics.
6. **Signals** — decision, score, confidence, probability-as-N/A.
7. **Risk** — backend risk fields only.
8. **Shadow** — positions and outcomes, clearly simulated.
9. **Research / audit** — incident and research status.
10. **Visual polish** — ASTRA brand, accessibility, degraded/error states.

This order follows the backend capability dependency order (data before
decisions, decisions before shadow).

---

## 22. Acceptance checklist

- [ ] Backend connection uses only the documented facade contract.
- [ ] All API calls use implemented routes and exact field names.
- [ ] Response envelopes (`api`/`schema`/`data`) and error objects parsed
      correctly.
- [ ] System mode and service state are displayed as distinct vocabularies.
- [ ] DEGRADED is shown as not-healthy; no safe default is substituted.
- [ ] MT5-unavailable state is explicit and blocks data-dependent views.
- [ ] STALE / MISSING / INVALID / UNKNOWN data is labelled and never shown as
      fresh.
- [ ] No fabricated values; missing values render as "not available".
- [ ] No frontend-owned trading logic (signals, score, risk, fills).
- [ ] Score is never presented as a probability; probability shows N/A.
- [ ] Scope is XAUUSD only; no other asset is implied.
- [ ] M15 is presented as the operational/setup timeframe; H4 as structural.
- [ ] Startup UX requires no manual Python/CMD step.
- [ ] Errors are visible and classified (retry / disable / policy).
- [ ] No former D-gap remains marked `BACKEND DATA NOT EXPOSED`; where the
      backend reports `null`/`UNKNOWN`, render it as unavailable, never invented.

---

## 23. Known limitations

Backend evidence limitations (from `project-control/TEST_LOG.md`):

- **Real MT5 / broker retrieval not verified** in this environment (no
  MetaTrader5 package or broker terminal). Bridge contract, handshake, and
  loopback behaviour are tested; real XAUUSD candle retrieval is not claimed.
- **Windows double-click execution not verified** in this environment (Linux
  toolchain used; Windows path is compiled but not executed).

These are backend evidence gaps, not frontend assumptions. Alpha must not
assume real broker data is present, and must design for the explicit
MT5-unavailable and degraded paths.

---

## 24. Discrepancies found (documentation vs implementation)

| ID | Discrepancy | Implemented reality | Frontend consequence |
|----|-------------|---------------------|----------------------|
| D1 | Docs describe a "local application API / in-process façade / approved transport"; no concrete frontend transport was implemented | RESOLVED: `aura::LoopbackApiServer` publishes the facade over loopback HTTP/JSON on `127.0.0.1:8790`; started/stopped by the host; non-loopback binds refused | Alpha connects to `http://127.0.0.1:8790/api/v1/*`; commands via `POST /api/v1/command` |
| D2 | Docs imply a uniform timeframe contract | `/timeframes` now returns the array in canonical M1..MN1 order; `quality` is a `{state, decision_grade}` object plus `freshness`, `last_successful_update`, `capability_impact` | Key by `timeframe`; order is stable |
| D3 | Docs list freshness / last successful update / capability impact | RESOLVED: emitted per timeframe in `/timeframes` and `/timeframes/{tf}/snapshot` | Available |
| D4 | Decision display contract lists symbol/time/state/versions | RESOLVED: `/signals/latest` emits symbol, trigger_time, data/structure/regime/eligibility state, strategy/configuration versions (explicit `UNKNOWN`/`null` when no live decision context) | Available |
| D5 | Docs imply risk proposal visibility | RESOLVED: `/risk/latest` emits portfolio risk plus the last per-decision `proposal` (`proposal_available` gates it) | Available |
| D6 | "Audit" implies an append-only audit stream | RESOLVED: `/audit/recent` emits `audit_records` (hash-chained, newest first) + `audit_stream_size`, alongside `active_incidents` | Available |
| D7 | Docs imply research/governance visibility | RESOLVED: `/research/status` emits `experiments` + `failures`; `/governance/status` emits `history` | Available |
| D8 | Shadow lifecycle implies full detail | RESOLVED: `/shadow/positions` emits exit price, realized P&L/R, close reason, open/close timestamps; `/shadow/outcomes` uses the outcome engine | Available |
| D9 | Bridge identity/MT5 readiness implied visible | RESOLVED: `/bridge/status` emits transport, loopback flag, managed-by-application, handshake, MT5 readiness, broker/server/symbol (explicit unknown when unobserved) | Available |

All nine discrepancies were closed in the backend (transport + facade
endpoints). The corresponding contract tests live in
`tests/FrontendContractD1D9Tests.cpp` (TST-0021).

---

## 25. Final handoff statement

Alpha must treat this document as the implementation map for connecting the
ASTRA frontend to the completed AURA backend. When this document conflicts
with actual backend implementation, actual implemented behavior must be
reported and the discrepancy must not be hidden.
