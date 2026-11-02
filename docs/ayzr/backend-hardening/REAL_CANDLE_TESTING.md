# AYZR — Real XAUUSD Candle QA & Historical Replay

Owner: Agent C (Real XAUUSD candle QA & historical replay), backend-hardening
round. Shadow-only: this workflow ingests and reads back market data; it never
executes an order and never fabricates a price, candle, indicator or analysis
result.

## 1. What this is

Two artefacts exercise the receiving backend against the **genuine** MT5 broker
export committed in the repository:

| Artefact | Role |
|---|---|
| `tools/ayzr_agents/real_candle_replay.py` | Reproducible replay workflow: checksum gate, OHLC QA, M1→M15/H4 aggregation, POST to a local `astra_server`, idempotency + read-back + restart persistence, provenance record. |
| `tests/RealCandleReplayTests.cpp` | The same guarantees as a C++ test case set, reading the CSV directly and driving the real `astra_server` binary over loopback. |

Neither artefact talks to production. The Python tool refuses a non-loopback
`--server-url`; the C++ test starts `astra_server` bound to `127.0.0.1` on a
free port. Both force `ASTRA_LLM_PROVIDER=mock`, so no live LLM call is ever
made.

## 2. The corpus (real data — do not relabel)

- Path: `research/data/xauusd_m1/xauusd_m1_real.csv` (committed, checksummed).
- Format: `timestamp,open,high,low,close,volume` (`volume` = MT5 tick volume).
- Rows: **100,008** (excluding the header line).
- Coverage: **2026-06-24 11:08:00 .. 2026-10-08 10:30:00** (broker server time).
- Timestamp convention: the CSV carries **no timezone label**; timestamps are
  broker server time as written and are parsed to epoch seconds with `timegm`
  (the same convention `bridge/mt5_python/mt5_csv_feed.py` uses). The replay
  therefore sends `clock_offset_seconds: 0` — no offset was measured, so none is
  claimed. Every bar is historical relative to wall-clock, so the future guard is
  not offset-sensitive here.
- sha256 (verified against `research/data/xauusd_m1/checksums.sha256` before
  any bar is used):
  `96db3014127609fa16e761b9e7e3ef1126a350fc44fe8d0ccc632fdc6902ce8b`

The corpus is the operator's MT5 export; its own quality report
(`research/data/xauusd_m1/QUALITY.md`) records 0 OHLC violations, 0 duplicates,
0 off-grid timestamps and a price-band WARN (real 2026 gold levels, not a
defect). The replay re-verifies the hard checks rather than trusting the report.

## 3. Aggregation rule (the backend's, not an invented one)

M1 → M15/H4 uses **timeframe-boundary bucketing**, which is the rule the backend
already ships in `bridge/mt5_python/mt5_csv_feed.py::CsvFeed._aggregate`
(real engine code, used by the packaged bridge's `MetaTrader5` shim):

```
bucket = open_time - (open_time % interval)      # interval: 900s M15, 14400s H4
open       = first M1 open in the bucket
high       = max of the M1 highs
low        = min of the M1 lows
close      = last M1 close
tick_volume= sum of the M1 tick volumes
```

The bar shape mirrors `src/data/BarNormalizer.h` (`Bar`). The replay's own
aggregator is cross-checked against the backend module: `real_candle_replay.py`
imports `mt5_csv_feed` with `FAKE_MT5_CSV_NO_SHIFT=1` (so the corpus is not
time-shifted) and asserts its M15/H4 buckets are byte-identical to the tool's.
Disagreement is a failure.

On the full corpus this yields (independently derivable: the distinct values of
`t - (t % interval)`):

| Timeframe | Bars | First open (epoch) | Last open (epoch) | Last bucket complete? |
|---|---|---|---|---|
| M15 | 6670 | 1782298800 | 1791455400 | no — 1 M1 member (corpus ends mid-bucket) |
| H4  | 457  | 1782288000 | 1791446400 | no — 151 M1 members (of 240) |

The partial final bucket is a property of the **real corpus** (it ends at
10:30 broker time, mid-H4). The tool records `last_bucket_m1_count` and
`last_bucket_complete` and never asserts the final bar is complete.

## 4. Workflow

```bash
# Build
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j4

# C++ real-data test cases (also part of the standard suite)
cd build-linux && ./RealCandleReplayTests

# Python replay, trailing window (default 4000 M1 bars, snapped to an H4 boundary)
python3 tools/ayzr_agents/real_candle_replay.py --json-out /tmp/replay.json

# Full corpus, M15 + H4 (fast), plus a guarded-read scaling probe
python3 tools/ayzr_agents/real_candle_replay.py --all --scaling --json-out /tmp/replay_full.json

# Opt in to a full-corpus M1 read (slow — see §6)
python3 tools/ayzr_agents/real_candle_replay.py --all --timeframes M1,M15,H4
```

Steps the tool performs, in order:

1. **Checksum gate.** sha256 the CSV and compare to `checksums.sha256`. A
   mismatch aborts before any bar is used.
2. **OHLC QA.** `low <= open <= high`, `low <= close <= high`, all prices > 0,
   strictly ascending, unique, minute-aligned. Defects are counted and reported,
   never repaired.
3. **Aggregation** (rule above) plus the backend-module cross-check.
4. **Ingest.** Start a loopback `astra_server` with `ASTRA_SENDER_TOKEN`, POST
   real batches (≤500 candles) to `POST /api/v1/candles/ingest`.
5. **Idempotency.** Replay every batch; every candle must come back
   `already_stored` and `ingested` must be 0.
6. **Read-back.** `GET /api/v1/candles?tf=…&limit=1000`; every returned bar must
   equal the real bar that was sent (prices, volume and open time), and
   `GET /api/v1/sync/latest?source_id=…` must report the true count and watermark.
7. **Restart persistence.** Stop the process, start a fresh one over the same
   data dir, and re-read; the series must be identical and the watermark
   unchanged.
8. **Provenance.** Emit symbol, timeframes, date range, row counts, timestamp
   convention, checksum and aggregation rule to `--json-out`.

## 5. What the checks prove

- The genuine corpus is intact (checksum + 0 OHLC/sequence defects over 100,008
  rows).
- M1→M15/H4 aggregation matches the backend's own rule exactly.
- The ingest route accepts real, grid-aligned XAUUSD bars and rejects nothing
  from the real corpus.
- Duplicate-batch replay is idempotent (`already_stored`, `ingested == 0`).
- Stored candles match the API responses field for field (max abs price delta
  0.0 at the API's 10-significant-digit precision).
- The store survives a server restart; both the served series and the sync
  watermark are unchanged.
- `sync/latest` count and watermark equal the real aggregated series.

## 6. Findings (real-data observations, not fixes)

These were observed while exercising the real corpus. They are recorded for the
integration owners; this document does not change the routes.

### 6.1 `/api/v1/candles` serves the OLDEST `limit` rows

`CandleStore::recent` (`src/persistence/CandleStore.cpp`) is
`… ORDER BY open_time ASC LIMIT ?`. The route then reports
`newest_closed_time = candles.back().openTimeUtc`. For a series longer than the
route's `limit` cap (1000), the served slice is the **oldest** 1000 rows, so:

- the client receives the oldest window, not the most recent one;
- `newest_closed_time` is the newest of the *returned* window, not the newest
  **stored** candle.

Observed on real data (full corpus, M15 = 6670 stored rows, `limit=1000`):
the route returned the first 1000 bars (`open_time` 1782298800…) and reported
`newest_closed_time = 1783632600`, while the true newest stored M15 open time is
`1791455400` — the reported "newest" is **8692 M15 candles (~145 hours) stale**
relative to the store. `tests/RealCandleReplayTests.cpp`
(`RealCandleReplay_ApiSeriesWindowFinding`) pins this with a real 1231-row M1
series (reported newest `1791441540` vs. true newest `1791455400`, a 231-candle
gap) and reports it as `[FINDING]`. `sync/latest` is unaffected: it uses
`MAX(open_time)` and reports the true watermark.

### 6.2 The read guard is quadratic in series length

Every guarded read (`CandleStore::recent`, `syncPoints`, `syncPointsForSource`,
`latestOpenTimeAnySource`) appends a correlated `NOT EXISTS (SELECT 1 FROM
candles bad …)` predicate (`cleanSeriesExpr()` / `cleanSourceSeriesExpr()`).
On a real M1 series the measured `GET /api/v1/sync/latest` latency scales
quadratically (real data, one source, fresh store per size):

| Stored M1 rows | sync/latest | `/api/v1/candles` (limit 1000) |
|---|---|---|
| 2,071 | 0.7 s | 0.4 s |
| 4,111 | 2.2 s | 0.5 s |
| 8,071 | 9.6 s | 1.0 s |
| 16,227 | 38.6 s | 3.0 s |

Extrapolated, the full 100,008-row M1 series is on the order of tens of minutes
per guarded read (the full-corpus run in §4 therefore defaults to M15/H4 and
makes M1 opt-in). Ingest itself is fast (100,008 M1 rows ≈ 0.3 s). This is a
performance/DoS characteristic of the current guard SQL, not a data-integrity
failure — the guard's correctness (withholding a whole contaminated series) is
unchanged. A non-correlated rewrite (e.g. a per-`(symbol,timeframe)` alignment
flag maintained on write) would keep the guarantee without the quadratic scan.

### 6.3 Partial final buckets are real, not corruption

The corpus ends mid-H4, so the last M15 (1 member) and H4 (151 members) buckets
are partial. The replay records this explicitly and never treats the truncated
bucket as a complete bar.

## 7. Totals

See `docs/ayzr/backend-hardening/reports/agent-c-realdata.md` for the exact
commands and pass/fail totals observed on the verification date.

## 8. Scope / honesty

- No order execution; shadow only.
- No live LLM7 calls (`ASTRA_LLM_PROVIDER=mock`).
- No secrets are read or written; the sender token used locally is a literal
  `real-replay-token`, not a repository secret.
- `astra-frontend/` and the Windows MT5 sender EXE were neither read nor
  modified.
- All expected values come from the real corpus or from an independent
  re-derivation of it. Nothing is invented.
