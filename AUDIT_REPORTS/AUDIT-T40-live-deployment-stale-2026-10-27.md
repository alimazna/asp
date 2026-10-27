# AUDIT-T40 — Live deployment still serves the pre-fix build (2026-10-27)

## Summary

The three backend fixes committed on this branch (`606aa74`, `0ee0817`,
`cd586bd`) are correct and verified against the real `astra_server` binary. The
**deployed** Railway service does not have them. It still serves the exact
symptoms those commits remove.

Nothing here is a code defect. It is a deployment gap, and it is reported
because a green CI run on this branch does **not** mean the fix is live.

## Evidence: the deployed service

Probed 2026-10-27 against `https://astra-server-production.up.railway.app`.

```
$ curl -s https://astra-server-production.up.railway.app/api/v1/health
{"api":"v1","schema":"1.0","data":{"aggregate":"DEGRADED",
 "decision_grade_data":false,
 "degraded_reasons":["timeframe-D1: closed bar present",
                     "timeframe-H1: closed bar present",
                     ... all nine timeframes ...],
 "observed_at":1792429170980, ...}}
```

```
$ curl -s https://astra-server-production.up.railway.app/api/v1/analysis/latest
... "symbol":"", "context":{"regime":"UNKNOWN"}, ...   (no streams, no data_quality)
```

Two distinct faults, both the ones the commits address:

1. `degraded_reasons` carries the sentence **"closed bar present"** — a phrase
   that reads as healthy — for all nine timeframes, instead of naming the fault.
   `cd586bd` replaced it with `no closed bar` / `... but not decision-grade`.
2. `symbol` is the empty string for real XAUUSD candles. `0ee0817` added the
   fallback to the pipeline symbol.

`observed_at` is a real epoch millisecond value, so the STARTING-stuck variant
that `cd586bd` also fixed is not what is deployed here.

## Evidence: the committed build, run locally

The same binary CI builds (`build-linux/astra_server`, `27/27` CTest green,
including `RuntimeRehydrationTests`) was started against an empty data
directory, fed two real closed M15 candles, replayed with the identical batch,
then restarted with **no** new ingestion.

| Step | Result |
| --- | --- |
| Fresh start, no data | `aggregate: STARTING`, `observed_at: -1`, `reasons: []` |
| Ingest two closed M15 candles | `state: stored_new` for both, `ingested: 2` |
| Replay the identical batch | `state: already_stored` for both, `ingested: 0` |
| **Restart, no new ingestion** | `symbol: "XAUUSD"` (production: `""`) |
| Health after rehydration | `observed_at: 1793142395959` (not `-1`) |
| `degraded_reasons` after rehydration | `timeframe-M15: closed bar present but not decision-grade (UNKNOWN)`, and `no state observed` for the other eight |
| Candles stored after the replay | `2`, not `4` |

The last row is the duplicate-prevention guarantee: the uniqueness constraint on
`symbol + timeframe + open_time` held across a process restart, and the replay
did not create second records.

## Why the deployment is blocked

Both deploy workflows stop at their credential preflight. Neither prints a
credential value.

```
$ gh run view 40474182680 --log-failed
##[error]Add RAILWAY_TOKEN (a Railway project token for astra-server) under
Settings -> Secrets and variables -> Actions, then re-run.

$ gh run view 40475146315 --log-failed
##[error]Add CLOUDFLARE_API_TOKEN (Cloudflare Pages: Edit) and
CLOUDFLARE_ACCOUNT_ID under Settings -> Secrets and variables -> Actions,
then re-run this workflow.
```

Checked in the agent environment: `RAILWAY_TOKEN`, `CLOUDFLARE_API_TOKEN` and
`CLOUDFLARE_ACCOUNT_ID` are all absent. `.railway/config.json` holds project and
environment IDs only — no credential.

## What is confirmed working in production today

- `https://astra-dia.pages.dev` serves the frontend (`HTTP 200`), and its
  `config.js` points at the Railway backend.
- CORS is correctly allowlisted: `Origin: https://astra-dia.pages.dev` is echoed
  in `access-control-allow-origin`; `Origin: https://evil.example.com` gets `403`.
- Ingestion is authenticated: `/api/v1/candles/ingest` and `/api/v1/sync/latest`
  both return `401 {"code":"unauthorized"}` with no token **and** with a bogus
  token.
- The frontend bundle contains no credential-shaped content.
- `/api/v1/candles` returns a real XAUUSD bar (`open 4186.59`, `close 4182.46`,
  `tick_volume 4833`, `spread 12`) whose `time` is `1792405800` — the same stamp
  `/api/v1/analysis/latest` reports. The sender path therefore works end to end;
  only the analysis presentation is degraded. (The response held one bar, not a
  nine-timeframe set.)

## Resolution

Add `RAILWAY_TOKEN`, `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` as
repository secrets, then re-run `deploy-backend-railway.yml` against this branch.
After the deploy, re-probe `/api/v1/analysis/latest` and confirm `symbol` is
`XAUUSD` and no `degraded_reasons` entry reads `closed bar present`.
