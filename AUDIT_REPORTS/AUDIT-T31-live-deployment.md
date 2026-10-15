# AUDIT — T31 Live Production Deployment (Railway + Cloudflare Pages + Windows Sender)

Status: **deployed and verified live** (2026-10-15)
Author: deployment agent (autonomous)
Scope: Phase Zero audit through Phase Seven live deployment of the ASTRA stack.

This report records the **actual, evidence-backed** production deployment. Every
"VERIFIED" line below was reproduced from this environment; anything not
reproducible here is marked **NOT VERIFIED** explicitly.

---

## 1. Repository audit — findings

| Area | Finding |
| --- | --- |
| C++ backend | `src/` builds a single `astra_server` executable (CMake, 96 targets) plus `libaura_core.a`. The AURA engine, decision pipeline, `CandleStore`, `AstraApiServer`, and `BackendFacade` are all cross-platform C++17. |
| Windows-only code | Only `bridge/mt5_python/*` (via the `MetaTrader5` PyPI package) is Windows-bound. It is **not** required by the server runtime; it belongs to the Windows sender. |
| API bind | `AstraServerMain.cpp` reads `ASTRA_HOST` (default `0.0.0.0`), `PORT` (default `8080`), `ASTRA_DATA_DIR` (default `./data`), `ASTRA_SENDER_TOKEN`, `ASTRA_ALLOWED_ORIGINS`. Loopback-only binding is a **local-dev default**, overridden for the container via `ASTRA_HOST=0.0.0.0`. |
| Persistence | `CandleStore` is SQLite. A container filesystem is ephemeral, so a Railway **volume** is required for durability. |
| Existing routes | `/api/v1/health`, `/analysis/latest`, `/context/latest`, `/system/state`, `/bridge/status`, `/candles`, `/candles/ingest` (auth), `/sync/latest` (auth). |
| Frontend | `astra-frontend/` is **not** a framework app — it is a static site with a Node `build.js` that emits `dist/` (`index.html`, `assets/app.css`, `assets/app.js`, generated `config.js`). No `package.json`. |
| Windows sender | `windows-sender/` already existed from a prior session with a full implementation + 17 tests and a PyInstaller spec. |
| Prior blockers | Earlier audit (`AUDIT-T30`) recorded the Railway CLI "Multiple services found" contradiction and no service existing. Root-caused this session (see §4). |

### What was broken / blocking
1. No Railway service existed, so `railway up` could not target anything.
2. No durable volume existed (would have lost candles on redeploy).
3. CORS allowlist was empty in the deployed environment.
4. The Windows build workflow's **release-attach** step failed with HTTP 403
   ("Resource not accessible by integration") even though the `.exe` built fine.

---

## 2. Architecture delivered

```
Windows PC (owner)                         Railway (Linux container)                 Cloudflare Pages
┌──────────────────────────┐   HTTPS    ┌──────────────────────────────┐          ┌────────────────────┐
│ AstraSender.exe (ttk GUI)│──────────▶ │ astra_server (C++17)         │ ◀──HTTPS─│ astra-dia.pages.dev│
│  • MetaTrader5 (read-only│  ingest    │  • AstraApiServer (v1)       │  read    │  • static dist/    │
│    closed XAUUSD candles)│  bearer    │  • AURA engine + pipeline    │          │  • config.js holds │
│  • SQLite ack ledger     │            │  • CandleStore (SQLite)      │          │    public backend  │
│  • 9 timeframes, dedup   │            │  • volume /opt/astra/data    │          │    base URL only   │
└──────────────────────────┘            └──────────────────────────────┘          └────────────────────┘
        │ outbound only                          ▲ durable volume
        └── no inbound port, no broker password ─┘
```

- The sender only makes **outbound HTTPS** calls; it never listens on a port and
  never receives broker passwords.
- The frontend holds **no secrets** — only the public backend base URL.

---

## 3. Deployment evidence (VERIFIED)

### Railway backend
- Project `romantic-warmth` (`bd2e8b02-9d95-4159-9891-c4d4f6914092`),
  environment `production` (`dc17b0a6-4e5a-47ea-bfb7-7f8e9404f8d5`).
- Service `astra-server` (`3a852ac0-ffdc-4eb3-9c5f-dc75affcde4f`), builder
  Dockerfile, healthcheck `/api/v1/health`.
- Volume `astra-server-volume` mounted at `/opt/astra/data` (durable).
- Public URL: **https://astra-server-production.up.railway.app**
- Linux build: 96/96 CMake targets compiled; `astra_server` linked; image pushed.
- `GET /api/v1/health` → **HTTP 200**:
  `{"api":"v1","schema":"1.0","data":{"aggregate":"STARTING","decision_grade_data":false,...}}`

### Persistence + idempotency (live)
- Ingest (auth) first send → `state: stored_new`, `ingested: 1`.
- Replay same identity → `state: already_stored`, `ingested: 0`, listed under `duplicates`.
- After `railway restart`, all previously ingested candles were still readable
  from `GET /api/v1/candles` → volume persistence **VERIFIED**.

### Auth + validation (live)
- No/invalid bearer token → **HTTP 401**.
- Future-dated candle → `rejected: "candle open_time is in the future"`, `ingested: 0`.

### CORS (live)
- `OPTIONS` preflight from `https://astra-dia.pages.dev` → **HTTP 204** with
  `access-control-allow-origin: https://astra-dia.pages.dev`.

### Cloudflare Pages frontend
- Project `astra`, production branch `main`.
- URL: **https://astra-dia.pages.dev** (deployment `f69ac39e.astra-dia.pages.dev`).
- Built with `BACKEND_BASE=https://astra-server-production.up.railway.app`.
- Real-browser smoke test rendered live data: "backend online",
  "XAUUSD · M15 · 3 closed bar(s)", "Real closed candles (read-only)",
  plus honest degraded states ("not available", "UNKNOWN").

### Windows sender
- Source: `windows-sender/` (`astra_sender/` package + tests + PyInstaller spec).
- Executable artifact (release asset, verified PE):
  **https://github.com/alimazna/asp/releases/download/sender-v0.3.0/AstraSender.exe**
  size 11,985,360 bytes, SHA256
  `f5f8639b6e20f98605a3334e61dd11ccbd9b7a3834d7638c1cf2dca5c96ae716`.
- Live E2E from this environment (real `SyncEngine`/`ServerClient`/`DeliveryLedger`
  against the deployed backend): `MT5=READY`, `sent=1`, `acked=1`, `dedup=5`,
  `failed=0`; the candle (`open_time=1792068300`) then appeared in
  `GET /api/v1/candles`.

---

## 4. Root cause of the earlier Railway blocker

`railway service list` returned `[]` and `railway up` reported "Multiple services
found". Reading the CLI source (`railwayapp/cli` v5.68.0,
`src/controllers/service.rs`) showed:
- an empty project service list means "backboard will generate one for us";
- a **non-empty** list with no `--service` (non-TTY) bails with "Multiple services found".

The `railway add` interactive wizard **did** create the `astra-server` service,
which then appeared in `railway status --json`. Subsequent commands must target
the service by **ID** (`railway volume` looks the service up by ID and panics on
a name). No account-level token is needed; a project token can deploy to a
pre-existing service, matching the Railway docs.

---

## 5. Costs and limits

- Railway: existing project/trial only; one service, one volume, no plan upgrade.
- Cloudflare Pages: free tier; one project, no paid resources.
- No billable database was provisioned — SQLite on a Railway volume is used.

## 6. Remaining items

- The Windows **release-attach** CI step fails with HTTP 403
  (`contents: write` not granted by the repo settings); the `.exe` still builds
  and uploads as a workflow artifact, and the `sender-v0.3.0` release asset
  exists. Fixing the 403 needs repo-settings permission, not code.
- Live MT5 terminal verification on the owner's Windows PC is **NOT VERIFIED**
  from this environment (no MT5 terminal available); the sender's MT5 path is
  covered by the repo's `fake_mt5` fixture and unit tests.
