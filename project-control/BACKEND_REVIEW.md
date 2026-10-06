# BACKEND_REVIEW.md

Automated backend review performed against:
`AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`, `GLOBAL_AI_CODING_RULES.md`,
`TASK_MANIFEST.yaml`, `BACKEND_BUILD_ORDER.md`, `BACKEND_DONE_DEFINITION.md`,
`BACKEND_FRONTEND_API_V1.md`, `MT5_PYTHON_BRIDGE_V1.md`.

## Result: PASS (with documented evidence limitations)

### 1. Task coverage
- 222/222 manifest `output_path` entries exist on disk (verified programmatically).
- Manifest statuses updated from `PLANNED` to `IMPLEMENTED`; `authority` fields
  that were `PROPOSED` now read `IMPLEMENTED` (project-decision entries kept).

### 2. Architecture (BACKEND_DONE_DEFINITION §A)
- Python + MetaTrader5 is the only candle-acquisition path (`bridge/mt5_python`,
  `src/mt5/`). No MQL5/EA candle path exists in `src/` (grep verified).
- C++ owns the runtime and the deterministic decision chain.
- M15 is primary operational, H4 primary structural (bridge handshake declares
  both; API exposes all nine streams).
- SHADOW is the only mode; `GuardianPolicy::allowLiveExecution` is fixed false
  and `Guardian` denies `live.execute`.

### 3. Bridge (§B)
- Loopback-only enforced in `HttpClient` (refuses non-loopback hosts) and the
  bridge binds 127.0.0.1 only.
- Versioned handshake + schema check implemented; mismatch yields structured
  errors.
- Nine timeframes; closed-bar default (`closed_only=true`).
- Health/freshness observable; MT5-unavailable reported as a structured error,
  never fabricated candles.
- **Limitation:** no MetaTrader5 package or broker terminal in this environment,
  so real XAUUSD candle retrieval is not exercised or claimed.

### 4. Data integrity (§C)
- OHLC invariants, monotonicity, duplicate/out-of-order/incomplete/future-date
  detection in `DataValidator`; tests in `ClosedBarCausalityTests`.
- `BarFinalizer` guarantees the forming bar is never decision-grade.
- Quality gates propagate (`isDecisionGrade`); UNKNOWN/STALE are never treated as
  fresh or safe.

### 5. Runtime/resilience (§D)
- Paths resolve from the executable, not CWD (`PathResolver`); test proves
  CWD-independence.
- Bridge launch is app-managed (`StartupCoordinator` + `ProcessSupervisor`); the
  operator never runs Python/CMD.
- Impact resolution blocks hard dependents and degrades soft dependents.
- Restart budget is bounded (`RestartPolicy`); graceful shutdown implemented.

### 6. Decision/shadow (§E)
- Deterministic decision identity (timeframe + closed-bar open + direction);
  duplicate identities rejected by the ledger.
- The live runtime now actually runs the decision chain on every new closed M15
  bar via `DecisionPipeline` (`src/runtime/DecisionPipeline.{h,cpp}`): features
  -> structure -> regime -> eligibility -> signal -> score -> confidence ->
  probability -> macro/market-quality -> risk -> portfolio -> shadow execution
  -> position -> persistence -> outcomes. The chain is the same set of engines
  used by replay, so live and replay decisions are structurally identical.
- The decision is recorded in the ledger and durable store *before* a shadow
  command is issued; overlapping bridge windows are deduplicated per timeframe.
- Score (0..100 ranking) and confidence (0..1 meta-measure) are separate;
  probability is explicitly `null`/uncalibrated.
- Risk precedes shadow execution; portfolio limits applied; shadow lifecycle
  persisted; reconciliation implemented.

### 7. Persistence/audit (§F)
- File-backed store survives restart; writes idempotent; streams append-only.
- Hash-chained audit records; versions captured in record metadata.

### 8. Research/evolution/governance (§G)
- Evaluator is independent from candidate/research code; holdout/OOS/walk-forward
  validators present; approval gate exists for promotion; rollback/known-good
  concepts persisted.

### 9. Packaging (§H)
- Deterministic relative paths; actionable missing-dependency errors; bundled
  interpreter with system fallback disabled in the release configuration.
- **Limitation:** the Windows double-click launch and a packaged installer were
  not executed in this environment.

### 10. Evidence (§I)
- 11/11 CTest executables pass; build is warning-clean under `-Wall -Wextra`.
- The backend host was started for real (Linux): paths resolved, bridge launched
  as a supervised child and reached `ONLINE`, handshake succeeded, and the
  facade served `/api/v1/system/state`. Timeframes reported explicit bridge
  errors because MetaTrader5 is absent.
- See `TEST_LOG.md` for the per-test evidence and the explicit NOT RUN items.

## Defects found and fixed during review
1. `BackendFacade` emitted booleans/numbers as JSON strings — wire contract
   invalid. Fixed by marking pre-encoded values `raw`.
2. POSIX `ProcessSupervisor` reported a failed `exec` as `ONLINE` — fixed with a
   close-on-exec self-pipe that surfaces pre-exec failure as `ERROR`.
3. `BarFinalizer` rejected epoch-0 boundary bars — fixed to reject only negative
   open times.
4. No factory to construct the process-wide Guardian — added `makeGuardian()`.
5. The runtime ingested and published bars but never ran the decision chain, and
   re-published the bridge's overlapping window as duplicate history. Fixed by
   adding `DecisionPipeline` and per-timeframe publish dedup.
6. `PersistenceEngine` wrote position doubles at default precision, so a
   write/read round-trip did not compare equal and reconciliation reported a
   spurious FIELD_MISMATCH. Fixed with `std::setprecision(17)`.

## Explicitly not claimed
- Profitability, calibrated probability, broker validation, live-trading safety,
  Windows execution evidence, or real MT5 data retrieval.
