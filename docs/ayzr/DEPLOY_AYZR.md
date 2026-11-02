# AYZR deployment runbook (Railway)

How to put the AYZR stack online: the ASTRA backend **plus** the perpetual
biquote candle feeder in a single self-contained container. One image, one
candle source, zero external collector.

Verified locally on 2026-10-30 by building and running the image with Docker
(see "What was actually verified" at the end).

## Architecture

```
Railway container (one image)
  ├── astra_server        :$PORT   /api/v1/...   (read + ingest API)
  └── biquote_sender.py   loop, 30s interval → POSTs to 127.0.0.1:$PORT
        └── fetches closed XAUUSD bars from https://biquote.io (keyless)
  durable store: $ASTRA_DATA_DIR/candles.sqlite  (Railway volume)
```

Two processes, one image (`docker/entrypoint.sh`). If either dies the container
exits and Railway's `ON_FAILURE` restart policy brings it back. `curl` in the
entrypoint waits for `/api/v1/livez` before the feeder starts, so the first
batch is never lost to a race.

## Candle source

biquote is the **sole and permanent** candle source. It is public and keyless,
so the container needs no MT5 terminal, no bridge, and no sender host. It
offers 7 of the 9 ASTRA timeframes (M1…D1); W1 and MN1 are not offered by
biquote and remain the MT5 sender's job. The backend reports those two as
"no state observed" rather than inventing values.

## Prerequisites

- A Railway project and service (the owner's service: `astra-server`).
- A Railway API token with deploy rights, **or** use the dashboard.
- One Railway volume mounted at `/opt/astra/data`.

The agent environment does **not** hold a Railway token; the owner deploys.

## Environment variables (set in the Railway dashboard — never in a file)

| Variable | Required | Purpose |
|---|---|---|
| `ASTRA_SENDER_TOKEN` | yes | Feeder's bearer token for `POST /api/v1/candles/ingest`. Also the token a future MT5 sender uses. |
| `ASTRA_CLIENT_TOKEN` | yes | Read-only token the AYZR app uses for `/news`, `/analysis/latest`, `/history`, `/candles`. Must differ from the sender token. |
| `ASTRA_ADMIN_TOKEN` | no | Enables the admin inspection/quarantine routes. Leave unset to disable them. Must differ from both above. |
| `ASTRA_ALLOWED_ORIGINS` | no | Comma-separated browser origins if a web client is ever allowed. |
| `AYZR_FEEDER_ENABLED` | no | `0` runs the backend without the feeder (e.g. when the MT5 EXE becomes the source). Default `1`. |
| `AYZR_FEEDER_INTERVAL_S` | no | Feeder poll interval. Default `30`. |
| `PORT` | no | Assigned by Railway automatically. |
| `LLM7_API_TOKEN` | no | Enables live AI news analysis. Without it the pipeline reports an honest `unavailable` state and the rest of the app keeps working. |
| `LLM7_BASE_URL` / `LLM7_MODEL` | no | Defaults `https://api.llm7.io/v1` / `nemotron-3-nano:30b`. |

The backend **refuses to start** if the same value is reused across the
sender/admin/client token sets — that separation is a security invariant.

## Deploy with the Railway CLI

```bash
# from the repository root; the Dockerfile is picked up automatically
railway up --service astra-server --environment production --ci
```

Alternatively push the branch and use the dashboard, or run `railway up` from
an authenticated shell. The agent environment cannot do this (no token).

## Deploy-free verification before you ship

Build and run the exact image locally first (needs a Docker daemon):

```bash
docker build -t ayzr-test -f Dockerfile .
docker run -d --name ayzr-run -p 18080:8080 \
  -e PORT=8080 \
  -e ASTRA_SENDER_TOKEN=demo-sender \
  -e ASTRA_CLIENT_TOKEN=demo-client \
  -e ASTRA_ADMIN_TOKEN=demo-admin \
  -e ASTRA_DATA_DIR=/tmp/astra-data \
  ayzr-test

# backend liveness
curl -fsS http://127.0.0.1:18080/api/v1/livez
# a timeframe should be filling within ~60s of start
curl -fsS "http://127.0.0.1:18080/api/v1/candles?symbol=XAUUSD&tf=M15&limit=3" \
  -H "Authorization: Bearer demo-client"
docker rm -f ayzr-run
```

## Post-deploy checks

```bash
BASE=https://<your-service>.up.railway.app/api/v1
curl -fsS "$BASE/livez"
curl -fsS "$BASE/health"                                   -H "Authorization: Bearer $ASTRA_CLIENT_TOKEN"
curl -fsS "$BASE/analysis/latest"                          -H "Authorization: Bearer $ASTRA_CLIENT_TOKEN"
curl -fsS "$BASE/candles?symbol=XAUUSD&tf=M15&limit=5"     -H "Authorization: Bearer $ASTRA_CLIENT_TOKEN"
```

`/health` should show `decision_grade_data: true` once enough closed bars have
arrived. A `DEGRADED` mode with partial timeframe coverage is the honest state
before W1/MN1 have a source.

## Point the Android app at it

Rebuild the APK with the deployed host (see `docs/ayzr/ANDROID.md`):

```bash
flutter build apk --release \
  --dart-define=AYZR_BASE_URL=https://<your-service>.up.railway.app \
  --dart-define=AYZR_CLIENT_TOKEN=<the client token>
```

Give the app the **host root** (no `/api/v1` suffix). The app also tolerates a
`/api/v1` suffix, but the root is the canonical form.

## What was actually verified (2026-10-30, locally)

- `ubuntu:22.04` as the base **failed to build**: its libcurl 7.81 lacks
  `CURLOPT_PROTOCOLS_STR` (added in 7.85). Base raised to `ubuntu:24.04`
  (libcurl 8.5). Without this the Railway build would have failed.
- Image builds clean; container starts; backend binds and answers on `$PORT`.
- The feeder fetched real closed bars and persisted them: all 7 biquote
  timeframes (M1…D1) returned data over the public API. M1 close observed live.
- The analysis endpoint returned a well-formed payload after warmup.
- No secret is embedded in `Dockerfile`, `docker/entrypoint.sh`, or
  `railway.toml`.

## Live deployment (2026-10-30)

Deployed and verified end-to-end. Details in `docs/ayzr/DEPLOYMENT_RECORD.md`.

- Project `just-success` (`e5ef331e-a01b-44af-919e-82d62dbe56bb`), environment
  `production`, service `astra-server` (`7c55fadb-1d54-45d7-a49d-79502bd10b03`).
- Public host: `https://astra-server-production-9f1d.up.railway.app`.
- Volume `astra-server-volume` mounted at `/opt/astra/data` (SQLite survives
  redeploys).
- `GET /api/v1/livez` → 200. `GET /api/v1/health` → `ready:true`, all 7 biquote
  timeframes reporting closed bars; W1/MN1 "no state observed" (honest).
- Feeder is live in-container: newest M1 bar age ≈ 1–2 min and shrinking.
- AI news analysis ran end-to-end through the failover chain
  (`zai → mistral → llm7`): z.ai failed `transport_error`, Mistral answered
  (`provider_id: mistral`, `model_id: open-mistral-nemo`). The chain is what
  makes the news pipeline survive a single provider outage.

### Railway token quirk (important for re-deploys)

The project token must be sent as the **`Project-Access-Token`** header, not
`Authorization: Bearer`. With `Bearer` every call returns `Not Authorized`.
The Railway CLI sets the header correctly on its own, but it silently mis-parses
a stale `~/.railway/config.json` / `.railway/config.json`; delete those first if
`railway status` warns "Unable to parse config file".

## Not verified (needs owner action)

- Supabase `public.candles` table — the secret key is an API key, not a
  database password, so it cannot run DDL. Apply
  `tools/ayzr_agents/supabase/schema.sql` once by hand in the Supabase SQL
  Editor. Until then the feeder's Supabase dual-write logs `PGRST205` and the
  feed continues unaffected.
- W1/MN1 coverage — depends on the separate MT5 sender, which is out of scope.
- Real-device install — the release APK builds and its bound client token was
  proven against the live API from this host, but no Android device/emulator was
  available to install it.
