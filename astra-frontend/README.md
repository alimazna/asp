# ASTRA — XAUUSD Market Intelligence Terminal (web frontend)

A standalone, responsive web frontend for the existing **AURA/ASTRA** backend.
It replaces the Qt desktop shell with a premium, chart-first market-intelligence
terminal that renders **only** data supplied by the AURA HTTP API — and says so
plainly when that data is missing.

This project is a frontend only. It contains no trading logic, no analysis
logic and no backend of its own.

- **Stack:** React 18 + TypeScript (strict) + Vite 5 + React Router 6 + plain
  CSS design tokens + a hand-written canvas candlestick chart (no WebGL, no
  charting library).
- **API:** AURA API v1, schema `1.0` (`docs/architecture/API_V1_SCHEMA.json`).
- **Status:** shadow-only decision support. There is no live-trading path
  anywhere in this UI, by construction.

---

## 1. Quick start (Windows 10, no C++/Qt/MT5 needed)

```bat
:: 1. Node 20 or newer is required (node --version)
cd astra-frontend
npm ci                 :: installs exactly the locked dependency versions
copy .env.example .env.local
:: 2. start the backend you already have (ASTRA desktop host or the mock):
::    python scripts\mock_api.py --port 8790
:: 3. start the frontend
npm run dev            :: http://localhost:5173
```

macOS / Linux: the same steps with `cp` and `python3`.

### Commands

| Command | What it does |
|---|---|
| `npm ci` | Reproduce the locked dependency tree. |
| `npm run dev` | Dev server on :5173 (proxies `/api` → `http://127.0.0.1:8790`). |
| `npm run dev:mock` | Dev server **and** `scripts/mock_api.py` together. |
| `npm run typecheck` | `tsc -b --force`, including the test suite. |
| `npm test` | Vitest suite (69 tests, 6 files). |
| `npm run test:live` | Opt-in integration test against a running dev backend. |
| `npm run build` | Typecheck + production bundle into `dist/`. |
| `npm run preview` | Serve the production bundle on :4173. |
| `npm run verify` | typecheck → test → build (the full gate). |

---

## 2. Connecting to the AURA backend

`VITE_API_BASE_URL` is the only required variable (see `.env.example`).

**Local development — use the dev proxy.** The loopback backend
(`127.0.0.1:8790`) sends **no CORS headers** and answers pre-flight `OPTIONS`
with `501`, so a browser on `:5173` cannot call it cross-origin. Set the base
URL to the same-origin path; Vite forwards `/api` to the backend (see
`server.proxy` in `vite.config.ts`, override the target with
`VITE_DEV_API_TARGET`):

```
VITE_API_BASE_URL=/api/v1/
```

**Direct URL.** `VITE_API_BASE_URL=http://127.0.0.1:8790/api/v1/` works only if
your backend (or a proxy in front of it) returns
`Access-Control-Allow-Origin` for your dev origin. Otherwise every request is
blocked by the browser and the UI correctly reports `BACKEND OFFLINE`.

**Production.** The production build must point at the HTTPS gateway that
fronts the user's laptop backend. **That gateway does not exist in the AURA
repository yet.** Until it is deployed and tested, leave `VITE_API_BASE_URL`
unset: the app then shows `CLOUD CONNECTION NOT CONFIGURED` and refuses to
invent data. Never hardcode a fictional hostname, and never let the deployed
build fall back to `localhost` — the browser would silently talk to the
*visitor's* machine.

**Demo / offline.** `VITE_DEMO_MODE=1` switches the app to the same-origin
`/api/v1` path and labels every screen `DEMO DATA`. Use it with
`scripts/mock_api.py`. Demo data is never mixed silently with a real backend.

---

## 3. Screens

| Route | Screen | Endpoint(s) |
|---|---|---|
| `/` | Dashboard — KPI row, M15 chart, latest analysis, nine-timeframe overview, risk, open shadow positions | `/analysis/latest`, `/risk/latest`, `/shadow/positions`, `/timeframes`, `/candles` |
| `/chart` | Candlestick chart — 9 timeframes, candle-count selector, crosshair/OHLC, zoom & pan | `/candles?tf=&limit=` |
| `/timeframes` | Capability matrix for all nine timeframes + M15 snapshot | `/timeframes`, `/timeframes/{tf}/snapshot` |
| `/analysis` | Decision detail — direction, score, probability gate, context, levels, disclaimer | `/analysis/latest`, `/context/latest` |
| `/history` | Analysis history with filters + paging | `/analysis/history?limit=` |
| `/shadow` | Shadow positions (open/closed) and outcomes | `/shadow/positions`, `/shadow/outcomes` |
| `/risk` | Aggregate risk, system state, liveness, audit, incidents | `/risk/latest`, `/system/state`, `/health`, `/health/v1`, `/audit/recent` |
| `/bridge` | MT5 bridge lifecycle and market-data availability | `/bridge/status`, `/health` |
| `/research` | Research ledger (experiments + failure memory) | `/research/status` |
| `/governance` | Approval queue, history, allow-listed operator commands | `/governance/status`, `POST /command` |
| `/audit` | Audit ledger and active incidents | `/audit/recent` |
| `/settings` | Theme, refresh cadence, API/demo configuration readout, safety notes | — (local only) |

Nine XAUUSD timeframes are supported everywhere in the canonical order
**M1 → M5 → M15 → M30 → H1 → H4 → D1 → W1 → MN1**; M15 is the default
operational timeframe and H4 is marked as the structural-authority timeframe.

---

## 4. Honesty rules (enforced in code, asserted in tests)

- `signal.score` is a **raw, opaque number** with an unresolved scale. It is
  never converted to a probability, never given a `%`, and never assumed to be
  0–1 or 0–100.
- A probability is displayed **only** when `probability_calibrated` is true.
  Otherwise the UI prints `N/A — CALIBRATION NOT AUTHORISED`.
- `meta.score_is_probability` is never used as the display switch.
- `null` / `"UNKNOWN"` values render as `—` / "unavailable" — never `0`, never
  "healthy", never "safe".
- The backend's disclaimer is preserved verbatim.
- Failed polls mark data **stale** and keep the last values *with their real
  fetch time*; a disconnected backend is never disguised as a live one.
- The chart renders **closed bars only** — it never draws or implies a forming
  candle, and it never draws levels the backend did not supply.
- `SHADOW ONLY` is visible on every screen, at every viewport width.

---

## 5. Architecture

```
src/
  api/        client.ts (transport, envelope, retries, timeouts)
              endpoints.ts (one typed wrapper per frozen route)
              types.ts (contract types + canonical timeframe order)
              honors.ts (score/probability/state display rules)
              useApi.ts (the only data-fetch/polling hook)
  charts/     CandleChart.tsx (canvas renderer + OHLC readout)
  components/ AppShell, Sidebar/Drawer/TabBar, TopBar, Banners, States, Chip, DataCard
  layouts/    AppShell.tsx
  lib/        appContext.tsx (theme, refresh cadence, connection state)
  pages/      12 screens
  styles/     tokens.css, global.css, shell.css, pages.css
tests/        client, honors, components, useApi, app, command, live-mock
scripts/      mock_api.py + dev/test helpers + frozen contract fixtures
docs/         API schema, handoff guides, brand brief, deployment notes
```

Every component reads through `useApi` → `endpoints` → `client`. No component
builds its own request or bypasses the envelope decoder. Polling intervals:
analysis ≈5 s, health ≈10 s, candles ≈15 s, slow surfaces 30 s; requests never
overlap, are cancelled when superseded, and pause while the tab is hidden.

### Request/response handling

- Success envelope `{"api":"v1","schema":"1.0","data":…}` is required. A bare
  object, a wrong `api` constant or a missing `schema` key is a protocol error,
  surfaced as `API ERROR — UNEXPECTED_SHAPE`.
- `POST /command` is the one endpoint whose documented 202 body is **flat**
  (`{"accepted":true,…}`); the client decodes it in "optional envelope" mode
  while still rejecting flat `{error,code,message}` bodies.
- Errors map to useful text for 400/403/404/405/413/429/502/503 plus network
  failure and timeout. GETs retry with bounded exponential backoff on 5xx and
  network errors only; writes are never retried.

---

## 6. Testing and verification

```
npm run verify        # typecheck + tests + production build
npm run test:live     # optional: needs the backend on 127.0.0.1:8790
```

The suite covers API URL composition, envelope decoding (valid, invalid and
error fixtures from the frozen contract), null/missing values, probability
gating, score honesty, data-quality state rendering, chart empty states,
nine-timeframe order, mobile navigation, the disconnected/unconfigured states,
polling behaviour (no overlap, staleness, visibility pause) and command
validation. See `docs/VERIFICATION.md` for the recorded results and for the
checks that can only be completed in a browser.

`tests/live-mock.test.ts` runs the real client against a real dev server (the
T19 mock) and is skipped unless it is asked for.

---

## 7. Deployment (Railway)

The production artefact is a static SPA, so it deploys as a container serving
`dist/` behind nginx with SPA fallback:

- `Dockerfile` — multi-stage: `node:20-alpine` build → `nginx:alpine` serve.
- `nginx.conf` — gzip, cache headers, `try_files … /index.html`, and a
  pass-through location for `/api/` if you later put the gateway on the same
  origin.
- `railway.json` — Dockerfile builder, healthcheck on `/`.

Steps: create a Railway service from the repository, set the service root to
`astra-frontend/`, add the variable `VITE_API_BASE_URL` (build-time) pointing at the
gateway URL once it exists, then deploy. Full notes, including the gateway
requirements, are in `docs/DEPLOYMENT.md`.

> The app uses `HashRouter`, so deep links work on any static host without
> server rewrites; the nginx fallback is provided for non-hash entry URLs.

---

## 8. Known limitations

1. **No remote connectivity yet.** The AURA repository has no cloud-facing
   gateway. Until one is deployed, a deployed build can only show
   `CLOUD CONNECTION NOT CONFIGURED` — by design.
2. **No CORS on the loopback backend.** Use the dev proxy for local work.
3. **`POST /command` cannot be exercised against the T19 mock** (it is GET-only
   and answers `405`). The write path is contract-tested against the documented
   202/400/403 bodies and becomes live only with the real backend.
4. **Risk, research and audit modules may be empty** on a ground-zero backend;
   those screens show intentional empty states, never sample data.
5. `GET /signals/latest` and `GET /probability/latest` are absent from the mock
   (404). They are treated as optional legacy surfaces.

## 9. Safety

Shadow-only decision support. No live-trading switch, no order path, no broker
credentials, no service-role keys, no backend secrets in client code. The only
write the UI can perform is the allow-listed `notify` / `request_approval`
command with explicit actor attribution, exactly as the backend permits.
