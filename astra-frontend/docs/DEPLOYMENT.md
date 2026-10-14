# ASTRA web — deployment notes

## 1. What is being deployed

A static single-page application (`dist/`) plus, optionally, an HTTPS gateway
that fronts the user's laptop backend. The frontend itself has no server
runtime, no database and no secrets.

## 2. Environments

| Environment | API base URL | Notes |
|---|---|---|
| Local dev (proxy) | `/api/v1/` | Vite proxies `/api` → `http://127.0.0.1:8790`; avoids the missing CORS headers. |
| Local dev (direct) | `http://127.0.0.1:8790/api/v1/` | Only if the backend answers CORS for your origin. |
| Demo / offline | `/api/v1/` + `VITE_DEMO_MODE=1` | Served by `scripts/mock_api.py`; screens are labelled `DEMO DATA`. |
| Railway (static SPA only) | *(unset)* | Shows `CLOUD CONNECTION NOT CONFIGURED`. Honest, and the intended state until the gateway exists. |
| Railway + gateway | `https://<gateway-host>/api/v1/` | Requires the gateway below. |

Vite inlines `VITE_*` variables at **build** time. Changing the API URL means
rebuilding (or redeploying) — restarting the container is not enough.

## 3. Railway

1. New service from the repository; set the service root to `astra-frontend/`.
2. Railway builds `Dockerfile` (multi-stage node build → nginx runtime) as
   declared in `railway.json`.
3. Set `VITE_API_BASE_URL` as a build variable. Leave it unset if the gateway
   is not deployed yet.
4. Healthcheck is `GET /`; nginx listens on `$PORT` (template-substituted).
5. Deploy, then load the URL: the shell, brand mark, `SHADOW ONLY` chip and the
   honest connection state must render even with no API configured.

Static alternatives work too (Netlify/Vercel/S3+CloudFront/GitHub Pages): upload
`dist/`. `HashRouter` means deep links such as `/#/chart` need no rewrite rule.

## 4. The missing gateway (external requirement)

The AURA backend and the MT5 bridge are loopback-only by design
(`127.0.0.1:8790`, `127.0.0.1:8791`, `loopback_only: true`). A website cannot
reach them. Remote market data therefore requires a gateway that is **not part
of the AURA repository** and is not implemented here. It must provide:

1. HTTPS with a valid certificate (no mixed content from an HTTPS frontend).
2. Authentication and authorisation for every operator — never an open proxy to
   a laptop. Prefer per-user credentials, short-lived sessions, and an explicit
   allow-list of read routes.
3. A CORS policy (or same-origin deployment) that permits the frontend origin
   only, with `OPTIONS` pre-flight support — the loopback backend currently
   returns `501` for `OPTIONS`.
4. Read-route forwarding for the frozen v1 surface, plus `POST /api/v1/command`
   restricted to the `notify` / `request_approval` allow-list with actor
   attribution. Execution-like commands must stay rejected with 403.
5. Server-side rate limiting, request logging and a kill switch — the bridge
   reaching into MT5 on the operator's machine is the sensitive end.
6. No credentials in the browser: the gateway holds upstream secrets; the SPA
   never sees a backend or service-role key.

Until that exists, the correct deployed behaviour is the one implemented here:
`CLOUD CONNECTION NOT CONFIGURED`, no invented data, retry available.

## 5. Local operations

```bash
# backend (choose one)
python scripts/mock_api.py --port 8790          # frozen-contract mock, GET-only
# …or the real ASTRA desktop host, which supervises the MT5 bridge

# frontend
npm ci
npm run dev                                     # http://localhost:5173
npm run verify                                  # typecheck + tests + build
npm run test:live                               # integration test vs :8790
```

Container check without Railway:

```bash
docker build -t astra-frontend .
docker run --rm -p 8080:8080 astra-frontend
# → http://localhost:8080
```

## 6. Rollback

Static images are immutable; redeploy the previous image or revert the commit.
No data migration is involved, so rollback is instantaneous. If a bad API base
URL is deployed, unsetting `VITE_API_BASE_URL` and redeploying returns the app
to the explicit not-configured state rather than to a wrong endpoint.
