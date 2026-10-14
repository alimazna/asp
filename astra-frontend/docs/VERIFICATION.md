# ASTRA web — verification record

Environment: Linux (cloud workspace), Node 24.21.0, npm 11.19.0, 270 packages
installed from the committed `package-lock.json` with `npm ci`.

Run everything again with:

```bash
npm run verify        # typecheck → tests → production build
npm run test:live     # needs the dev backend on 127.0.0.1:8790
```

## Executed

| Check | Command | Result |
|---|---|---|
| Dependency install | `npm ci` | **PASS** — 270 packages, lockfile unchanged |
| Typecheck (src + tests) | `tsc -b --force` | **PASS** — 0 errors, strict mode, `noUnusedLocals` |
| Unit/component tests | `vitest run` | **PASS** — 6 files, 69 tests |
| Live integration tests | `vitest run tests/live-mock.test.ts` (node env) | **PASS** — 6 tests against `scripts/mock_api.py` |
| Production build | `vite build` | **PASS** — `index.html` 0.70 kB, CSS 16.74 kB (4.26 kB gzip), JS 252.43 kB (75.90 kB gzip) |
| SPA served by dev server | `GET /` with preview `Host` header | **PASS** — HTTP 200, `text/html` |
| Dev API proxy | `GET /api/v1/health` via `:3000` | **PASS** — HTTP 200, frozen envelope forwarded |
| Module transform | `GET /src/main.tsx` | **PASS** — HTTP 200, `text/javascript` |
| ZIP integrity | `zipfile.testzip()` | **PASS** — no corrupt entries |
| ZIP completeness | required-file audit | **PASS** — 110 files, single root `astra-web/`, 12 screens, 10 test files, no `node_modules`/`dist`/`.env*`/`*.tsbuildinfo` |
| Extract → install → verify | `npm ci && npm run verify` in the extracted copy | **PASS** — 69 tests, clean typecheck, production build |

### Test files

- `tests/client.test.ts` (20) — URL composition, trailing-slash normalisation,
  envelope decode, `invalid/` protocol failures, flat `errors/` bodies,
  empty/unparseable bodies, network failure, timeout vs cancellation, bounded
  retry (5xx/network only, never on 4xx, never on writes), and the flat
  `POST /command` 202 body.
- `tests/honors.test.ts` (13) — score never becomes a probability, percentage
  only when `probability_calibrated`, `N/A — CALIBRATION NOT AUTHORISED`,
  null/undefined handling, UTC formatting, quality/freshness/service/health
  tones (unknown is never "up").
- `tests/components.test.tsx` (9) — DataCard loading/error/empty/stale states,
  retry wiring, chip tone classes, chart empty state, real OHLC readout.
- `tests/useApi.test.tsx` (6) — first-load spinner, no overlapping polls,
  staleness after a failed poll with previous data retained, disabled mode,
  visibility pause, manual refetch.
- `tests/app.test.tsx` (16) — unconfigured deployment banner, `SHADOW ONLY`
  always visible, sidebar/drawer/tab-bar navigation, only frozen v1 URLs are
  requested, `SCORE` label with no fabricated percentage, real candle series,
  canonical nine-timeframe order, history/shadow/bridge screens.
- `tests/command.test.tsx` (5) — actor attribution required, allow-listed
  command body, backend rejection surfaced, no live/execute/order control.
- `tests/live-mock.test.ts` (6) — all frozen route families decoded, nine
  timeframes in order, closed-bar OHLC series for every timeframe, score
  honesty end-to-end, legacy surfaces reported absent (404), read-only mock's
  `405` on the write path.

## Verified statically

- **Responsive breakpoints** exist at 768 / 900 / 1024 / 1366 px; the shell
  switches between sidebar (≥1024), drawer + tab bar (mobile) and a
  `max-width: 1560px` content column. Wide tables live inside `overflow-x:
  auto` wrappers (`.table-wrap`), so no page-level horizontal overflow.
- **No unprotected widths:** no `100vw` usage, no fixed page widths below
  375 px, `viewport-fit=cover` set for iOS notches.
- **No fabricated data paths:** every screen renders through `useApi` →
  `endpoints` → `client`; there are no literal price/level/probability values
  in `src/pages`. The only synthetic series in the tree is inside the dev-only
  `scripts/mock_api.py` (upstream, unmodified) and is labelled `DEMO DATA`
  whenever `VITE_DEMO_MODE=1`.
- **Backend untouched:** this project is a new directory (`astra-frontend/` inside the AURA repository). No
  file in `src/`, `bridge/`, `frontend/qt/` or the root build configuration of
  the AURA repository was modified, and nothing was committed or pushed.

## Still to confirm in a browser (not verifiable here)

This environment has no browser automation, so the following need a human:

1. Visual inspection at 375 / 390 / 768 / 1024 / 1366 / 1440 px (the dev server
   is running on port 3000 with the dev proxy and `DEMO DATA` labelling).
2. Chart interaction: wheel zoom, drag pan, touch pinch, crosshair readout.
3. Theme toggle persistence across reloads, and the drawer animation on a real
   touch device.
4. Sign-in/authorization behaviour once the external HTTPS gateway exists.
5. `POST /command` against the **real** backend (the mock is GET-only and
   answers `405`), including the `400 actor_required` and `403
   command_not_permitted` paths.

## Known environment-specific note

`tests/live-mock.test.ts` runs in the `node` Vitest environment on purpose:
jsdom's `AbortSignal` is not accepted by Node's undici `fetch`, which would
make the live test fail for harness reasons. The application code is unchanged
and uses standard browser `fetch`/`AbortSignal`.
