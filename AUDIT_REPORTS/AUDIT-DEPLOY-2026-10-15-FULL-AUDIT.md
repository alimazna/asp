# AUDIT — ASTRA Full Autonomous Deployment — Initial Repository Audit

> Date: 2026-10-15
> Scope: complete repository audit for the ASTRA deployment mission
> Evidence basis: source inspection + real builds and test runs on Linux

## 1. What exists

| Component | Path | State |
|---|---|---|
| C++ backend core | `src/` (`aura_core` static lib) | builds on Linux (GCC 14), 23/23 CTest green |
| Backend host (loopback) | `src/platform/windows/AuraBackendHost.cpp` | Windows/loopback-oriented desktop host |
| Python MT5 bridge | `bridge/mt5_python/` | loopback HTTP service on 127.0.0.1:8791; standard-library-only, optional MetaTrader5 |
| Qt 6 desktop frontend | `frontend/qt/` | Windows-only Qt C++ app (the only existing frontend) |
| Persistence | `src/persistence/FilePersistenceStore` | file-backed key/value store under `~/.local/share/astra/data` |
| Fake MT5 test harness | `tests/integration/fake_mt5/MetaTrader5.py` | reusable fake for MT5-dependent code |
| Packaging | `packaging/` (PyInstaller spec, Inno Setup) | frozen `bridge.exe` + installer |
| CI | `.github/workflows/build-windows.yml` | Windows Qt/backend/bridge/installer build |

## 2. What is broken / missing for the mission

1. **No web frontend.** The repository contains NO `astra-frontend/` directory and no
   web app at all. The only frontend is a Qt 6 desktop app (`frontend/qt_`). A
   Cloudflare Pages web frontend must be built.
2. **No remote-facing API server.** `aura::LoopbackApiServer` refuses to bind anything
   other than 127.0.0.1 (`isLoopbackHost` check) — correct for the desktop model,
   impossible for a Railway public HTTPS service.
3. **No ingestion route.** The frozen v1 API surface is read-only GET routes plus
   `POST /api/v1/command` (allow-listed notify/request_approval). There is no route
   for a remote Windows sender to submit closed candles.
4. **No sender credential / authentication.** The loopback model is trusted-by-design;
   there is no bearer-token or key model for a remote data sender.
5. **No Windows closed-candle sender.** The bridge *serves* candles over loopback to the
   local backend; nothing reads closed candles and *pushes* them securely to a remote
   server, nor does any local acknowledgment ledger (SQLite) exist.
6. **Persistence is not a production database.** `FilePersistenceStore` is durable at
   the record boundary on a local filesystem but has no uniqueness constraints, no SQL,
   and cannot serve as the authoritative idempotent candle store on Railway (the
   container filesystem is ephemeral unless a volume is mounted).
7. **No Dockerfile / Railway config / Cloudflare config / build-deploy scripts** exist.
8. **No live-loopback seam between HTTP ingestion and the analysis runtime.** The
   runtime (`AuraRuntime::tick`) is fed exclusively by the local bridge client.

## 3. Verified build evidence (Linux container)

- `cmake .. && cmake --build . -j` → succeeds; `aura_backend_host` links.
- `ctest` → **23/23 tests pass** (AnalysisApi, BackendApiContract, BridgeRecovery,
  CandlesApi, ClosedBarCausality, DataQuality, DecisionIdentity, DecisionPipeline,
  FrontendContractD1D9, JsonParser, LoopbackConcurrency, PackageSmoke,
  PersistenceRestart, ProbabilityApi, PythonBridgeContract, ShadowLifecycle,
  SingleInstance, StartupEarlyBind, StartupWithoutCmd, + 4 feature tests).

## 4. API contract audit

- `docs/architecture/API_V1_SCHEMA.json` is **authoritative** and matches the
  implementation (`BackendFacade`, `BackendApiSchema`). Every response enveloped as
  `{api:"v1",schema:"1.0",data:...}`; errors flat `{error,code,message}`.
- Existing routes (v1): system/state, health, health/v1, timeframes,
  timeframes/{tf}/snapshot, candles, signals/latest, probability/latest, risk/latest,
  shadow/positions, shadow/outcomes, research/status, governance/status,
  bridge/status, audit/recent, analysis/latest, analysis/history, context/latest,
  POST command.
- Missing routes for the mission: candle **ingestion**, sender **sync/reconcile**
  status. Both are additive to v1.

## 5. Platform constraints

- MetaTrader5 Python package: Windows-only. Must not be installed on Railway.
- `bridge/mt5_python` itself is stdlib-only and runs on Linux; it reports
  `MT5_TERMINAL_UNAVAILABLE` honestly when the package/terminal is absent.
- The C++ core builds on Linux and is platform-neutral except for
  `src/platform/windows/*` which use portable code paths (`#ifdef`).

## 6. Deployment blockers at audit time

- **No Railway credentials** in the environment.
- **No Cloudflare credentials** in the environment.
- Work-host public HTTPS endpoints exist for this agent environment's local ports
  12000/12001 and are usable for end-to-end smoke verification in lieu of a real
  Railway/Cloudflare deployment.

## 7. Architecture decisions for this mission

1. **Linux backend service (`astra_server`)** — new C++ executable built from
   `aura_core` that binds `0.0.0.0:$PORT` (default 8080, Railway-style), requires a
   sender bearer token, serves the existing v1 API plus new additive ingestion and
   sync routes, and persists accepted candles in a durable **SQLite** store
   (`CandleStore`) with a uniqueness constraint on `(source,symbol,timeframe,open_time)`.
   Accepted bars are fed into the existing `AuraRuntime` data path so the existing
   AURA decision pipeline runs unchanged.
2. **Windows sender (`windows-sender/`)** — Python app: reads closed XAUUSD candles
   from the logged-in MT5 terminal (via the existing `bridge/mt5_python` client
   logic), keeps a durable SQLite acknowledgment ledger, sends idempotent candle
   batches over authenticated HTTPS, retries with exponential backoff/jitter, and
   renders a lightweight ttk GUI. Packaged with PyInstaller into a single `.exe`.
3. **Web frontend (`astra-frontend/`)** — a dependency-light static SPA that consumes
   the deployed backend's HTTPS API, with honest loading/stale/unavailable/error states
   and responsive layout for Cloudflare Pages.
4. **Deployment** — Dockerfile + Railway config + Cloudflare Pages build notes are
   authored; actual Railway/Cloudflare provisioning is **NOT VERIFIED** because no
   credentials are available in this environment. A real end-to-end smoke test is
   performed through the work-host HTTPS endpoints instead.

## 8. Files examined (primary)

CMakeLists.txt, src/api/{BackendFacade,BackendApiSchema,AnalysisApi,LoopbackApiServer}.
{h,cpp}, src/runtime/AuraRuntime.{h,cpp}, src/runtime/DecisionPipeline.{h,cpp},
src/mt5/PythonBridgeClient.{h,cpp}, src/mt5/Mt5BridgeContract.h,
src/persistence/*, src/data/*, src/platform/windows/{AuraBackendHost,PathResolver,
StartupCoordinator,BundleLocator,ProcessSupervisor,StartupCoordinator}.*,
bridge/mt5_python/*, frontend/qt/**, packaging/**, tests/**, docs/architecture/*,
project-control/*, .github/workflows/build-windows.yml.