# AGENTS.md — ASTRA repository notes

Shadow-only XAUUSD market-intelligence system. No order execution, ever.

## Layout

- `src/` — C++17 backend. CMake builds `astra_server` + `libaura_core.a`
  (AURA engine, decision pipeline, `CandleStore`, `AstraApiServer`).
- `astra-frontend/` — **static SPA**, no `package.json`, no framework.
  `node build.js` emits `dist/` (`index.html`, `assets/*`, generated
  `config.js`). Live at https://astra-dia.pages.dev (Cloudflare Pages,
  project `astra`, direct upload — **not** Git-connected).
- `windows-sender/` — Python MT5 closed-candle sender (`astra_sender/`) plus
  `fake_mt5` test fixture. Windows-only because of the `MetaTrader5` package.
- `bridge/mt5_python/` — Windows-only MT5 access. Not needed by the server.

## Build and test

```bash
# backend (needs libsqlite3-dev)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# no ctest in this image; run the binaries directly:
cd build-linux && for t in *Tests; do ./"$t" || echo "FAIL $t"; done   # 26 binaries

# frontend
cd astra-frontend && node --test tests/*.test.js && node build.js
node scripts/secret-scan.js dist

# windows sender
cd windows-sender && python3 -m unittest discover -s tests -v   # 17 tests
```

## Production

- Backend: https://astra-server-production.up.railway.app (`railway.toml`,
  `Dockerfile`; SQLite on a Railway volume at `/opt/astra/data`).
- Frontend: https://astra-dia.pages.dev.

## Data-honesty rules (enforced in code)

The production store contains **M15 rows that are not MT5 data** (price jumps
1901→2001; open times not M15-aligned). The backend derives its M15 `observed`
flag, watermark, and freshness from them, so a naive UI reports a synthetic
timestamp as a real reading.

Two independent guards withhold that data:

- **Ingest** rejects any intraday candle whose `open_time` is not aligned to its
  timeframe grid (`parseCandle` / `AuraRuntime`), so no new bad row is stored.
  D1/W1/MN1 are exempt (whole-hour broker offsets break the check for them).
- **Reads** quarantine a whole `(symbol, timeframe)` series when any of its rows
  is grid-misaligned: `CandleStore::recent`, `syncPoints`, `syncPointsForSource`
  and `latestOpenTimeAnySource` append a correlated `NOT EXISTS` predicate, so
  `LIMIT` and `MAX()` only ever see a clean series. A plausible-looking aligned
  sibling of a fabricated row is therefore withheld too, and the API reports
  "no candle data available" for that timeframe rather than serving a fake.

Third layer: the frontend `assets/lib.js#candleBatchIntegrity(bars, timeframe)`
gate checks open times are exact multiples of the timeframe and spaced exactly
one timeframe apart. Every view that could surface M15 calls it and withholds or
flags failing data: chart, forecast watermark, market-data last-closed-candle,
market-data observed count, market-data coverage matrix + watermark table,
dashboard coverage metric and matrix.

The gate is data-driven: genuine MT5 bars pass and display normally. Never
hardcode a timeframe as untrusted.

## Gotchas

- CORS is strict. Loopback origins are refused (403). To preview locally against
  the live backend, run a same-origin proxy; do not add localhost to
  `ASTRA_ALLOWED_ORIGINS` in production.
- The remote URL has carried an embedded GitHub token. Refresh it with
  `git remote set-url origin "https://${GITHUB_TOKEN}@github.com/alimazna/asp.git"`
  when pushes prompt for a password.
- The available GitHub token is a GitHub App token: no `x-oauth-scopes`, and
  `gh secret set` / the branch-protection API return 403. Repository secrets
  cannot be read or written from the agent environment.
- `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` are **not** set in the
  agent environment and never appear in conversation history. Pages deployment
  needs them (token with *Cloudflare Pages: Edit*). See
  `astra-frontend/scripts/deploy-pages.sh`.
- The Windows release-attach CI step returns 403, but the
  `sender-v0.3.0` release asset exists and is valid.

## Production state (2026-10-16)

- Backend: `https://astra-server-production.up.railway.app` (service
  `astra-server`, project `romantic-warmth`). Railway has **no GitHub repo
  connected** — deploy the current source with
  `railway up --service astra-server --environment production --ci`.
- Durable store: Railway volume `astra-server-volume`
  (`4605ca78-b54a-4688-934d-45b37f54a7a8`) mounted at `/opt/astra/data`.
- Frontend: `https://astra-dia.pages.dev` (Cloudflare Pages project `astra`).
- All five synthetic deployment-verification rows were purged on 2026-10-16.
  Production now holds the owner's **real** MT5 data for the multi-hour
  timeframes (H4/D1/W1/MN1); the intraday timeframes are empty because the
  pre-`f92075e` guard rejected them (see the broker-clock section below).
  `readyz` reports `ready:true` with `mode:DEGRADED`, which is the honest state
  for a backend with no bridge and partial timeframe coverage.
- **2026-10-19:** the deployed container still runs the pre-`f92075e` ingest
  guard (see below). The broker-clock fix is merged to `main` (`5afe44d`,
  PR #15) but **not deployed**, because no Railway credential exists in the
  agent environment. The owner's "only 4 timeframes" symptom persists until
  `Deploy backend to Railway` runs (needs a `RAILWAY_TOKEN` secret) or
  `railway up` runs with a token.
- Admin inspection: `GET /api/v1/admin/candles?tf=...` (admin token only).
  `scripts/quarantine_admin.py list --all` flags misaligned rows and prints
  the exact purge command.
- Railway SSH needs interactive account auth, so the project token cannot open
  a shell in the container; use the admin HTTP routes instead.

## Windows sender: MT5 attach (v0.4.6+)

- `MetaTrader5.initialize()` defaults to a **60000 ms** IPC timeout. Always pass
  an explicit `timeout=` (the sender uses `mt5_timeout_ms`, default 15000), or a
  dead terminal blocks for a minute and looks like a hang.
- MT5 takes the terminal path as the **first positional** argument; it is not
  accepted as a keyword. `Mt5Client.initialize(path, timeout_ms)` handles this.
- A blank `mt5_path` is correct for the common case: the package attaches to the
  already-running terminal. A path is only needed for broker terminals installed
  under a suffixed folder (e.g. `MetaTrader 5 IC Markets (SC)`).
- **`-10005 IPC timeout` almost always means a privilege mismatch** between the
  sender and `terminal64.exe`. Both must run elevated, or neither. The terminal
  looks healthy either way, so it is easy to misdiagnose.
- `SenderConfig.from_dict()` reads only known keys and silently drops the rest.
  A hand-edited config key the dataclass does not declare is ignored with no
  warning - check the dataclass before telling a user to edit JSON.
- The diagnostic runs the attach on a daemon thread with a bounded deadline so it
  cannot hang. Keep it that way; it is the only support channel on the owners PC.

## Windows sender: frozen bridge packaging (v0.4.7+)

- **The bridge must be bundled flat at the PyInstaller bundle root**, never
  inside the `astra_sender` package. `bridge/mt5_python/mt5_client.py` does
  `from schemas import TIMEFRAMES`, so `schemas.py` has to sit on `sys.path`.
  In `astra_sender.spec` the `datas` destination is `"."` for both files.
- Shipping them as `astra_sender.mt5_client` was the **real** cause of the
  "MT5 UNAVAILABLE but my Python script works" report: the module was found, then
  `from schemas import TIMEFRAMES` raised `ModuleNotFoundError`, and the loader
  swallowed it. Reproduced by building a onefile exe with the old layout.
- `_load_mt5_client()` must **never** swallow an ImportError silently. It now
  collects every attempt into the raised message. A bare "cannot locate" sent the
  owner chasing the terminal when the fault was packaging.
- `--selfcheck` imports `ClosedCandleCollector` (the package) but that does *not*
  load the separately-bundled bridge. It now calls `_load_mt5_client()` and exits
  1 on failure. `ASTRA_SENDER_TEST_BREAK_BRIDGE=1` forces that failure so CI
  proves the guard fires instead of trusting it.
- `--diag-mt5` used `MetaTrader5` directly, so it exited 0 while the GUI reported
  UNAVAILABLE - the exact contradiction users report. It now checks the bridge
  first and returns **7** on a packaging fault, before touching the terminal.
- Version strings must have one source: `astra_sender/__init__.py.__version__`.
  `__main__.py` had a second literal (`VERSION = "0.4.4"`) that drifted; it now
  imports the package value.

## Security: never publish a token-bearing installer

The repository is **public**, so any GitHub Release asset is world-readable. The
`-Configured` installer has the ingestion token compiled into it (innoextract /
7-Zip recover it trivially - Inno compression is not encryption), so publishing
one leaks a working credential to anyone. `AstraSenderSetup-0.4.6-Configured.exe`
was public from 2026-10-16 until it was deleted on 2026-10-17; **that token must
be treated as compromised and rotated.**

Rules:
- Release assets may contain only the token-less installer. The workflow has a
  guard step that fails if a `*Configured*` file is present, and it runs before
  the upload.
- A token-less installer is not a dead end for an existing user: an upgrade
  reuses the config already provisioned in
  `%PROGRAMDATA%\AstraSender\config.json` (`_looks_configured` gates this). Only a
  first-time user with no config needs a provisioned build, and those are
  delivered privately.
- The `[Run]` entry in `astra_sender.iss` must keep `runasoriginaluser`. Setup
  elevates, and MT5's IPC handshake fails when the sender and `terminal64.exe`
  run at different integrity levels, which surfaces as "MT5 UNAVAILABLE".

## Bar timestamps are broker time, not UTC (v0.4.8+)

- MT5 stamps bar open times in **broker server time**. This broker is UTC+3, so
  a bar that opened at 09:00 UTC carries the stamp `12:00`. Treating that stamp
  as UTC makes the newest intraday bars look up to three hours future-dated.
- The failure mode is sneaky: the future guard drops exactly the timeframes whose
  newest closed bar opens within the offset window (M1..H1), while H4/D1/W1/MN1
  open far enough back to pass. The owner sees "only 4 timeframes arrive" and
  nothing looks broken. **Suspect this before any transport or auth theory.**
- The fix is symmetric and lives on both sides:
  - sender: `Collector.clock_offset_seconds` measures (broker - UTC) from the
    newest M1 bar and the collector validates against that frame;
  - wire: the ingest body carries `clock_offset_seconds` (optional, additive);
  - server: `parseClockOffset` clamps to |offset| <= 18h and both the per-candle
    guard and the runtime closure/freshness check run in broker time.
- Clamp the offset; never trust it blindly. An implausible offset means a stale
  feed, not an exotic timezone, and accepting it would mask exactly the staleness
  this system must never hide. A genuinely future bar must still be rejected.
- Deploying the sender alone is **not** enough — the server-side guard is the
  half that rejected the bars. Ship both, or the symptom persists.


## AYZR mobile + news intelligence (2026-10-27)

- `ayzr-android/` is a **separate** Flutter app (package `com.ayzr.ayzr_app`,
  Arabic-only, RTL). It is NOT the `astra-frontend/` SPA; do not reuse or read
  that frontend. Build: `cd ayzr-android && flutter build apk --debug`.
- The Android SDK lives at `/workspace/tools/android-sdk` (platform 34,
  build-tools 34.0.0, **ndk 27.0.12077973**). `android/app/build.gradle` pins
  `ndkVersion = "27.0.12077973"`: `shared_preferences_android` requires it and
  Flutter's default (23.1.7779620) raises a build warning. NDK releases are
  backward compatible, so pin the highest required version.
- Outbound LLM7 HTTP needs **libcurl**: `apt-get install libcurl4-openssl-dev`,
  then reconfigure cmake. Without it the news pipeline honestly reports
  `unavailable` ("outbound HTTP transport not compiled"). The build finds the
  header at `/usr/include/x86_64-linux-gnu/curl/`.
- `nemotron-3-nano:30b` on the LLM7 free tier has **no JSON mode** and returns
  *valid JSON that violates the schema* on the first turn (it folds top-level
  keys into a `facts` element). The service makes **one** conversational repair
  turn (system + user + the invalid assistant reply + a corrective user message
  with a concrete example). An inline restatement in a single user message does
  NOT work; the assistant+user turn sequence does.
- The LLM7 free tier **rate-limits aggressively** (HTTP 429 within seconds),
  and a real-token probe on 2026-10-27 confirmed the daily free budget can be
  fully exhausted (`insufficient_quota`, `retry_after` ~19 h) — the account has
  a $0 balance. The adapter classifies 429 as retryable/bounded and the API
  degrades to `unavailable`; reliable production analysis needs a paid balance.
- A **429 is two opposite conditions** and must not be conflated. `LlmQuotaMonitor`
  (`src/news/LlmQuotaMonitor.*`) refines the 429: a body with a quota marker
  (`insufficient_quota`/`quota_exceeded`/`daily token quota`) is `quota_exhausted`
  (**not retryable** — the service opens a cooldown and withholds provider calls);
  any other 429 is `rate_limited` (retryable, bounded). The reset comes from the
  `Retry-After` header or the body `retry_after`; when absent it cools down via
  `LLM7_QUOTA_FALLBACK_COOLDOWN_S` but reports `retry_after_seconds: null` — it
  **never invents a countdown**. `GET /api/v1/news/quota` (client token) exposes
  a secret-free snapshot; the Android app shows an Arabic `QuotaBanner`.
- **Live 429 shape (2026-10-27):** `retry-after: 66401` header plus body
  `{"error":{"message":"Daily token quota exceeded. Retry after 66401 seconds.","type":"insufficient_quota","param":null,"code":"quota_exceeded","retry_after":66401}}`.
  The classifier matches it exactly; the test fixture is grounded in this probe.
- `ASTRA_MOCK_LLM_MODE` (`ASTRA_LLM_PROVIDER=mock`) selects a mock failure mode
  (`valid`/`malformed`/`empty`/`timeout`/`rate_limited`/`quota_exhausted`/
  `auth_failure`/`malformed_then_valid`) so the full HTTP path can be
  failure-injected **without spending real quota**. Unknown value → `valid`.
- **`CandleStore::recent()` returned the OLDEST `limit` rows**, not the newest,
  despite its name. With 6667 real M15 rows and `limit=500`, the candles route
  published `newest_closed_time` ~98 days stale as the "newest closed candle".
  Fixed to select the newest N in an inner `DESC` query then re-sort `ASC`, with
  the quarantine predicate kept inside the inner query so `LIMIT` sees a clean
  series (agent-b, verified independently by the principal).
- News routes are client-token gated: `/api/v1/news`, `/api/v1/news/analyze?id=`,
  `/api/v1/history` (note: **not** `/news/history`). Ingest is sender-token
  gated: `/api/v1/news/ingest`. Candle read is `/api/v1/candles?tf=...&limit=...`
  and returns a `bars` array (the route ignores `symbol`, defaults `tf=M15`).

## Macro events (backend + AYZR Android, 2026-10-27)

- `src/macro/CalendarFeed.{h,cpp}`, `EventStore.{h,cpp}`, `EventWatcher.{h,cpp}`
  add a scheduled-macro-event surface on top of the news pipeline. The watcher
  polls a real calendar (default FairEconomy `ff_calendar_thisweek.json`) and
  writes two analysis turns per event, keyed by
  `source|country|title|scheduled_at`: an EXPECTATION before release and a
  RESULT after. Client routes: `GET /api/v1/events?scope=upcoming|past&limit=`
  and `GET /api/v1/events/detail?key=` (client-token gated, read-only).
- Honesty invariants (mirror the news surface): an unpublished consensus
  serializes as `null`, never `0`/`""`; a turn that has not run reports
  `has_expectation`/`has_result=false`, which the UI shows as "pending" — never
  as "ran and found nothing".
- **Dangling-pointer trap:** the calendar source must be a `shared_ptr` that
  outlives the watcher. Scoping it inside the setup `if` block left the watcher
  holding a raw pointer that dangled (symptom: `URL using bad/illegal format`).
  Keep it at function scope in `AstraServerMain`.
- A failing calendar feed backs off exponentially (cap ~15 min). Do not replace
  this with a fixed short retry — the public feed rate-limits (HTTP 429).
- The outbound client is HTTPS-only; an `http://` fixture calendar is rejected
  by design. Use the real HTTPS feed or a mock source for tests.
- Android lives in `ayzr-android/` (Flutter, Arabic-only RTL, `com.ayzr.ayzr_app`,
  INTERNET permission only). Shared analysis rendering is
  `lib/ui/widgets/analysis_view.dart`, used by both news detail and event turns.
  Build: `ANDROID_HOME=/workspace/tools/android-sdk JAVA_HOME=/workspace/tools/jdk17
  flutter build apk --debug`. Tests: `flutter test` (86 pass).

## Multi-provider LLM failover (2026-10-27)

- `ASTRA_LLM_PROVIDER=auto` (default) builds an ordered chain from
  `ASTRA_LLM_ORDER` (default `gemini,zai,mistral,llm7`); a provider whose key is
  empty is skipped. Pinning the var to one name selects exactly that provider —
  it must never silently become a chain, or a test pointed at a dead port would
  be rescued by the others.
- `FailoverLlmProvider::health()` folds per-provider outcomes into
  `ok | degraded | all_quota_exhausted | all_unavailable | no_provider`. Only
  **hard** outcomes (`auth_failure`, `quota_exhausted`, `bad_request`) mark a
  provider down; a timeout/5xx/transport/rate-limit may clear and must not be
  reported as down. `GET /api/v1/news/quota` carries this plus per-provider
  rows (labels/counts only, never key material).
- **Gemini maps every 429 to RateLimited at the status level.** A spent daily
  budget and a temporary burst limit are both HTTP 429 + `RESOURCE_EXHAUSTED`,
  so the body must be inspected: only an exhaustion marker
  (`insufficient_quota`, `quota_exceeded`, `exceeded your current quota`, `check
  your plan and billing`) is `quota_exhausted`. A bare rate limit stays
  `rate_limited`. Reset comes from `Retry-After` or
  `google.rpc.RetryInfo.retryDelay`; absent both, it is reported unknown.
- Provider env names (authoritative): `GEMINI_API_KEY`, `ZAI_API_KEY`,
  `MISTRAL_API_KEY` (all `*_API_KEY`), but `LLM7_API_TOKEN`. Each also has
  `*_BASE_URL`, `*_MODEL`, `*_READ_TIMEOUT_MS`.
- **E2E-proving the chain needs a TLS stub.** `OutboundHttpClient` is
  HTTPS-only, so a plain-HTTP stub fails with "Unsupported protocol" and a
  self-signed leaf with "SSL peer certificate ... not OK". Install the stub CA
  into the system trust store (`/usr/local/share/ca-certificates` +
  `update-ca-certificates`) rather than passing `CURL_CA_BUNDLE` to the server
  process; then point `*_BASE_URL` at `https://127.0.0.1:<port>/...` and read
  `/api/v1/news/quota` back over real HTTP. Remove the CA afterwards.
