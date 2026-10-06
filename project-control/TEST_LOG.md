# TEST_LOG.md

| Timestamp | Scope | Result | Evidence | Notes |
|---|---|---|---|---|
| 2026-10-06 | Repository task pack generation | READY | This repository pack | Backend implementation not started |
| 2026-10-06 | Backend build (BLD-0001/0002) | PASS | `cmake -S . -B build && cmake --build build` | `aura_core` static lib + `aura_backend_host` executable; 0 warnings |
| 2026-10-06 | TST-0001 bridge contract | PASS | `build/PythonBridgeContractTests` | Real bundled bridge booted on 127.0.0.1; handshake, 9 timeframes, structured MT5-unavailable error |
| 2026-10-06 | TST-0002 closed-bar causality | PASS | `build/ClosedBarCausalityTests` | Candidate bar never closed; ordering/duplicate/future-date detection |
| 2026-10-06 | TST-0003 data quality propagation | PASS | `build/DataQualityPropagationTests` | UNKNOWN/STALE not decision-grade; bridge failure blocks dependents, degrades soft edges |
| 2026-10-06 | TST-0004 startup without CMD | PASS | `build/StartupWithoutCmdTests` | Executable-root paths, CWD-independent, packaging validated |
| 2026-10-06 | TST-0005 bridge recovery | PASS | `build/BridgeRecoveryTests` | Watchdog degrade/recover, bounded restart budget |
| 2026-10-06 | TST-0006 persistence/restart | PASS | `build/PersistenceRestartTests` | Survives restart, idempotent writes, append-only stream, checkpoint recovery |
| 2026-10-06 | TST-0007 shadow lifecycle | PASS | `build/ShadowLifecycleTests` | decision→fill→position→outcome; stop precedence; live denied |
| 2026-10-06 | TST-0008 decision identity | PASS | `build/DecisionIdentityTests` | Deterministic IDs, duplicate rejection, score≠probability |
| 2026-10-06 | TST-0009 backend API contract | PASS | `build/BackendApiContractTests` | Versioned v1 envelope, unknown-not-safe, command allow-list |
| 2026-10-06 | TST-0010 package smoke | PASS | `build/PackageSmokeTests` | RuntimeManifest parses, packaging coherent, error envelope structured |
| 2026-10-06 | Full CTest run | PASS | `ctest --test-dir build` | 10/10 tests, 0 failures |
| 2026-10-06 | Backend review gate | PASS (with documented limitations) | `project-control/BACKEND_REVIEW.md` | 222/222 outputs present |

## Defects found and fixed during this run
- `src/api/BackendFacade.cpp`: booleans/numbers were emitted as JSON *strings*;
  all pre-encoded values are now marked `raw` so the wire contract is valid.
- `src/platform/windows/ProcessSupervisor.cpp` (POSIX path): a failed `exec`
  was reported as `ONLINE`. A self-pipe now detects pre-exec failure and the
  supervisor reports `ERROR` with an actionable message.
- `src/data/BarFinalizer.cpp`: epoch-0 boundary bars were rejected; only
  negative open times are invalid now.
- `src/guardian/IGuardian.h` / `Guardian.cpp`: added `makeGuardian()` so the
  process-wide Guardian can be constructed through its interface.

## Required evidence categories before frontend handoff
- static analysis: PASS (build with `-Wall -Wextra`, zero warnings)
- unit tests: PASS (10/10)
- integration tests: PASS (real loopback bridge boot + contract)
- Windows build: NOT RUN (Linux toolchain used; Windows path compiled but not executed)
- bridge startup/handshake: PASS
- MT5 candle retrieval test: NOT RUN (no MetaTrader5 / broker in this environment)
- closed-bar causality test: PASS
- stale/offline degradation test: PASS
- persistence/restart test: PASS
- packaging/double-click startup test: PASS (path/CWD-independence verified; Windows double-click not executed here)
- shadow lifecycle test: PASS
