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
  Production currently holds **zero** candles, so `/api/v1/candles` returns
  `503` and `readyz` returns `503` — the honest pre-data state. Genuine MT5
  data must arrive from the Windows sender.
- Admin inspection: `GET /api/v1/admin/candles?tf=...` (admin token only).
  `scripts/quarantine_admin.py list --all` flags misaligned rows and prints
  the exact purge command.
- Railway SSH needs interactive account auth, so the project token cannot open
  a shell in the container; use the admin HTTP routes instead.
