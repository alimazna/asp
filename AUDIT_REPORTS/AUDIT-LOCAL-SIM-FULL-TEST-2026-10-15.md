# Audit — Local Production Simulation: Full Endpoint Test & Hardening

Date: 2026-10-15
Scope: exhaustive live verification of the locally deployed ASTRA stack
(backend `astra_server`, frontend `astra-frontend`, Windows-sender protocol
client) and one production bug found + fixed.

Environment: Linux container; simulated Railway = `astra_server` on
`0.0.0.0:PORT`; simulated Cloudflare = static SPA served over ports mirrored
by the all-hands work-host proxies (work-1 -> 12000, work-2 -> 12001, HTTPS).

## 1. Stack under test

| Component | Artifact | Location |
|---|---|---|
| Backend | `build-linux/astra_server` (fresh build) | repo root |
| DB | SQLite `candles.sqlite` via `ASTRA_DATA_DIR` | `/tmp/astra_data` |
| Frontend | `astra-frontend/dist/` (built with `node build.js`, `BACKEND_BASE` env) | repo |
| Sender client | `astra_sender/protocol.py` (stdlib urllib) | `windows-sender/` |
| Tests | CTest 25 targets; sender unittest 17 | repo |

## 2. Result matrix (live, production-like mode)

| # | Test | Result |
|---|---|---|
| 6-1 | `GET /api/v1/health` | 200 OK |
| 6-2 | ingest no token / wrong token | 401 `unauthorized` (both) |
| 6-3 | CORS allowed origin (`localhost:3000`, `127.0.0.1:3000`) | 200 + `Access-Control-Allow-Origin` |
| 6-3 | CORS disallowed origin | 403, `code":"origin_not_allowed` |
| 6-4 | valid closed candle ingest | `stored_new`, `ingested:1` |
| 6-5 | identical candle replayed | `already_stored`, `ingested:0` |
| 6-6 | `GET /api/v1/sync/latest` (auth) | watermark `last_open_time_utc` correct |
| 6-6 | sync without auth | 401 |
| 6-6 | `GET /api/v1/candles?tf=` (public) | bars persisted, `closed_only:true` |
| 6-7 | server restart, same data dir | candle still served (durable) |
| 6-8 | 10 concurrent ingest threads | all 200, exactly 10 rows, 0 dup |
| 6-8 | stress 100 concurrent ingests, 5 symbols, multi-source | all 200, no transport errors, server healthy, 0 dup across PK |
| 8-1 | malformed JSON body | 400 `malformed_json` |
| 8-2 | empty body | 400 `malformed_request` |
| 8-3 | `candles` not an array | 400 `missing_candles` |
| 8-4 | **future candle timestamp (pre-fix)** | **BUG: accepted as `stored_new`** |
| 8-5 | **future candle timestamp (post-fix)** | `rejected`, reason `candle open_time is in the future`, `ingested:0` |
| 8-6 | empty candles array | 200 no-op, `ingested:0` |
| 8-7 | negative timestamp | `rejected`, `candle open_time must be a positive epoch timestamp` |
| 8-10 | empty symbol | 400 `missing_fields` |
| 8-11 | CORS allowlist matrix (2 allowed / 2 denied) | correct (never permissive) |
| E2E | frontend (work-1 HTTPS) → backend (work-2 HTTPS) | page renders `backend online`, real candle table, honest empty states |
| E2E | sender `ServerClient` over HTTPS | ingest `stored_new` → replay `already_stored` → sync watermark covers candle (all asserted PASS) |
| E2E | full system restart | health 200, persisted candles served after backend restart |

## 3. Bug found and fixed

**Bug:** `parseCandle()` in `src/api/AstraServerMain.cpp` accepted candle
`open_time` values far in the future (a fabricated/future record could enter
the store as a real candle).

**Fix** (`src/api/AstraServerMain.cpp`):
- reject `open_time > now + 300s` (clock-skew slack) with
  `candle open_time is in the future`;
- reject `open_time <= 0` with
  `candle open_time must be a positive epoch timestamp`.

**Regression test** (`tests/AstraApiServerTests.cpp`,
`astra_server_rejects_future_and_invalid_timestamps`): asserts future and
non-positive timestamps are `rejected` with `ingested:0` and that nothing
malformed enters the store (`GET /candles` honestly returns 503
`dependency_unavailable` while empty).

Result: backend still **25/25 CTest pass** (test target
`AstraApiServerTests` passes with the new testcase), sender **17/17 pass**.

## 4. HTTP status notes (contract)

- Ingest responses use HTTP 200 with a structured per-candle `state`
  (`stored_new` / `already_stored` / `rejected`) in the envelope. The sender
  protocol treats only an explicit server `stored_new`/`already_stored` as an
  acknowledgment — a bare 200 with no identity match is not an ack. This is
  the established, documented contract; changing the HTTP code to 201 was
  intentionally not done to avoid a backward-incompatible deviation.
- Empty store chart reads return 503 `dependency_unavailable` — honest
  unavailable state, matched by the frontend's degraded UI.

## 5. Files changed

- `src/api/AstraServerMain.cpp` — future/zero timestamp validation.
- `tests/AstraApiServerTests.cpp` — regression test (+`<cstdint>`, `<ctime>`).

## 6. Remaining (out of scope of this local simulation)

- Live Railway deployment: still blocked on `RAILWAY_TOKEN` (see
  `AUDIT-RAILWAY-DEPLOY-ATTEMPT-2026-10-15.md`).
- Live Cloudflare Pages deployment: still blocked on CF credentials (see same).
- Live MT5 / Windows host E2E: not reachable from this container; covered by
  fake-feed unit tests only.