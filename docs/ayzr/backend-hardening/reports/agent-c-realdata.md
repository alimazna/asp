# Agent C — Real XAUUSD candle QA & historical replay — report

Scope: `tools/ayzr_agents/real_candle_replay.py` (new),
`tests/RealCandleReplayTests.cpp` (new),
`docs/ayzr/backend-hardening/REAL_CANDLE_TESTING.md` (new). Shadow-only: the
workflow ingests and reads back market data against a **local** `astra_server`;
it never executes an order and never fabricates a price, candle, indicator or
analysis result. `astra-frontend/` and the Windows MT5 sender EXE were neither
read nor modified. No secrets were read or written; the local sender token is the
literal `real-replay-token`, not a repository secret. No live LLM7 call was made
(`ASTRA_LLM_PROVIDER=mock` throughout).

## 1. Files created / changed (exact paths)

Created:
- `tools/ayzr_agents/real_candle_replay.py` — reproducible replay workflow
  (checksum gate → OHLC QA → M1→M15/H4 aggregation → local ingest → idempotency
  → read-back comparison → restart persistence → provenance JSON; optional
  guarded-read scaling probe).
- `tests/RealCandleReplayTests.cpp` — 4 C++ cases reading the real CSV directly
  and driving the real `astra_server` binary over loopback.
- `docs/ayzr/backend-hardening/REAL_CANDLE_TESTING.md` — the workflow, the
  aggregation rule, the findings, and the honesty/scope notes.

Changed: none. The two owned new files and one new doc are the only additions;
no existing source, test or contract file was edited (`git status` shows only
untracked additions under the owned paths).

## 2. Corpus provenance (verified)

- Path: `research/data/xauusd_m1/xauusd_m1_real.csv` (committed, checksummed).
- sha256 (verified against `research/data/xauusd_m1/checksums.sha256` **before**
  any bar is used):
  `96db3014127609fa16e761b9e7e3ef1126a350fc44fe8d0ccc632fdc6902ce8b`
- Symbol `XAUUSD`; row count **100,008**; coverage **2026-06-24 11:08:00 ..
  2026-10-08 10:30:00** broker server time; format
  `timestamp,open,high,low,close,volume` (volume = MT5 tick volume).
- Timestamp convention: broker server time as written (no tz label), parsed to
  epoch seconds with `timegm` — the same convention the backend's
  `bridge/mt5_python/mt5_csv_feed.py` uses. The replay therefore sends
  `clock_offset_seconds: 0` (none was measured, so none is claimed).

## 3. Aggregation rule (the backend's own, not invented)

Read from `src/data/BarNormalizer.h` (bar shape) and
`bridge/mt5_python/mt5_csv_feed.py::CsvFeed._aggregate` (the bucket rule, real
engine code):

```
bucket = t - (t % interval)          # 900s M15, 14400s H4
open=first, high=max, low=min, close=last, tick_volume=sum
```

The tool imports the backend module with `FAKE_MT5_CSV_NO_SHIFT=1` and asserts
its M15/H4 buckets are byte-identical to the tool's own aggregator
(`backend_aggregation_cross_check.all_agree == true` in every run). Full-corpus
result: M15 = **6670** bars, H4 = **457** bars (independently derivable from the
distinct `t-(t%interval)` values).

## 4. Commands run

```bash
# checksum gate (independent of the tool)
sha256sum research/data/xauusd_m1/xauusd_m1_real.csv
# 96db3014127609fa16e761b9e7e3ef1126a350fc44fe8d0ccc632fdc6902ce8b

# build
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j4

# C++ real-data cases
cd build-linux && ./RealCandleReplayTests

# Python replay — full corpus, M15 + H4, plus guarded-read scaling probe
python3 tools/ayzr_agents/real_candle_replay.py --all --scaling \
    --server-log /tmp/srv.log --json-out /tmp/replay.json

# Python replay — trailing window with an M1 series longer than the API cap
python3 tools/ayzr_agents/real_candle_replay.py --bars 1500 --timeframes M1,M15

# full C++ suite
cd build-linux && for t in *Tests; do ./"$t"; done
cd build-linux && ctest
```

## 5. Exact test results

### `RealCandleReplayTests` (the new binary)

```
4 test case(s), 0 failed, 0 assertion failure(s)
RESULT: PASS
```
Cases (all PASS):
1. `RealCandleReplay_CorpusChecksumAndOhlcIntegrity` — checksum verified; 100,008
   rows; 0 unparseable; 0 OHLC violations; 0 off-grid; 0 non-ascending.
2. `RealCandleReplay_AggregationMatchesIndependentDerivation` — M15 = 6670,
   H4 = 457; every bucket grid-aligned and ascending; independent per-bucket
   re-derivation has 0 mismatches; partial final buckets recorded (last M15
   bucket 1 member, last H4 bucket 151 members).
3. `RealCandleReplay_EndToEndIngestIdempotentAndPersistent` — real M15/H4 window
   ingested (203/14 bars), replayed (203/14 duplicates, 0 new), read back with
   max abs price delta **0.0**, sync watermark exact, series survived a server
   restart.
4. `RealCandleReplay_ApiSeriesWindowFinding` — pins finding 6.1 below.

### Full C++ suite (no regressions)

```
ctest: 100% tests passed, 0 tests failed out of 31
per-binary: 31 binaries, 258 test cases, 0 failed, 0 assertion failures
(SingleInstanceTests uses its own printer: "Single-instance tests: all passed")
```
Baseline before this work was 30 binaries / 254 cases; the delta is exactly the
new `RealCandleReplayTests` (4 cases).

### Python replay workflow

- Full corpus, M15 + H4: **RESULT: PASS** (`ok: true`, no failures, no errors).
  - M15: 6670 aggregated, 6670 accepted, 6670 replay-duplicates, `ingested=0` on
    replay, 1000 returned (API cap — see 6.1), 0 rejected.
  - H4: 457 aggregated, 457 accepted, 457 replay-duplicates, 457 returned,
    0 rejected.
  - Restart persistence: both series survived intact (`survived: true`).
  - `backend_aggregation_cross_check.all_agree: true`.
- Trailing M1 window (>1000 rows): **RESULT: PASS**, with the truncation finding
  recorded (`api_truncated: true`, `api_newest_closed_time_is_newest_stored:
  false`).

## 6. Findings (real-data observations; no routes were changed)

### 6.1 `/api/v1/candles` serves the OLDEST `limit` rows (contract defect)

`CandleStore::recent` is `… ORDER BY open_time ASC LIMIT ?`, and the route
reports `newest_closed_time = candles.back().openTimeUtc`. For a series longer
than the route cap (1000) the client gets the **oldest** 1000 rows and the
reported "newest" is not the newest stored candle. On the real full corpus
(M15 = 6670 stored rows): returned `open_time` 1782298800…, reported
`newest_closed_time = 1783632600`, true newest stored `1791455400` — **8692 M15
candles (~145 h) stale**. `sync/latest` is unaffected (uses `MAX(open_time)`).
Pinned by `RealCandleReplay_ApiSeriesWindowFinding`. Fix (not applied — outside
this scope) is to select the newest N (e.g. `ORDER BY open_time DESC LIMIT ?`
then re-ascend) or to report the store's true `MAX(open_time)`.

### 6.2 The guarded read path is quadratic in series length (DoS characteristic)

The `NOT EXISTS` alignment guard in `CandleStore` makes guarded reads O(n²). On
real M1 data (one source, fresh store per size), measured `sync/latest` latency:

| Stored M1 rows | sync/latest | /api/v1/candles (limit 1000) |
|---|---|---|
| 2,071 | 0.8 s | 0.3 s |
| 4,111 | 3.3 s | 0.6 s |
| 8,071 | 8.9 s | 1.0 s |
| 16,227 | 35.0 s | 2.0 s |

Ingest of the full 100,008 M1 rows takes ~0.3 s; the guarded read of that same
series is minutes. Not a data-integrity failure (the guard still withholds a
contaminated series), but a read-path scaling issue worth a non-correlated
rewrite. The full-corpus replay therefore defaults to M15/H4 and makes M1
opt-in; `--scaling` reproduces the table.

### 6.3 Partial final buckets are real, not corruption

The corpus ends mid-H4 (10:30 broker time), so the last M15 (1 member) and H4
(151 members) buckets are partial. Recorded explicitly; never asserted complete.

## 7. Reproducibility notes / unresolved issues

- **Zombie `astra_server` children in this environment.** The workflow was
  initially killed by a signal directed at the parent's process group (the main
  server logged a clean `shutdown requested`, i.e. it received SIGTERM) during
  the scaling probe. The tool now starts each server with
  `start_new_session=True` (its own session/process group), which removes the
  interference; verified by two clean full-corpus runs. Separately, ~23 defunct
  `astra_server` processes were already present in the shared container before
  this work (reaped only when their parent exits) — a harness hygiene issue, not
  a product one. The C++ `ProcessSupervisor` already reaps its own children.
- **`sync/latest` on the full M1 series is not exercised in the default run**
  (6.2): a 100k-row guarded read is minutes. The scaling probe characterizes the
  same code path on real data up to 16,227 rows instead.
- No production system was contacted; no data was deleted; nothing was deployed
  or pushed.
