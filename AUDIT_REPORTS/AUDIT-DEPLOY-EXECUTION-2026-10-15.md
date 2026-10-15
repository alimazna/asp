# AUDIT — ASTRA Deployment Execution Status (2026-10-15)

> Companion to `AUDIT-DEPLOY-2026-10-15-FULL-AUDIT.md`.
> Status of implementation + verification after the Phase 0–7 execution.
> Evidence basis: real builds, real test runs, real Docker container, real
> HTTP requests through the work-host public HTTPS endpoints.

## 1. Delivered components

| Component | Path | Status |
|---|---|---|
| Linux backend (`astra_server`) | `src/api/AstraServerMain.cpp`, `AstraApiServer.*` | BUILT + TESTED in container |
| Candle persistence (SQLite, unique constraint) | `src/persistence/CandleStore.{h,cpp}` | TESTED (dedup survives restart) |
| Auth + sender token | `AstraServerMain` (`ASTRA_SENDER_TOKEN`) | TESTED (401 without, 200 with) |
| CORS allowlist | `AstraServerMain` (`ASTRA_ALLOWED_ORIGINS`) | TESTED (allowed 200, denied 403) |
| Windows sender | `windows-sender/` (7 modules + spec) | CODE + 17 unit tests pass |
| Web frontend | `astra-frontend/` (static SPA) | BUILT; smoke-tested through work host |
| Dockerfile + Railway config | `Dockerfile`, `railway.toml` | BUILT + container-tested |
| CI Windows sender build | `.github/workflows/sender-windows-build.yml` | Added (runs on tag/manual) |

## 2. Verification evidence

### Backend (host + Docker)

- Host build: `cmake --build build-linux -j$(nproc)` → OK.
- CTest: **25/25 pass** (`ctest --output-on-failure`).
- Docker build `sudo docker build -t astra-server:test .` → OK.
- Container run: `PORT=8080`, `ASTRA_SENDER_TOKEN`, `ASTRA_ALLOWED_ORIGINS`
  set; started with `./astra_server --host=0.0.0.0`.
  - `GET /api/v1/health` → 200 `{"aggregate":"STARTING","decision_grade_data":false,...}`
  - Unauthenticated ingest POST → **401**
  - Authenticated ingest POST → `"accepted":[{"identity":"symbol=XAUUSD&timeframe=M15&open_time=1792045800","state":"stored_new"}],"ingested":1`
  - Duplicate POST (same identity) → `"duplicates":[{"..."state":"already_stored"}],"ingested":0`
  - `docker restart` → the candle is still present (`GET /api/v1/candles?tf=M15`).
  - `GET /api/v1/sync/latest` → authoritative watermarks.
  - CORS: `Origin: https://example.pages.dev` → 200; disallowed origin → 403.
- Logs confirm `api bound: http://0.0.0.0:8080/api/v1 (public)` and data dir.

### Frontend

- `astra-frontend/build.js` with `BACKEND_BASE` pointing at the work-host
  public HTTPS URL → builds `dist/`.
- Served via `python -m http.server`, browser loaded it; the app displayed
  REAL backend values: server mode DEGRADED, decision grade degraded,
  candles (XAUUSD M15 latest 06:30 UTC), bridge state OFFLINE, and honest
  "not available"/"—" for missing analysis context. CORS through the public
  proxy passed.

### Windows sender

- Unit tests (Linux, against `tests/integration/fake_mt5` + a loopback stub
  server): **17/17 pass** in `windows-sender/`.
  - nine-timeframe mappings; closed-candle-only reads (index 0 forming bar
    never returned); OHLC validation; terminal-unavailable honesty.
  - ledger durability across close/reopen; idempotent restart (no resend);
    uncertain-delivery retry reuses the same identity; bounded chronological
    catch-up; failed vs pending vs acked counts.

## 3. What is NOT VERIFIED (blockers)

1. **Railway deployment** — NO Railway credentials are available in this
   environment. The Docker image builds and the container behaves correctly,
   but no Railway project/service was created. BLOCKED on credentials.
2. **Cloudflare Pages deployment** — NO Cloudflare credentials available. The
   static frontend builds and passes an in-browser smoke test, but no Pages
   project was created. BLOCKED on credentials.
3. **Windows .exe artifact** — **DONE**. The GitHub Actions workflow
   *Build Windows Astra Sender* ran on a `windows-latest` runner from tag
   `sender-v0.3.0`: unit tests passed on Windows, the single-file exe was
   built, the `--version` smoke-run passed, and the artifact
   `AstraSender-exe` (11,721,635 bytes) was uploaded. A GitHub Release
   `https://github.com/alimazna/asp/releases/tag/sender-v0.3.0` carries the
   asset `AstraSender.exe` (11,985,360 bytes). The exe is a valid PE (MZ/PE,
   subsystem 2 GUI, PyInstaller, Python 3.11).
4. **Live MT5 terminal integration** — no Windows PC with a logged-in MT5
   terminal is reachable; sender logic is tested against the fake MT5 feed only.
5. **Railway persistent volume** — `railway.toml` declares the `astra-data`
   volume mount; actual provisioned durability is NOT VERIFIED until a Railway
   service exists.

## 4. Configuration reference

Secrets and origins are supplied via environment / secret store, never by
source control or the frontend bundle:

- `ASTRA_SENDER_TOKEN` — dedicated ingestion credential (backend).
- `ASTRA_ALLOWED_ORIGINS` — comma-separated browser origins.
- `PORT` — Railway assigned port; `--port` overrides.
- `ASTRA_DATA_DIR` — persistent SQLite directory (volume mount;
  defaults `~/.local/share/astra/data` locally, `/opt/astra/data` in Docker).
- Frontend `BACKEND_BASE` — public Railway HTTPS base URL (build-time env).
- Sender `config.json` — `backend_url` + `sender_token` + `sender_id`
  (user's Windows PC, ACL protected, never committed).