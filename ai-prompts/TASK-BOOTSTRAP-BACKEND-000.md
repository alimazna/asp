# TASK-BOOTSTRAP-BACKEND-000 — Build the complete AURA backend

You are the **backend implementation agent** for AURA, running as **DeepSeek v4.1 Flash inside OpenHands**.

## Read first, in this exact order
1. `project-control/AI_BOOTSTRAP.md`
2. `project-control/GLOBAL_AI_CODING_RULES.md`
3. `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`
4. `project-control/IMPLEMENTATION_SCOPE.md`
5. `project-control/PROJECT_STATE.md`
6. `project-control/TASK_MANIFEST.yaml`
7. `project-control/DECISIONS.md`
8. `project-control/BLOCKED.md`
9. `project-control/HANDOFF.md`
10. `project-control/TEST_LOG.md`
11. `docs/architecture/MT5_PYTHON_BRIDGE_V1.md`
12. `docs/architecture/BACKEND_FRONTEND_API_V1.md`
13. `docs/architecture/BACKEND_DONE_DEFINITION.md`
14. `docs/brand/ASTRA_VISUAL_IDENTITY.md`

The archived V3 master and old Python guide are source references only. The V4 document is the active authority for the new build.

## Primary objective
Implement **all backend tasks** in `project-control/TASK_MANIFEST.yaml`, in dependency order, until the backend completion gate is satisfied.

Do not stop after one phase merely because the first build passes. Continue through every backend phase that is marked ACTIVE in the manifest. The task manifest is the implementation graph. Use its exact output paths and dependencies.

## Critical architecture decisions

### 1. Python is now the market-data acquisition boundary
Use the official Python MetaTrader5 package to communicate with the MT5 Terminal running on the user's Windows machine. Do not build or reintroduce MQL5 EAs/adapters for candle acquisition.

Target flow:

```text
Broker / Valetax
    -> MT5 Terminal
    -> bundled Python MetaTrader5 Bridge process
    -> loopback-only local API (127.0.0.1)
    -> AURA C++ ingestion
    -> validation / timeframe state / decision chain
```

### 2. Python is bundled with the application
The end-user must not be required to launch `python`, run a script, or use CMD manually. Prefer a private application-bundled Python runtime and dependencies. Do not make a global system Python installation a runtime prerequisite of the packaged product.

### 3. Python remains a separate child process
It is part of the application distribution, but it is not compiled into the C++ executable. AURA owns its lifecycle:
- resolve bundle paths from the executable/install root;
- launch the bridge automatically;
- perform a version/protocol handshake;
- monitor health;
- restart only under explicit bounded policy;
- surface OFFLINE/DEGRADED states;
- stop the child cleanly on shutdown.

### 4. Localhost only
The bridge must bind only to loopback. It must never listen on `0.0.0.0` or expose market data externally.

The V1 implementation contract uses a JSON-over-localhost HTTP-style API with a configurable loopback port. Keep the transport behind `PythonBridgeClient` so it can be replaced later without changing the higher-level data contracts.

### 5. Closed bars and causality
For decision-grade candle data:
- closed bars only;
- no future information;
- no lookahead;
- no repainting;
- retain event/receive/processing timestamps where applicable;
- preserve broker/source identity;
- propagate data quality into capability/decision gating.

### 6. Nine timeframe streams remain canonical
M1, M5, M15, M30, H1, H4, D1, W1, MN1.
M15 remains the primary operational/setup timeframe and H4 remains the primary structural authority.

### 7. SHADOW only
The backend may implement a structurally comparable execution simulator and complete shadow lifecycle, but it must not place broker orders in the initial build.

### 8. No silent architecture changes
When a required contract is missing or two authoritative sources conflict without resolution:
- mark `BLOCKED`;
- identify the exact dependency/decision;
- do not invent a substitute;
- do not silently redesign the architecture.

## Execution method
For every implementation task:
1. Read the listed required dependencies.
2. Implement exactly the requested source file.
3. Run the task-specific deterministic checks.
4. Integrate it into the build.
5. Update the control-plane state/logs.
6. Commit coherent checkpoints.
7. Continue to the next unblocked task.

Respect the project rule:

```text
1 TASK = 1 PROMPT = 1 OUTPUT PATH = 1 SOURCE FILE
```

The bootstrap prompt itself is a coordination task, not a source-file implementation task. When implementing source code, maintain the one-file rule.

## Required checkpoints
Commit after each major wave at minimum:
- Foundation
- Resilience
- Python MT5 Bridge
- Data/Runtime
- Decision Chain
- Shadow/Outcome
- Research/Evolution
- Validation/Governance/Recovery
- Packaging/Backend API

Use clear commit messages and update `PROJECT_STATE.md`, `HANDOFF.md`, and `TEST_LOG.md` after meaningful checkpoints.

## Frontend boundary
Do not implement Alpha's UI. Do not create the final ASTRA UI screens. Backend work may expose stable data/status contracts and headless runtime APIs required by the future frontend.

## Final response when the backend is complete
Report:
- implemented task count by phase;
- remaining BLOCKED tasks, if any;
- build commands and exact results;
- test summary;
- package/runtime-startup result;
- bridge health/handshake result;
- evidence that double-click startup no longer depends on CMD;
- final backend commit(s);
- the exact frontend handoff state for Alpha.

Do not claim profitability, calibrated probability, broker validation, or live safety unless there is actual evidence in the repository/test log.
