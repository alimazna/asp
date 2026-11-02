# AYZR deployment record — 2026-10-30

Evidence-based record of what was actually deployed and verified. Every claim
below was produced by a command whose output is quoted or summarised here.

## 1. Railway

| Item | Value |
|---|---|
| Project | `just-success` — `e5ef331e-a01b-44af-919e-82d62dbe56bb` |
| Environment | `production` — `5a81ff63-b1ff-48de-98b9-731c05e9779f` |
| Service | `astra-server` — `7c55fadb-1d54-45d7-a49d-79502bd10b03` |
| Public host | `https://astra-server-production-9f1d.up.railway.app` |
| Volume | `astra-server-volume` (`870b788c-3f13-484d-83d9-38aef130885a`) at `/opt/astra/data` |

The project had **no services** when work began; the service, its public domain
and its volume were created through the Railway GraphQL API.

### Token format

The project token authenticates only as the `Project-Access-Token` request
header. As `Authorization: Bearer <token>` every endpoint answers
`{"errors":[{"message":"Not Authorized"}]}`. The Railway CLI sends the correct
header itself; it only failed earlier because a stale `~/.railway/config.json`
made it mis-parse its own config.

## 2. Service environment variables

Set on the service (values are held in the Railway dashboard, never in a file
in this repository):

| Variable | Purpose |
|---|---|
| `ASTRA_SENDER_TOKEN` | Feeder ingest bearer (also the future MT5 sender's token) |
| `ASTRA_CLIENT_TOKEN` | Read-only bearer the AYZR app uses |
| `ASTRA_ADMIN_TOKEN` | Admin routes |
| `ASTRA_ALLOWED_ORIGINS` | CORS origins |
| `ASTRA_DATA_DIR` | `/opt/astra/data` (the volume) |
| `AYZR_FEEDER_ENABLED` / `AYZR_FEEDER_INTERVAL_S` / `AYZR_FEEDER_BACKFILL` | Feeder behaviour (`1` / `30` / `500`) |
| `SUPABASE_URL` / `SUPABASE_SECRET_KEY` / `SUPABASE_TABLE` | Candle archive (optional) |
| `GEMINI_*`, `ZAI_*`, `MISTRAL_*`, `LLM7_*` | Multi-provider news-analysis chain |
| `ASTRA_INGEST_RATE_PER_MIN` etc. | Rate and batch limits |

The three ASTRA tokens are distinct random values; the backend refuses to boot
if any two are equal.

## 3. Deployment

```
railway up --service astra-server --environment production --detach
```

Build (`Dockerfile`, Ubuntu 24.04 + CMake/Ninja C++17) reached `SUCCESS`.
`railway.toml` carries no `startCommand`, so the image ENTRYPOINT runs and both
`astra_server` and the feeder start. Setting a `startCommand` would override the
entrypoint and silently drop the feeder.

## 4. Verified behaviour

| Check | Result |
|---|---|
| `GET /api/v1/livez` | `200` |
| `GET /api/v1/health` | `ready:true`, mode `DEGRADED`, `unknown_is_not_safe:true` |
| Timeframes with closed bars | M1, M5, M15, M30, H1, H4, D1 |
| W1 / MN1 | `no state observed` (biquote does not offer them — not invented) |
| Newest M1 bar age | ≈ 1–2 min and shrinking (feeder is live) |
| `GET /api/v1/analysis/latest` | well-formed; `mtf_core_gate_passed:true`, regime `RANGE`, H4 bias `DOWN` |
| `POST /api/v1/news/ingest` | `stored:1` |
| `GET /api/v1/news` | article listed |
| `GET /api/v1/news/analyze?id=…` | `status:ok`, Arabic summary, `direction:no_trade`, `provider_id:mistral` |
| Client token on read routes | `200` on health, analysis, candles, news, history |
| Client token on `POST /candles/ingest` | `401 unauthorized` (read-only enforced) |
| `GET /api/v1/providers` | zai `transport_error`, mistral `ok` 4/4 — failover working |
| Tracked files secret scan | clean |

## 5. Android release APK (bound)

Built against the live host with the read-only client token:

```
flutter build apk --release \
  --dart-define=AYZR_BASE_URL=https://astra-server-production-9f1d.up.railway.app \
  --dart-define=AYZR_CLIENT_TOKEN=<client token>
```

- `app-release.apk` — 22,732,872 bytes
- `sha256 a6a4e962b5c2e6d11631075476704f8b64bbf0dd082347997013a75bc34e4717`
- ABI slices: arm64-v8a, armeabi-v7a, x86_64
- Bound URL and client token confirmed present inside the AOT `libapp.so`.
- Only the read-only client token is embedded; no sender, admin, Supabase,
  Railway or LLM credential is in the APK.

`flutter test` → **120 passed, 3 skipped**. Live integration test against the
deployed host → **3 passed**.

## 6. Outstanding owner actions

1. Create the Supabase table — run `tools/ayzr_agents/supabase/schema.sql` once
   in the Supabase SQL Editor (the secret key cannot run DDL).
2. Rotate every credential that appeared in chat: Railway token, Cloudflare and
   R2 tokens, Mistral, z.ai, LLM7, and the Supabase secret key.
3. Install the APK on a device (no emulator was available here).
