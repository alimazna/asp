# AYZR — Deployment Readiness

Status: **local readiness complete; production deploy awaits owner credentials.**
No production deployment has been performed. Nothing below claims otherwise.

## 1. What is verified locally

| Item | Command | Result |
|------|---------|--------|
| Backend builds | `cmake --build build-linux -j4` | OK, 0 errors, 0 warnings |
| Backend tests | `ctest` | 30/30 pass |
| News unit tests | `./NewsIntelligenceTests` | 32/32 pass |
| AI provider tests | `./NewsIntelligenceAgentCTests` | 34/34 pass |
| News API integration | `./NewsApiIntegrationTests` | 6/6 pass |
| Failure injection | `./AyzrFailureInjectionTests` | 18/18 pass |
| Mock MT5 sender E2E | `python3 tools/ayzr_agents/mock_sender.py --host 127.0.0.1 --port <p> --token <sender-token>` | PASS (4/4 steps) |
| Android tests | `flutter test` | 45/45 pass |
| Android debug APK | `flutter build apk --debug` | see `ANDROID.md` |

The container image (`Dockerfile`) installs `libcurl4-openssl-dev` in the build
stage and `libcurl4` at runtime. Without libcurl the server compiles but has no
outbound HTTPS transport, so every live news analysis honestly degrades to
`unavailable`. A clean configure confirms `Found CURL ... 8.14.1` and
`ldd astra_server` shows `libcurl.so.4`.

## 2. Credentials required (derived from the code, not invented)

### Required to run locally
None for a basic run — SQLite is created under `--data-dir`. For the news
analysis path, `LLM7_API_TOKEN` is needed; without it the pipeline honestly
reports `unavailable`.

### Required for Railway backend deployment

| Variable | Purpose | Where set |
|----------|---------|-----------|
| `ASTRA_SENDER_TOKEN` | bearer for candle + news ingest (write) | Railway variables |
| `ASTRA_ADMIN_TOKEN` | bearer for admin quarantine routes | Railway variables |
| `ASTRA_CLIENT_TOKEN` | read-only bearer for the Android app | Railway variables |
| `ASTRA_DATA_DIR` | `/opt/astra/data` (volume) | Railway variables |
| `ASTRA_ALLOWED_ORIGINS` | CORS origins (Cloudflare Pages URL) | Railway variables |
| `PORT` | assigned by Railway | automatic |

`ASTRA_SENDER_TOKEN`, `ASTRA_ADMIN_TOKEN` and `ASTRA_CLIENT_TOKEN` **must be three
different values**: they guard disjoint capabilities (write / destructive / read).

### Required for LLM news analysis

Provider selection is `ASTRA_LLM_PROVIDER`. Set it to `auto` to build an ordered
failover chain from every provider that has a credential, or pin it to one name
(`gemini` | `zai` | `mistral` | `llm7`) to isolate that provider. `mock` runs the
pipeline offline for tests.

| Variable | Purpose |
|----------|---------|
| `ASTRA_LLM_PROVIDER` | `auto` (chain) \| `gemini` \| `zai` \| `mistral` \| `llm7` \| `mock` |
| `ASTRA_LLM_ORDER` | chain order for `auto` (default `gemini,zai,mistral,llm7`); a provider without a credential is skipped |
| `LLM7_API_TOKEN`, `LLM7_BASE_URL`, `LLM7_MODEL` | LLM7 (OpenAI-compatible); default model `nemotron-3-nano:30b` |
| `GEMINI_API_KEY`, `GEMINI_BASE_URL`, `GEMINI_MODEL` | Gemini (native `generateContent`); key travels in `X-goog-api-key` |
| `ZAI_API_KEY`, `ZAI_BASE_URL`, `ZAI_MODEL` | z.ai (OpenAI-compatible) |
| `MISTRAL_API_KEY`, `MISTRAL_BASE_URL`, `MISTRAL_MODEL` | Mistral (OpenAI-compatible) |
| `ASTRA_MOCK_LLM_MODE` | with `ASTRA_LLM_PROVIDER=mock`: `valid` (default), `malformed`, `empty`, `timeout`, `rate_limited`, `quota_exhausted`, `auth_failure`, `malformed_then_valid`. An unknown value falls back to `valid` |
| `LLM7_CONNECT_TIMEOUT_MS`, `LLM7_READ_TIMEOUT_MS`, `LLM7_MAX_ATTEMPTS` | bounded timeouts/retries |
| `LLM7_QUOTA_FALLBACK_COOLDOWN_S` | cooldown seconds used when a 429 quota response omits a reset time (default 3600) |

Every provider key is a **server-side secret**; none is ever embedded in the
APK. `GET /api/v1/news/quota` reports the whole-chain verdict
(`ok` | `degraded` | `all_quota_exhausted` | `all_unavailable` | `no_provider`)
so the app shows an honest Arabic banner instead of looking broken.

### Cloudflare database
Not required. No Cloudflare database is used (see `CLOUDFLARE_DATABASE.md`). The
only Cloudflare credential in the project is `CLOUDFLARE_API_TOKEN` for the
**existing frontend Pages deploy**, which is out of scope here.

### Android app
- `AYZR_BASE_URL` (build-time `--dart-define`): the backend URL.
- `AYZR_CLIENT_TOKEN` (build-time `--dart-define`) or the in-app settings screen:
  the **read-only** client token. Never the sender, admin, DB or LLM7 token.

## 3. Deployment procedure (Railway) — for when credentials exist

```bash
# 1. Authenticate (owner supplies RAILWAY_TOKEN in the environment; never commit)
railway login --token "$RAILWAY_TOKEN"
railway link --service astra-server --environment production

# 2. Set secrets in the dashboard or via CLI (values never in the repo)
railway variables set ASTRA_SENDER_TOKEN=... ASTRA_ADMIN_TOKEN=... \
    ASTRA_CLIENT_TOKEN=... LLM7_API_TOKEN=... ASTRA_LLM_PROVIDER=llm7

# 3. Deploy current source (Railway has no GitHub repo connected)
railway up --service astra-server --environment production --ci

# 4. Verify
curl -fsS https://astra-server-production.up.railway.app/api/v1/livez
curl -fsS https://astra-server-production.up.railway.app/api/v1/readyz

# 5. Verify the receiving API with an authorized test batch (mock sender against
#    the deployed URL, with the real sender token) — only with owner consent.

# 6. Build the app against production
cd ayzr-android && flutter build apk --release \
  --dart-define=AYZR_BASE_URL=https://astra-server-production.up.railway.app \
  --dart-define=AYZR_CLIENT_TOKEN=<client token>
```

## 4. Database migrations

None are required. The news tables are created idempotently on first open
(`CREATE TABLE IF NOT EXISTS`) by `NewsStore::open`; the candle schema is
unchanged. No destructive migration is performed.

Recovery: back up the Railway volume before any deploy that could touch data
(`railway volume` snapshot). SQLite WAL files (`*.sqlite-wal`, `*.sqlite-shm`)
must be copied together with the `.sqlite` file for a consistent backup.

## 5. Health and dependency checks

| Route | Meaning |
|-------|---------|
| `GET /api/v1/livez` | process liveness (used by Railway healthcheck) |
| `GET /api/v1/health` | diagnostics |
| `GET /api/v1/readyz` | readiness; `503` until closed candles arrive |

`readyz` returns `ready:true` with `mode:DEGRADED` when there is no bridge and
partial timeframe coverage — that is the honest state, not a failure.

## 6. Remaining limitations

- **Not deployed.** No Railway credential exists in the agent environment, so the
  production deploy and the LLM7 live-analysis path are **not** verified in
  production. The LLM7 endpoint itself was probed live (unauthenticated free
  tier) and returns the expected OpenAI-compatible shape for
  `nemotron-3-nano:30b`.
- **No real-device test.** The APK is built and widget-tested; it has not been
  installed on a physical device.
- **EXE integration is future work.** The separate MT5 Sender EXE project has not
  been connected; only the receiving contract is prepared and tested with a mock.

## 7. Credential rotation

If any token was ever exposed in chat, logs, or a public artifact, treat it as
compromised and rotate it in the Railway dashboard; update the Android build and
the sender config accordingly. Never reuse a leaked value.
