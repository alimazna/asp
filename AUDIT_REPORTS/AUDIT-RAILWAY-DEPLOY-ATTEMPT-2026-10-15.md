# AUDIT — Railway Deployment Attempt & Full Local Deployment Simulation (2026-10-15)

> Companion to `AUDIT-DEPLOY-EXECUTION-2026-10-15.md`.
> Evidence from a fresh Railway-CLI deployment attempt and a complete local
> simulation of the target Railway runtime environment.

## 1. Token search

Exhaustively searched for a Railway credential before attempting deployment:

- Project root and subdirectories for `*.txt`, `*.env`, `secrets` — only
  `requirements.txt`, `CMakeLists.txt`, `.env.example`, build artifacts, and
  the authored `railway.toml` (no token values).
- Grep for `RAILWAY_TOKEN`, `RAILWAY`, `rw_` across the repo, the workspace
  (`/workspace`), conversation event history, home directory, and system env.
- Result: **no Railway token present anywhere.** The only available credentials
  are `GITHUB_TOKEN` and `OPENHANDS_API_KEY` (system-managed).
- The project `.env.example` carries placeholder names only — no values.

## 2. Railway CLI auth check

- Installed Railway CLI (`@railway/cli`) as root.
- `railway whoami` → `Unauthorized. Please login with "railway login"`
- `railway status` → `Unauthorized. Please login with "railway login"`
- Conclusion: **no valid Railway credential exists to deploy with.** Per the
  task instructions (stop and notify on invalid/absent token, then fall through
  to local testing), a live Railway deployment could not be performed.

## 3. Full local build + test suite (deployment-environment fallback)

All commands run in a clean shell against a fresh checkout state.

### Backend C++ (matching `cmake -S . -B build && cmake --build build && ctest`)

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` → OK
  (required `libsqlite3-dev`; installed via `apt`).
- `cmake --build build -j$(nproc)` → **build OK**, `astra_server` produced.
- `ctest --test-dir build --output-on-failure` → **25/25 tests passed.**

### Windows sender (matching `python -m unittest discover -s windows-sender/tests -v`)

- Run from `windows-sender/` (module-under-test on `sys.path`).
- → **17/17 tests passed** (ledger identity/ack/failed/pending, collector,
  protocol, engine, build config).

### Frontend (dependency-free static SPA; `npm ci` does not apply)

- No `package.json`: `astro-frontend/` is a static SPA built with
  `node build.js` + `BACKEND_BASE` env var.
- `BACKEND_BASE=https://<railway>.up.railway.app node build.js` → **dist/ OK**
  (index.html, config.js, assets/app.js, assets/app.css).

## 4. Local server = simulated Railway runtime

Started `build/astra_server` with Railway-style environment:

```
PORT=12001 ASTRA_HOST=0.0.0.0 ASTRA_SENDER_TOKEN=test-sender-token-12345
ASTRA_DATA_DIR=/tmp/astra-sim-data
ASTRA_ALLOWED_ORIGINS=https://astra.pages.dev,https://astra.example.com
```

Startup log:
```
astra_server starting
  candle store: /tmp/astra-sim-data/candles.sqlite
  runtime stage: DEGRADED (bridge script not found ...)
  api bound: http://0.0.0.0:12001/api/v1 (public)
```

### Endpoint matrix (all via curl against the live local process)

| # | Test | Result |
|---|---|---|
| 1 | `GET /api/v1/health` | 200 health envelope, `aggregate:STARTING`, honest `decision_grade_data:false` |
| 2 | `POST /api/v1/candles/ingest` (no auth) | **401** |
| 3 | `POST /api/v1/candles/ingest` (wrong Bearer) | **401** |
| 4 | `POST /api/v1/candles/ingest` (valid token) | accepted → `stored_new`, `ingested:1` |
| 5 | Same candle again | `already_stored` duplicate; `ingested:0` |
| 6 | `GET /api/v1/sync/latest` (valid token) | authoritative watermark `{symbol:"XAUUSD",timeframe:"M15",last_open_time_utc:...}` |
| 7 | `GET /api/v1/sync/latest` (no auth) | **401** |
| 8 | `GET /api/v1/candles?tf=M15&limit=5` | persisted candle returned, `closed_only:true` |
| 9 | CORS allowed origin (`https://astra.pages.dev`) | 200 + `Access-Control-Allow-Origin` echoed |
| 10 | CORS disallowed origin (`https://evil.example.com`) | **403** |
| 11 | Malformed candle (negative `open`) | **rejected** with reason, `ingested:0` |
| 12 | Kill + restart server (same `ASTRA_DATA_DIR`) | candle **survives restart** (SQLite durability) |

### Sender protocol E2E (real `ServerClient` code path)

Ran `astra_sender.protocol.ServerClient` against the live local server:
- Ingest accepted; replay → `already_stored` (idempotency).
- `sync_latest()` returns the authoritative watermark set.
- Wrong token → `ProtocolError(code="unauthorized")` (sender treats it as
  uncertain/rejected and keeps the delivery pending — correct).

### Frontend integration (browser)

- Built `dist/` pointing at `http://127.0.0.1:12001`; served on port 8765.
- Without the local origin in the CORS allowlist → frontend truthfully showed
  `backend unavailable` (**strict CORS, no localhost shortcuts**).
- After adding `http://127.0.0.1:8765` to `ASTRA_ALLOWED_ORIGINS`:
  - `backend online`, server mode `DEGRADED` (honest shadow-only),
  - real candle table: `XAUUSD · M15 · 1 closed bar(s) … Latest: 2026-10-15 09:15:00 UTC`,
  - analysis panel `NONE` / `not available` / `UNKNOWN` — truthful empty states,
    no fabricated results,
  - transport `bridge OFFLINE` (no MT5 on the server).

## 5. Result summary

| Item | Status |
|---|---|
| Railway token on disk / env | **NOT FOUND** |
| `railway whoami` | Unauthorized → **deployment blocked on credentials** |
| Backend C++ build | OK |
| Backend tests | **25/25 pass** |
| Sender tests | **17/17 pass** |
| Frontend build | OK (`node build.js`) |
| Simulated Railway runtime endpoints | all pass (auth/CORS/ingest/dedup/sync/persistence/restart) |
| Sender protocol E2E vs live server | pass |
| Frontend ↔ backend in browser | pass (real candle data rendered; honest empty states) |

## 6. Blockers

1. **Railway credential absent** — no `RAILWAY_TOKEN` (or CLI login) exists in
   this environment. Required before any live `railway link` / `railway up`.
   Deployment commands remain ready in the repo (`railway.toml`, `Dockerfile`).
2. Live MT5 terminal / Windows host — not reachable from this environment;
   sender logic is covered by the fake-feed unit tests only.

## 7. To deploy when credentials are available

```bash
export RAILWAY_TOKEN=...            # from the owner, never committed
railway whoami                       # must say "Logged in"
railway link --project determined-warmth --environment production   # or by prompt
railway up                           # builds Dockerfile, starts astra_server
railway variables set ASTRA_SENDER_TOKEN="..." ASTRA_ALLOWED_ORIGINS="https://<pages>.pages.dev"
# then verify https://<railway>.up.railway.app/api/v1/health
```