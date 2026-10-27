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
- **2026-10-26 (verified live):** the deployed container now runs the
  post-`f92075e` ingest guard. `/api/v1/health` reports all **nine** timeframes
  with a closed bar, so the owner's "only 4 timeframes" symptom is resolved in
  production. The store still holds only the candles the sender actually pushed
  (M1/M5/M15/M30/D1/W1/MN1 had a single closed bar; H1 nine; H4 four) — the
  system never fabricates coverage.
- **2026-10-26 (open gap):** the deployed container does **not** yet run the
  analysis-bus rehydration from `606aa74` **nor the two honesty fixes that
  followed it** (`0ee0817` symbol reporting, `cd586bd` health publication), so
  `/api/v1/analysis/latest` still reports `mtf_core_gate_passed:false` with
  `block_reasons:["multi-timeframe core gate: M15 and H4 streams unavailable"]`
  even though both streams are present in the durable store. The gate needs
  three closed bars on each of M15 and H4; the in-memory bus starts empty on
  every container start and is only fed by live ingestion. Until the rehydration
  commit is deployed, a fresh container serves no objective until live candles
  accumulate on their own.
- **Deploy blocker (confirmed 2026-10-26 by dispatching the workflow):** all
  three deploy workflows stop at their credential preflight. The Railway run
  fails at "Fail with an actionable message if the token is missing"
  (`RAILWAY_TOKEN` absent); the Cloudflare runs fail at "Preflight — Cloudflare
  credentials present" (`CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` absent).
  No token exists in the agent environment and there is no `~/.railway`, so a
  deploy cannot be performed from here. Owner must add the three repository
  secrets, then re-run the workflows on `feat/standalone-collector`.
- **Re-verified live 2026-10-27:** the deployed container still serves
  `analysis.symbol:""` and `mtf_core_gate_passed:false` with
  `block_reasons:["multi-timeframe core gate: M15 and H4 streams unavailable"]`.
  The rehydration commits `606aa74`, `0ee0817`, `cd586bd` are all present on
  `feat/standalone-collector`, so the remaining work is a deploy, not a code
  change. Still blocked on `RAILWAY_TOKEN`. Health and readiness both answer:
  `/api/v1/health` and `/api/v1/readyz` return 200 (`ready:true`,
  `startup_stage:DEGRADED`); the readiness route is `/api/v1/readyz`, **not**
  `/readyz` or `/api/v1/ready`.
- **Local verification of the un-deployed fixes (2026-10-26):** fresh ingest
  into an empty data dir, then restart with no new ingestion, yields
  `analysis.symbol:"XAUUSD"`, `mtf_core_gate_passed:true`, and a `/health` that
  reports `observed_at` as a real timestamp with fault-accurate per-timeframe
  reasons. Asserted by `RuntimeRehydrationTests`; 27/27 CTest pass.
- `readyz` reports `ready:true` with `mode:DEGRADED`, which is the honest state
  for a backend with no bridge.
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

## Windows collector packaging (2026-10-27, release collector-v1.0.2)

The packaged collector shipped twice unable to read a single candle. Both faults
were in the bundle, not the collection logic, and neither is visible from a
successful `PyInstaller` exit code:

- **`numpy` in `excludes` breaks the bundled `MetaTrader5`.** The spec bundled
  `MetaTrader5` as a hidden import but listed `numpy` under `excludes`, and
  excludes wins over hiddenimports. The frozen exe then failed with
  `ImportError: numpy._core.multiarray failed to import` on the owner's first
  read. The sender spec never excluded numpy for exactly this reason. A spec
  that bundles a package must not exclude that package's dependencies.
- **A windowed exe turns an unhandled exception into a blocking dialog.** With
  `console=False` and `disable_windowed_traceback=False`, an escaping exception
  opens a native message box that waits for a click. In a headless run nobody
  clicks it, so the job hung for 25 minutes until cancelled. The collector now
  routes `sys.stdout`/`sys.stderr` to `console.log` in the data dir when they are
  `None`, and `ASTRA_COLLECTOR_NO_DIALOG=1` converts an escaping exception into a
  logged non-zero exit (mirroring the sender's `ASTRA_SENDER_NO_DIALOG`).
- **Diagnostics must be written, not only printed.** A windowed exe has no
  stdout, so `--selfcheck` writes `selfcheck.txt` in the data dir. CI reads that
  file and asserts a positive `ok   MetaTrader5`, because "no FAIL line" also
  passes for an exe that dropped MT5 entirely.
- **Bound every headless smoke run with a timeout.** `WaitForExit(ms)` plus an
  explicit "did not exit (a blocking dialog?)" error turns a future hang into a
  one-minute failure instead of burning the job.
- **`upload-artifact` stores paths relative to the uploaded file's parent**, so a
  release job that assumes `dist/ASTRA-Collector.exe` fails even though the build
  and smoke test passed. The release job now searches for the file and prints a
  listing when it is absent.
- Tests that pass on one host and fail on another usually branch on a missing
  dependency: the selfcheck test passed locally (no tkinter, so it read stderr)
  and failed in CI (tkinter present, so stdout was empty). Assert the contract
  that holds on both hosts.

## Collector GUI: verify before saving, connect on demand (v1.0.4+)

The owner-facing window had two design faults that only showed up when someone
who is not the author used it. Both are worth preserving as rules.

- **A "test" that tests the wrong thing is worse than no test.** The setup
  button originally confirmed the URL via `/health`, which is unauthenticated on
  the real server by design. It returned 200 for a wrong token, so the dialog
  saved a configuration that could not deliver a single candle. The fix is
  `ReceiverClient.verify_token()`, which calls the authenticated
  `/api/v1/sync/latest`. Whenever a check is meant to prove a credential, it
  must hit a route that requires that credential.
- **The test double must not be more permissive than the server.** `FakeReceiver`
  required a token on `/health` while the real server does not. A client that
  never sent one would pass the double and fail in production. When the two
  disagree, the double is wrong — check the C++ handler
  (`src/api/AstraApiServer.cpp`) rather than assuming.
- **Nothing connects on its own.** The MT5 connection is a separate button. A
  probe that silently attaches on startup hides a misconfigured terminal and
  makes the window's own status meaningless.
- **Never call Tk's `after()` from a worker thread.** It raised "main thread is
  not in main loop" under load. Worker results go through a `queue.Queue`
  drained by `_drain_events` on the main thread. This is the pattern to follow
  for any new background work.
- **`/sync/latest` without `?source_id=` is an aggregate across senders and is
  not delivery proof for any one sender.** The C++ handler says so in a comment.
  Use it to prove a token; use the scoped form to reconcile a ledger.
- GUI logic tests must not construct a window: the Linux CI job in
  `collector-windows-build.yml` does not install tkinter. Test the pure helpers,
  and drive the real window under `xvfb-run` locally instead.

## A state change the user cannot see is not a state change (v1.0.5+)

Two more faults, both found by driving the real window rather than reading it.
Neither is visible in a diff, which is why they survived review.

- **Failure rendered as the idle text.** `_on_probe_fail` set the state to
  `DISCONNECTED`, whose label is "Terminal: not connected" — exactly what was
  shown *before* the click. The owner pressed **Connect to MT5**, the button
  greyed, then everything returned to how it looked before, with no reason
  given. A failure must produce text the user did not already have. It now sets
  `FAILED` and carries the reader's own message.
- **A colour map silently has no default for a new state.** Adding `FAILED` was
  not enough: `CONNECTED`, `CONNECTING`, `NO_DATA` and `DISCONNECTED` were
  absent too, so every one of them fell through to the neutral grey — a
  *successful* connection looked the same as a stopped one. When adding a state,
  add it to `STATE_COLOURS` as well as the text map, and assert the colour is
  not the fallback.
- **Keep the mapping out of the widget code.** It now lives in
  `display.probe_text`, a pure function, so the rule "an unknown state never
  reads as connected" is a unit test that runs on Linux CI with no display.
- **Cancel `after()` timers from a `<Destroy>` binding.** Closing the main window
  while the setup dialog was open left a pending timer that fired against the
  destroyed widget and printed `invalid command name "..._drain"` into the
  owner's log. `_close` alone is not enough: the *parent* can be destroyed
  first, so the binding is what makes it hold in both directions.

## The collector's wire contract must be tested against the real server

The collector shipped twice (v1.0.0, v1.0.2) unable to deliver **a single
candle**, while its own suite reported 99 passing tests. Three independent
wire-contract bugs, each invisible to the test double:

1. **No `source_id`.** The server validates the batch envelope before reading
   any candle and answers `HTTP 400 missing_fields`. The double never required
   the field, so the client never sent it.
2. **The open time was sent as `open_time_utc`.** `parseCandle` reads `time`.
   The server accepts the batch (HTTP 200) and returns every candle in its
   `rejected` array with `ingested: 0` — a *silent* failure, the worst kind,
   because a sender that only checks the status code reads it as success and a
   sender that requires a named ack reads it as "nothing confirmed" forever.
3. **No `clock_offset_seconds`.** Same broker-time trap as the sender (below),
   but the collector measured the offset and then dropped it before sending, so
   every intraday bar from a UTC+2/+3 broker was rejected as future-dated.

The lesson is not "be careful"; it is that **a hand-written test double is a
second implementation of the contract, and it will drift.** A double that is
more permissive than the server converts real production failures into green
tests. The double now enforces the same envelope check, reads `time`, and
applies the broker-frame future guard, and `tests/test_core.py::RealServerContractTest`
drives the actual C++ `astra_server` binary (skipped when no build is present).
When you change an endpoint, extend that class — do not only extend the double.

Corollary: a passing `HTTP 200` is not delivery. The only proof is a per-candle
ack naming the candle (`stored_new` / `already_stored`). The collector is
already built that way; the tests now prove it against the real server.

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

