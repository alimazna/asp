# PROJECT_STATE.md

```yaml
project: AURA
product: ASTRA
asset: XAUUSD
mode: SHADOW
backend_agent: DeepSeek v4.1 Flash / OpenHands
frontend_agent: Alpha / deferred
architecture_status: DEFINED_V4
backend_status: BACKEND_REVIEW_PASS
frontend_status: DEFERRED
market_data_path: Python_MetaTrader5_Bridge
bridge_distribution: BUNDLED_WITH_APP
bridge_process: SEPARATE_CHILD_PROCESS
bridge_transport: LOOPBACK_JSON_API
startup_requirement: DOUBLE_CLICK_NO_CMD
live_automation: NOT_INITIAL_OBJECTIVE
profitability: UNPROVEN
probability_calibration: NOT_ESTABLISHED
broker_validation: REQUIRED
```

## Current checkpoint
The complete backend task graph is implemented: 222/222 manifest output paths
exist on disk and build as one static core library (`aura_core`) plus the
`aura_backend_host` entry point. Eleven deterministic test executables are wired
into CTest and pass. The runtime now runs the full live decision chain per new
closed M15 bar (`src/runtime/DecisionPipeline.{h,cpp}`) through to shadow
execution, positions, persistence, and outcomes, with per-timeframe
closed-bar dedup. The backend host was started for real: paths resolved, the
Python bridge launched as a supervised child and reached `ONLINE`, the handshake
succeeded, and the v1 facade served `system/state`. A full backend review
against the control-plane documents was performed (see
`project-control/BACKEND_REVIEW.md`); the gate is **PASS** with the documented
evidence limitations.

## Frontend integration map
The final frontend integration map has been generated:
`project-control/FRONTEND_INTEGRATION_MAP.md` (documentation only). It records
the implemented v1 API surface (11 GET routes + 2 allow-listed commands), the
system/service/health states, market-data and timeframe-authority model, and
nine documentation-vs-implementation discrepancies (D1–D9), each marked
`BACKEND DATA NOT EXPOSED` where the backend does not emit the documented
field. Notably, no concrete frontend↔backend transport is implemented yet
(D1); this must be resolved before Alpha codes against a transport.

## Scope boundary
- Backend only. No ASTRA frontend/UI work has begun; Alpha remains deferred.
- SHADOW is the only execution mode. Live order placement does not exist in the
  codebase and the Guardian structurally denies it.

## Completion ownership
- DeepSeek: implemented the backend.
- Review gate: performed against the control-plane docs (BACKEND_REVIEW.md).
- Alpha: implement frontend only after this gate; see `project-control/HANDOFF.md`.

## Known evidence limitations (not blockers)
- Built and tested on Linux with GCC; the Windows toolchain path is compiled but
  not executed here.
- The bundled Python bridge is exercised for real over loopback; MetaTrader5 and
  a real broker terminal are not present, so live MT5 candle retrieval is not
  claimed.
