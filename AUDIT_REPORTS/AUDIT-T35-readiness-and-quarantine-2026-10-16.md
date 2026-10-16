# AUDIT-T35 — Remote-fed readiness + production quarantine cleanup

Date: 2026-10-16
Branch: `deploy/astra-stack`
Status: RESOLVED, deployed and verified in production

## Scope

Two related defects were found and fixed while bringing the deployed stack to a
truthful, production-usable state:

1. **Readiness deadlock (T34 work, verified here).** In remote-fed server mode
   the MT5 bridge process is not launched (`launchBridgeProcess=false`), yet
   `ready_` was only ever set by a local bridge handshake. A backend that
   received real candles from the Windows sender therefore reported
   `readyz = 503` forever, and the Railway probe (which pointed at `readyz`)
   restart-looped a healthy container.
2. **Unreadable contaminated series.** A single grid-misaligned row makes the
   guarded read path withhold its entire `(symbol, timeframe)` series. That is
   the correct anti-synthetic-data behaviour, but it left the operator with no
   way to see *which* row was poisoning the series — `/api/v1/candles` simply
   returned `503 dependency_unavailable`.

## Evidence — readiness

Local, with a real closed M15 candle accepted through the authenticated ingest
route:

| Moment | `readyz` |
|---|---|
| before any candle | `503`, `ready=false`, `startup_stage=DEGRADED` |
| after first accepted closed bar | `200`, `ready=true`, `startup_stage=DEGRADED` |

Production, after deploying the fix:

```
GET /api/v1/livez   -> 200   (process liveness; Railway probe)
GET /api/v1/readyz  -> 503   (no data yet — honest)
```

`mode` and `startup_stage` deliberately remain `DEGRADED`; only the boolean
readiness gate flips when decision-grade data exists.

## Evidence — contamination

Production `/api/v1/admin/candles?tf=M15` (new admin inspection view) listed
five rows, three of them misaligned on the M15 grid:

| source_id | open_time | `open_time % 900` | verdict |
|---|---|---|---|
| dedup-verify | 1792065000 | 300 | misaligned |
| railway-verify | 1792068271 | 871 | misaligned |
| railway-verify | 1792068276 | 876 | misaligned |
| live-e2e-verify | 1792068300 | 0 | aligned |
| prod-e2e | 1792160100 | 0 | aligned |

These were synthetic deployment-verification rows written by earlier agent
sessions, not MT5 data. The whole M15 series was withheld as a result, so
`/api/v1/candles?tf=M15` returned `503` even though `ready=true`.

All five rows were purged one at a time by full identity through the
authenticated admin route (`DELETE /api/v1/admin/candles`). Production now
reports:

```
GET /api/v1/admin/candles?tf=M15  -> count 0
GET /api/v1/candles?tf=M15        -> 503 "no candle data available for M15"
GET /api/v1/livez                 -> 200
GET /api/v1/readyz                -> 503
```

That is the honest state for a deployment that has not yet received genuine
MT5 candles.

## Changes

| File | Change |
|---|---|
| `src/persistence/CandleStore.h/.cpp` | `allRows()` — raw rows for a timeframe, without the alignment guard (the quarantine view) |
| `src/api/AstraApiServer.h/.cpp` | `adminListHandler`; `GET /api/v1/admin/candles?tf&limit` behind the admin credential |
| `src/api/AstraServerMain.cpp` | wires the admin list handler |
| `tests/CandleStoreTests.cpp` | `AllRowsExposesWithheldQuarantineRows`: guarded view hides the series, quarantine view shows both rows, purging the bad one restores the series |
| `tests/AstraApiServerTests.cpp` | admin GET is 200 for the admin token, 401 for the sender token (replaces the old "GET is 405" expectation) |

The public guarded read path is unchanged: no withheld or synthetic row can
reach the frontend through it.

## Safety properties preserved

- The admin credential is separate from the sender credential; a leaked sender
  token cannot reach the admin surface.
- The admin delete route remains single-row and full-identity only — there is
  no bulk delete.
- The sender credential is still required for ingestion and sync.

## Verification

- `ctest` — 26/26 suites pass.
- `windows-sender` pytest — 31/31 pass.
- Railway deploy of `e8bfc10` — SUCCESS.
- Production endpoints re-tested after purge (results above).
