# AUDIT-T39 — "Only 4 timeframes arrive": broker clock vs UTC (2026-10-19)

- **Owner report:** "it works and sends data, but only 4 timeframes..."
- **Repo HEAD at writing:** `5afe44d` (fix merged to `main` via PR #15)
- **Status:** fix implemented, tested, and merged; **server deployment BLOCKED**
  (no Railway credential — see §5)

## 1. Symptom on the live deployment

The public API showed exactly the reported pattern: the multi-hour timeframes
carried data while the intraday ones did not.

```
GET /api/v1/candles?tf=H4  -> 200, 1 bar (time 1792382400)
GET /api/v1/candles?tf=D1  -> 200, 1 bar (time 1792108800)
GET /api/v1/candles?tf=M1  -> 503 "no candle data available for M1"
```

`GET /api/v1/health` listed `timeframe-M1: no state observed`, `timeframe-M5: no
state observed`, `timeframe-M15: no state observed`, `timeframe-M30: no state
observed`, `timeframe-H1: no state observed`, while `timeframe-D1`, `timeframe-W1`
and `timeframe-MN1` reported "closed bar present".

The split is not arbitrary: **H4/D1/W1/MN1 are exactly the timeframes whose newest
closed bar opens far enough behind the wall clock to survive a raw-UTC comparison.**
M1..H1 open within the last few hours, so they looked future-dated.

## 2. Root cause

MT5 stamps every bar's open time in **broker server time**, not UTC. This broker
runs UTC+3 (measured offset 10800 s). Two places compared that broker-stamped time
against raw UTC:

1. `windows-sender/astra_sender/collector.py` — the sender's own future-guard
   dropped the bar locally with `candle open time is far in the future`
   (`MARKET_DATA_INVALID`), so it was never queued.
2. `src/api/AstraServerMain.cpp` — the ingest route's per-candle future guard
   (`openTimeUtc > nowUtc + 300`) rejected the same bars server-side, and the
   runtime's closure/freshness comparison ran in UTC as well.

The intraday bars were ~1.97 h (H1) to ~2.97 h (M1) ahead of UTC, i.e. beyond the
300 s skew slack, so they were discarded. Only bars older than the offset survived,
which is why roughly four timeframes arrived.

## 3. Fix (both halves of the protocol)

- **Sender measures the offset** from the newest M1 bar (`clock_offset_seconds`
  property on the collector) and evaluates its future guard in that frame.
- **Sender transmits** the measured value as `clock_offset_seconds` in the ingest
  body. The field is optional and additive, so an old server simply ignores it.
- **Server reads it**, clamps it to a plausible timezone band (|offset| <= 18 h;
  the real extreme timezone is UTC+14), and evaluates both the per-candle future
  guard and the runtime closure/freshness comparison in broker time.

The offset corrects the clock; it is **not** a licence to accept fabricated data.
A genuinely future bar is still rejected, and an implausible offset (a stale feed
rather than an exotic timezone) is ignored rather than trusted.

## 4. Evidence

| Check | Result |
|---|---|
| C++ suite (`ctest` in `build-verify`) | **26/26 passed** |
| `IngestContractTests` (incl. new broker-offset test) | passed |
| Sender suite (`python -m unittest discover -s tests`) | **81/81 passed** |
| Live ingest, all nine timeframes, real server + SQLite | each `stored_new`, 0 rejected |
| Replay of the same nine candles | each `already_stored`, 0 accepted (deduped) |
| Persistence | all nine read back via `/api/v1/candles` and present in `candles.sqlite` |
| Restart durability | server restarted; M1/H4/MN1 still readable |
| Genuinely future-dated bar (broker frame +2h) | rejected: `candle open_time is in the future` |
| Windows CI run `39235307853` | success; installer `AstraSenderSetup-0.4.8.exe`; frozen app `--selfcheck` exit 0, `bridge mt5_client: loaded` |

The strongest single check is the controlled A/B, which isolates the offset as
the cause rather than a coincidence:

```
WITHOUT offset: HTTP 200 accepted=0 rejected=1
   reason: candle rejected: candle open_time is in the future   <- the owner's symptom
WITH offset:    HTTP 200 accepted=1 rejected=0
ABSURD offset:  HTTP 200 accepted=0 rejected=1                  <- +30h ignored, not trusted
NO AUTH:        HTTP 401 (correctly refused)
```

The same candle bytes are rejected without the offset and accepted with it, so
the field — not any other change — is what recovers the intraday timeframes.

The two new sender tests were verified **not vacuous**: reverting the fix makes both
fail with the owner's exact error (`candle open time is far in the future`), and
they pass with it.

### Pre-existing fixture bug fixed in passing

`equals_form_cli_args_are_honored_and_bad_timeframe_is_rejected` carried M15
timestamps `1792065000/5900/6800`; `1792065000 % 900 == 300`, so the alignment gate
rejected them. This predates the offset work (the gate landed in the same commit as
the fixture, `257a6a1`, PR #10) and was corrected to `1792064700/5600/6500`.

## 5. Deployment status

### 5a. Live corroboration — H1 empty while H4 carries data

Re-checked on 2026-10-19 against the still-unreleased backend, the owner's own
data confirms the offset mechanism rather than merely being consistent with it:

```
M1  HTTP 503  no candle data available for M1
M15 HTTP 503  no candle data available for M15
H1  HTTP 503  no candle data available for H1
H4  HTTP 200  bar time=1792382400 (2026-10-19T04:00:00Z)
```

The H4 row is the diagnostic tell. Its open time is `04:00:00Z`, which is exactly
a four-hour boundary **in UTC**. The broker stamps H4 bars on the same 4-hour grid
shifted by +3h (i.e. 22:00, 02:00, 06:00, 10:00, 14:00, 18:00 UTC), so a bar the
broker opened at 07:00 UTC+3 (= 04:00Z) lands on `04:00Z` and passes the raw-UTC
check. An H1 bar opened at 07:00 UTC+3 (= 04:00Z) is the bar *currently forming*
in UTC terms, and the newest fully **closed** H1 bar the broker offers opens at
03:00Z — two hours ahead of the server's `now`, beyond the 300 s slack. So H1 is
always empty while H4 is sometimes populated.

That asymmetry — **H1 empty, H4 populated** — is the signature of a
broker-offset bug, not of a transport, auth, or subscription failure. A dropped
connection or a missing symbol would empty *all* timeframes at once; it would not
spare exactly the ones whose newest closed bar happens to fall more than the
offset behind the wall clock.

### 5b. Deployment

- **Sender:** rebuilt and published as an artifact by CI (v0.4.8). The installer
  preserves an existing `%PROGRAMDATA%\AstraSender\config.json` on upgrade
  (`onlyifdoesntexist`), so the owner's already-provisioned token is reused and the
  install needs no edits.
- **Backend:** the running Railway service still executes the pre-fix validation.
  Deploying the fix requires a Railway credential; **none is present in the agent
  environment** (checked env vars, `~/.railway`, `~/.config/railway`, the repo,
  `/run/secrets`, and prior conversation history). This is the same blocker recorded
  in `AUDIT-RAILWAY-DEPLOY-ATTEMPT-2026-10-15.md`; `railway up` reports
  "Not signed in."

  A one-click, confirmed workflow now exists for this
  (`.github/workflows/deploy-backend-railway.yml`). It was dispatched on `main`
  (run [39238449411](https://github.com/alimazna/asp/actions/runs/39238449411)) to
  prove the wiring: it stops at the credential gate with
  `::error Add RAILWAY_TOKEN (a Railway project token for astra-server) under
  Settings -> Secrets and variables -> Actions, then re-run.` and deploys nothing.
  Adding that one secret is the entire remaining blocker for the server half.

  The equivalent hand-run command, if preferred:

  ```
  RAILWAY_TOKEN=<token> railway up \
    --service astra-server --environment production --ci
  ```

  The project is already linked (`.railway/config.json`: project
  `bd2e8b02-9d95-4159-9891-c4d4f6914092`, environment
  `dc17b0a6-4e5a-47ea-bfb7-7f8e9404f8d5`). Durable storage is the Railway volume
  `astra-server-volume` at `/opt/astra/data`.

**Until the backend is redeployed the owner's symptom persists**, because the
server-side guard is the half that rejected the bars. Shipping the new sender alone
is not sufficient.

## 6. NOT VERIFIED

- Live MT5 attach and a real end-to-end delivery from the owner's Windows machine
  (no terminal in the agent environment).
- The fixed backend running on Railway (no credential to deploy).
- Cloudflare Pages redeploy (no Cloudflare credential; the current site is live and
  unchanged).
