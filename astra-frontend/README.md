# ASTRA Web Frontend (Cloudflare Pages)

A dependency-free static SPA that renders the **real** ASTRA backend v1 API. It
contains no secrets — only a public HTTPS base URL is configured at build time.

## What it shows

- Server health, mode, and decision-grade data status (`/api/v1/health`,
  `/api/v1/system/state`).
- Latest analysis signal and probabilities (`/api/v1/analysis/latest`),
  rendered as **unavailable/uncalibrated** when the backend has no value —
  never fabricated.
- Latest context (`/api/v1/context/latest`).
- Real closed candles per timeframe (`/api/v1/candles?tf=...&limit=120`) drawn
  on a canvas.
- Bridge/transport status (`/api/v1/bridge/status`).
- Honest loading / stale / unavailable / empty states, auto-refresh, and
  mobile-friendly layout.

## Build & deploy (Cloudflare Pages)

1. Create a Pages project with root directory `astra-frontend` (or deploy the
   built `dist/` directly):

   ```bash
   # from the repo root
   BACKEND_BASE=https://<railway-service>.up.railway.app \
     ASTRA_REFRESH_MS=30000 \
     node astra-frontend/build.js
   ```

   The build command in Cloudflare Pages (for a repo-based Pages project whose
   root is `astra-frontend/`) is:

   ```
   build command: node build.js
   output directory: dist
   env var: BACKEND_BASE
   ```

   Alternatively, deploy the `dist/` directory directly:

   ```bash
   BACKEND_BASE=https://... node build.js
   npx wrangler pages deploy astra-frontend/dist --project-name astra
   ```

2. The backend must allow the Cloudflare origin in `ASTRA_ALLOWED_ORIGINS`
   (comma-separated) on Railway so browser CORS succeeds.

## Local development

```bash
BACKEND_BASE=http://127.0.0.1:12001 node astra-frontend/build.js
npx wrangler pages dev astra-frontend/dist --port 8788
```

or serve the source directory with any static file server after creating
`config.js`:

```bash
cp config.example.js config.js
# edit config.js to point at your local backend
python3 -m http.server 8000 -d astra-frontend  # config.js must exist
```

## Files

| File | Purpose |
| --- | --- |
| `index.html` | Single page layout. |
| `assets/app.css` | Responsive dark theme. |
| `assets/app.js` | API client, rendering, candlestick canvas, refresh loop. |
| `config.example.js` | Placeholder runtime config (committed). |
| `build.js` | Emits `dist/config.js` from the `BACKEND_BASE` env var + copies assets. |
| `dist/` | Generated build output (never commit unless required). |

## Security

No backend secrets, sender tokens, or admin credentials are ever placed in
this frontend. `config.js` carries only the public HTTPS base URL of the
read-only API.