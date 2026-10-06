# HANDOFF.md

## Backend -> Frontend handoff contract

Frontend handoff is allowed only after:
1. all required backend tasks are implemented or explicitly documented as non-blocking accepted exceptions;
2. backend review gate is PASS;
3. Windows startup test confirms no CMD requirement;
4. bridge handshake and market-data integration tests have evidence;
5. `BACKEND_FRONTEND_API_V1.md` is frozen for the frontend release;
6. project state is updated to `BACKEND_APPROVED_FOR_FRONTEND`.

## Status at this checkpoint
- (1) 222/222 manifest outputs implemented. See `BACKEND_REVIEW.md`.
- (2) Backend review gate: **PASS** (documented evidence limitations).
- (3) Startup is executable-root based and CWD-independent (tested). The
  Windows double-click path is compiled but was not executed in this
  environment — treat as a documented limitation, not a claim.
- (4) Bridge handshake/loopback/schema and error contracts have real evidence
  (`PythonBridgeContractTests`), and the host was started for real on Linux with
  the bridge reaching `ONLINE`. Real MT5 candle retrieval has **no** evidence
  (no broker here). The live decision chain has real evidence
  (`DecisionPipelineIntegrationTests`).
- (5) `BACKEND_FRONTEND_API_V1.md` is implemented by `src/api/BackendApiSchema.h`
  and `BackendFacade` (api `v1`, schema `1.0`). Freeze it before Alpha starts.
- (6) `PROJECT_STATE.md` is set to `BACKEND_REVIEW_PASS`; flip to
  `BACKEND_APPROVED_FOR_FRONTEND` only after a human accepts the two documented
  limitations (Windows launch evidence, real MT5 evidence).

---

# Alpha frontend handoff package

> The authoritative connection map for Alpha is
> `project-control/FRONTEND_INTEGRATION_MAP.md`. It records the implemented
> API surface, states, and the documentation-vs-implementation discrepancies.
> All nine discrepancies (D1–D9) are **resolved in the backend**: the facade
> is published over a loopback-only HTTP/JSON transport on `127.0.0.1:8790`,
> and the previously unexposed freshness, decision, risk-proposal, audit,
> research, shadow-exit, and bridge-identity data are now emitted (with
> explicit `null`/`UNKNOWN` where a value is genuinely unknown).

## 1. Backend architecture
- C++17 runtime core built as one static library `aura_core`; the only executable
  is `aura_backend_host` (the double-click entry point).
- Data path: Broker → MT5 Terminal → bundled Python bridge → loopback JSON →
  C++ ingestion → validation → timeframe state → features → structure → regime →
  eligibility → signal → score/confidence → macro/market quality → risk →
  shadow execution → positions → outcomes → persistence/audit/research.
- Python is a data-ingestion bridge only. Strategy/decision logic stays in C++.
- The live chain is driven by `src/runtime/DecisionPipeline.{h,cpp}` from
  `AuraRuntime::tick()`: each new closed M15 bar is evaluated through the full
  chain, the decision is recorded before any shadow command, and open positions
  advance against each newly closed bar. The same engines back the deterministic
  replay engine, so live and replay decisions are structurally identical.

## 2. Startup model
- Paths resolve from the executable location (`PathResolver`), never from CWD.
- `StartupCoordinator` locates the bundled bridge, launches it as a supervised
  child (`ProcessSupervisor`), waits for the handshake, and reports a stage.
- Failure to launch the bridge is explicit (`DEGRADED`) and diagnosable; the
  backend still starts so non-data capabilities remain available.
- The end user never runs `python` or a CMD script.

## 3. Python MT5 bridge model
- Source: `bridge/mt5_python/` (`bridge_service.py`, `mt5_client.py`,
  `schemas.py`, `read_candles.py`).
- Loopback-only (`127.0.0.1`), protocol `1.0`, schema `1.0`.
- Endpoints: `GET /v1/health`, `/v1/handshake`, `/v1/symbol`,
  `/v1/candles?symbol&timeframe&count&closed_only`, `/v1/tick`.
- Closed-bar default; the forming bar is never returned as decision-grade.
- MT5/symbol problems return structured errors with `code/state/message/recovery`.

## 4. APIs / contracts (frontend-facing, v1)
Facade: `src/api/BackendFacade.{h,cpp}`; schema helpers:
`src/api/BackendApiSchema.{h,cpp}`. Transport: `aura::LoopbackApiServer`
(`src/api/LoopbackApiServer.{h,cpp}`), loopback-only HTTP/JSON on
`http://127.0.0.1:8790/api/v1/*`, started/stopped by the host. Every response
is wrapped as `{"api":"v1","schema":"1.0","data":{...}}`; errors are
`{"error":true,"code","message"}` with a matching HTTP status.
Routes (GET only for reads):
- `GET /api/v1/system/state`
- `GET /api/v1/health`
- `GET /api/v1/timeframes`
- `GET /api/v1/timeframes/{TF}/snapshot` (TF ∈ M1,M5,M15,M30,H1,H4,D1,W1,MN1)
- `GET /api/v1/signals/latest`
- `GET /api/v1/risk/latest`
- `GET /api/v1/shadow/positions`
- `GET /api/v1/shadow/outcomes`
- `GET /api/v1/research/status`
- `GET /api/v1/governance/status`
- `GET /api/v1/bridge/status`
- `GET /api/v1/audit/recent`
Commands (versioned, policy-checked, allow-list): `notify`,
`request_approval` via `POST /api/v1/command` (or `BackendFacade::command`).
Live-execution commands are rejected with 403.

## 5. Data schemas
- `PredictionRecord`, `Outcome`, `SimulatedPosition`, `ShadowCommand`,
  `TimeframeState`, `Bar` — see the corresponding headers under `src/`.
- Quality vocabulary: `VALID, DEGRADED, INVALID, UNKNOWN, STALE, MISSING,
  OUT_OF_ORDER, DUPLICATE, INCOMPLETE`. Only `VALID` is decision-grade.
- Service vocabulary: `STARTING, ONLINE, DEGRADED, OFFLINE, RECOVERING, PAUSED,
  BLOCKED, ERROR`.
- Modes: `STARTING, SHADOW, DEGRADED, EMERGENCY, HALTED`.

## 6. Available states / health information
- `system/state`: mode, ready, bridge state, startup stage, api/schema versions.
- `health`: aggregate state, decision-grade flag, degraded reasons,
  `unknown_is_not_safe: true`.
- `timeframes`: per-stream quality, decision-grade flag, `freshness`
  (`FRESH|STALE|UNKNOWN`), `last_successful_update`, and `capability_impact[]`,
  in canonical M1..MN1 order.
- `timeframes/{TF}/snapshot`: explicit `observed:false` + `quality:"UNKNOWN"`
  when a stream has not been seen; `freshness`/`capability_impact` always present.
- `bridge/status`: bridge process/startup state, handshake/MT5 readiness,
  transport and loopback flags, and broker/server/symbol once observed.
- `audit/recent`: append-only hash-chained `audit_records` plus
  `active_incidents`.

## 7. Error model
- 400 unknown_timeframe / actor_required; 403 command_not_permitted;
  404 not_found; 405 method_not_allowed; 429 queue_full;
  503 dependency_unavailable.
- The frontend must render UNKNOWN/STALE/DEGRADED as non-healthy and must never
  substitute a safe default.

## 8. Backend capabilities
Data ingestion/validation/freshness, deterministic decision chain, risk, shadow
execution + positions + outcomes + reconciliation, persistence/restart,
audit/incidents, research/evolution/validation, governance/approval, operating
window/recovery, Telegram (queue-only), health/watchdog, packaging/launcher, and
the v1 API facade.

## 9. Limitations
- Score is a ranking value, **not** a probability; probability is uncalibrated
  (`null`).
- No live trading; SHADOW only.
- Real MT5 candle retrieval and Windows double-click launch were not executed in
  the build environment (see `TEST_LOG.md`).
- Telegram is queue-only and carries no execution authority.

## 10. Known issues
- See the "Defects found and fixed" list in `TEST_LOG.md`; all were fixed and
  are covered by tests. No open backend defects are known.

## 11. Development/testing commands (development only)
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 12. Packaged runtime expectations
- Layout: `resources/bridge/mt5_python/bridge_service.py`,
  `resources/python/` (bundled interpreter), `config/`, `data/`, `logs/` under
  the application root.
- `src/api/RuntimeManifest.json` declares api `v1`, mode `SHADOW`, asset
  `XAUUSD`, nine timeframes, `live_trading_authorised: false`, and
  `requires_manual_cmd: false`.

## 13. Exact files Alpha should read
- `project-control/FRONTEND_INTEGRATION_MAP.md` (authoritative connection map)
- `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`
- `docs/architecture/BACKEND_FRONTEND_API_V1.md`
- `docs/architecture/BACKEND_DONE_DEFINITION.md`
- `docs/architecture/MT5_PYTHON_BRIDGE_V1.md`
- `docs/brand/ASTRA_VISUAL_IDENTITY.md`
- `src/api/BackendFacade.h`, `src/api/BackendApiSchema.h`,
  `src/api/LoopbackApiServer.h`, `src/api/RuntimeManifest.json`
- `src/foundation/DataQualityState.h`, `src/foundation/ServiceState.h`,
  `src/foundation/SystemMode.h`
- `project-control/BACKEND_REVIEW.md`, `project-control/TEST_LOG.md`

## 14. Visual constraints from ASTRA brand docs
- Geometric A-symbol, ASTRA wordmark, dark navy / deep blue, cool gray,
  silver/white language, refined spacing (per `ASTRA_VISUAL_IDENTITY.md`).
- No frontend implementation has been started in this phase.

## Alpha must not
- change backend contracts without a new architecture decision;
- add market-data providers;
- implement live order execution;
- bypass the bridge/data validation boundary;
- use the frontend as a source of truth.
