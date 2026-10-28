# AYZR backend performance results (Agent E)

Measured locally against a real `astra_server` process on
loopback, ingesting the committed REAL MT5 corpus. Only
measured values appear below; nothing is extrapolated.

## How to reproduce

```bash
# 1. build
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j4

# 2. full benchmark (starts its own astra_server on a free loopback port,
#    mock LLM provider, rate limits disabled, batch cap raised to 2000)
python3 tools/ayzr_agents/bench_backend.py --m1-full \
    --out docs/ayzr/backend-hardening/PERFORMANCE_RESULTS.md \
    --json-out /tmp/perf.json

# 3. quick smoke (fewer repetitions, smaller corpus slice)
python3 tools/ayzr_agents/bench_backend.py --quick --out /tmp/quick.md
```

The script verifies the corpus sha256 against the committed
`research/data/xauusd_m1/checksums.sha256` before measuring, refuses a
non-loopback host, and spends no external LLM quota. Every number below is a
measured value; the wall-clock repetition count is chosen adaptively from a
probe request so a slow path is measured with fewer reps rather than being
reported as a timeout (see the `adaptive_reps` field in the JSON output).

## Environment

| Field | Value |
|---|---|
| os | `Linux-6.8.0-1055-gke-x86_64-with-glibc2.41` |
| kernel | `6.8.0-1055-gke` |
| python | `3.13.15` |
| nproc | `4` |
| mem_total_mib | `15990.5` |
| server_binary | `/workspace/wt/agent-e/build-linux/astra_server` |
| server_sha256 | `8af98923f61a45469328468dcc0071aa8c33befea37a36419f4112f34a25fc86` |
| started_at_utc | `2026-10-27T18:03:07+00:00` |
| server env overrides | `{"ASTRA_LLM_PROVIDER": "mock", "ASTRA_INGEST_RATE_PER_MIN": "0", "ASTRA_SYNC_RATE_PER_MIN": "0", "ASTRA_MAX_CANDLES_PER_BATCH": "2000"}` |

## Data

| Field | Value |
|---|---|
| corpus | `/workspace/wt/agent-e/research/data/xauusd_m1/xauusd_m1_real.csv` |
| corpus sha256 | `96db3014127609fa16e761b9e7e3ef1126a350fc44fe8d0ccc632fdc6902ce8b` |
| matches committed checksums.sha256 | `True` (expected 96db3014127609fa, measured 96db3014127609fa) |
| M1 bars loaded | 100008 |
| M1 bars used | 100008 |
| M15 bars aggregated (complete buckets only) | 6665 |
| H4 bars aggregated (complete buckets only) | 301 |
| first M1 bar (UTC) | 2026-06-24 11:08:00 |
| last M1 bar (UTC) | 2026-10-08 10:30:00 |
| M1 rows stored after sweep | 100000 |
| M15 bars ingested | 6665 |
| H4 bars ingested | 301 |
| sync/latest | [{'symbol': 'XAUUSD', 'timeframe': 'M1', 'last_open_time_utc': 1791454920}, {'symbol': 'XAUUSD', 'timeframe': 'M15', 'last_open_time_utc': 1791454500}, {'symbol': 'XAUUSD', 'timeframe': 'H4', 'last_open_time_utc': 1791432000}] |

## Results

| Scenario | Requests | Conc. | Errors | min ms | mean ms | p95 ms | max ms | wall ms | req/s |
|---|---|---|---|---|---|---|---|---|---|
| read_M1_500_at_10000_rows | 3 | 1 | 0 | 595.83 | 642.80 | 701.90 | 701.90 | 1928.6 | 1.6 |
| read_M1_500_at_40000_rows | 3 | 1 | 0 | 2341.59 | 2563.02 | 2960.39 | 2960.39 | 7689.2 | 0.4 |
| read_M1_500_at_70000_rows | 3 | 1 | 0 | 4268.84 | 4381.48 | 4484.84 | 4484.84 | 13144.6 | 0.2 |
| read_M1_500_at_100000_rows | 3 | 1 | 0 | 7139.88 | 7907.86 | 8636.87 | 8636.87 | 23723.7 | 0.1 |
| ingest_batch_100 | 1 | 1 | 0 | 2.86 | 2.86 | 2.86 | 2.86 | 3.2 | 310.9 |
| ingest_batch_500 | 1 | 1 | 0 | 8.90 | 8.90 | 8.90 | 8.90 | 10.2 | 98.0 |
| ingest_batch_2000 | 1 | 1 | 0 | 32.46 | 32.46 | 32.46 | 32.46 | 37.2 | 26.9 |
| ingest_duplicate_500 | 3 | 1 | 0 | 4.04 | 5.78 | 8.83 | 8.83 | 21.2 | 141.8 |
| ingest_concurrent_disjoint | 8 | 8 | 0 | 25.14 | 47.21 | 65.20 | 65.20 | 82.1 | 97.4 |
| ingest_concurrent_duplicate | 8 | 8 | 0 | 6.34 | 8.44 | 11.52 | 11.52 | 15.8 | 507.2 |
| read_M1_500 | 4 | 1 | 0 | 7005.43 | 7024.35 | 7070.00 | 7070.00 | 28097.6 | 0.1 |
| read_M15_500 | 5 | 1 | 0 | 505.94 | 526.76 | 553.71 | 553.71 | 2634.0 | 1.9 |
| read_H4_500 | 5 | 1 | 0 | 17.86 | 18.08 | 18.25 | 18.25 | 90.6 | 55.2 |
| read_M15_limit_100 | 5 | 1 | 0 | 103.14 | 114.59 | 133.12 | 133.12 | 573.3 | 8.7 |
| read_M15_limit_500 | 5 | 1 | 0 | 512.71 | 527.82 | 552.06 | 552.06 | 2639.3 | 1.9 |
| read_M15_limit_1000 | 5 | 1 | 0 | 1019.05 | 1033.88 | 1049.93 | 1049.93 | 5169.6 | 1.0 |
| concurrent_read_M15_limit_500_threads_1 | 10 | 1 | 0 | 512.98 | 534.27 | 615.03 | 615.03 | 5343.4 | 1.9 |
| concurrent_read_M15_limit_500_threads_4 | 40 | 4 | 0 | 536.82 | 2216.31 | 2854.99 | 4705.70 | 23415.7 | 1.7 |
| concurrent_read_M15_limit_500_threads_8 | 48 | 8 | 0 | 517.96 | 4136.13 | 4657.23 | 4664.74 | 26773.1 | 1.8 |
| concurrent_read_M1_limit_500_threads_1 | 4 | 1 | 0 | 7151.67 | 7467.69 | 7635.62 | 7635.62 | 29871.2 | 0.1 |
| concurrent_read_M1_limit_500_threads_4 | 4 | 4 | 0 | 7453.53 | 18431.95 | 29186.35 | 29186.35 | 29187.3 | 0.1 |
| concurrent_read_with_ingest | 4 | 4 | 0 | 7280.60 | 17874.06 | 28461.65 | 28461.65 | 28501.9 | 0.1 |
| concurrent_ingest_with_read | 4 | 4 | 0 | 28465.75 | 28483.72 | 28496.68 | 28496.68 | 28501.9 | 0.1 |
| news_ingest_200 | 1 | 1 | 0 | 6.80 | 6.80 | 6.80 | 6.80 | 7.1 | 141.5 |
| news_list | 10 | 1 | 0 | 1.61 | 1.67 | 1.75 | 1.75 | 16.9 | 592.6 |
| news_history | 10 | 1 | 0 | 0.31 | 0.35 | 0.46 | 0.46 | 3.7 | 2695.0 |
| analysis_latest | 10 | 1 | 0 | 0.23 | 0.26 | 0.28 | 0.28 | 2.8 | 3568.9 |
| health_v1 | 10 | 1 | 0 | 0.22 | 0.24 | 0.27 | 0.27 | 2.6 | 3909.0 |
| timeframes | 10 | 1 | 0 | 0.26 | 0.30 | 0.40 | 0.40 | 3.2 | 3124.1 |
| sync_latest | 10 | 1 | 0 | 24.10 | 24.52 | 25.16 | 25.16 | 245.6 | 40.7 |
| health | 10 | 1 | 0 | 0.24 | 0.28 | 0.40 | 0.40 | 3.0 | 3305.7 |

### Scenario detail

| Scenario | Statuses | Extra | Notes |
|---|---|---|---|
| read_M1_500_at_10000_rows | `{"200": 3}` | `{"stored_m1_rows": 10000, "body_bytes": 62444, "adaptive_reps": 3, "probe_ms": 620.369}` | GET /api/v1/candles?tf=M1&limit=500 with 10000 stored M1 rows |
| read_M1_500_at_40000_rows | `{"200": 3}` | `{"stored_m1_rows": 40000, "body_bytes": 62444, "adaptive_reps": 3, "probe_ms": 2644.077}` | GET /api/v1/candles?tf=M1&limit=500 with 40000 stored M1 rows |
| read_M1_500_at_70000_rows | `{"200": 3}` | `{"stored_m1_rows": 70000, "body_bytes": 62444, "adaptive_reps": 3, "probe_ms": 4646.006}` | GET /api/v1/candles?tf=M1&limit=500 with 70000 stored M1 rows |
| read_M1_500_at_100000_rows | `{"200": 3}` | `{"stored_m1_rows": 100000, "body_bytes": 62444, "adaptive_reps": 3, "probe_ms": 6762.944}` | GET /api/v1/candles?tf=M1&limit=500 with 100000 stored M1 rows |
| ingest_batch_100 | `{"200": 1}` | `{"ingested": 100, "duplicates": 0, "rejected": 0, "body_bytes": 8582}` | POST /api/v1/candles/ingest, 100 real M1 bars |
| ingest_batch_500 | `{"200": 1}` | `{"ingested": 500, "duplicates": 0, "rejected": 0, "body_bytes": 42182}` | POST /api/v1/candles/ingest, 500 real M1 bars |
| ingest_batch_2000 | `{"200": 1}` | `{"ingested": 2000, "duplicates": 0, "rejected": 0, "body_bytes": 168184}` | POST /api/v1/candles/ingest, 2000 real M1 bars |
| ingest_duplicate_500 | `{"200": 3}` | `{"first_ingested": 500, "replay_ingested": [0, 0]}` | POST the same 500-candle batch 3 times |
| ingest_concurrent_disjoint | `{"200": 8}` | `{"expected_stored_total": 4000, "stored_total": 4000, "duplicates_total": 0, "rejected_total": 0}` | 8 threads x 500 distinct real M1 bars |
| ingest_concurrent_duplicate | `{"200": 8}` | `{"batch_size": 200, "stored_new_total": 200, "duplicate_total": 1400}` | 8 threads post the same fresh 200-candle batch |
| read_M1_500 | `{"200": 4}` | `{"body_bytes": 62424, "status": 200, "adaptive_reps": 4, "probe_ms": 7111.444}` | GET /api/v1/candles?tf=M1&limit=500 (store holds 100008 M1 bars) |
| read_M15_500 | `{"200": 5}` | `{"body_bytes": 62950, "status": 200, "adaptive_reps": 5, "probe_ms": 519.245}` | GET /api/v1/candles?tf=M15&limit=500 (store holds 6665 M15 bars) |
| read_H4_500 | `{"200": 5}` | `{"body_bytes": 38293, "status": 200, "adaptive_reps": 5, "probe_ms": 18.252}` | GET /api/v1/candles?tf=H4&limit=500 (store holds 301 H4 bars) |
| read_M15_limit_100 | `{"200": 5}` | `{"body_bytes": 12726, "status": 200, "adaptive_reps": 5, "probe_ms": 104.028}` | GET /api/v1/candles?tf=M15&limit=100 |
| read_M15_limit_500 | `{"200": 5}` | `{"body_bytes": 62950, "status": 200, "adaptive_reps": 5, "probe_ms": 543.314}` | GET /api/v1/candles?tf=M15&limit=500 |
| read_M15_limit_1000 | `{"200": 5}` | `{"body_bytes": 125731, "status": 200, "adaptive_reps": 5, "probe_ms": 1114.135}` | GET /api/v1/candles?tf=M15&limit=1000 |
| concurrent_read_M15_limit_500_threads_1 | `{"200": 10}` | `{"adaptive_reps_per_thread": 10, "probe_ms": 506.328}` | GET /api/v1/candles?tf=M15&limit=500, 1 clients |
| concurrent_read_M15_limit_500_threads_4 | `{"200": 40}` | `{"adaptive_reps_per_thread": 10, "probe_ms": 561.545}` | GET /api/v1/candles?tf=M15&limit=500, 4 clients |
| concurrent_read_M15_limit_500_threads_8 | `{"200": 48}` | `{"adaptive_reps_per_thread": 6, "probe_ms": 558.206}` | GET /api/v1/candles?tf=M15&limit=500, 8 clients |
| concurrent_read_M1_limit_500_threads_1 | `{"200": 4}` | `{"adaptive_reps_per_thread": 4, "probe_ms": 7419.479}` | GET /api/v1/candles?tf=M1&limit=500, 1 clients |
| concurrent_read_M1_limit_500_threads_4 | `{"200": 4}` | `{"adaptive_reps_per_thread": 1, "probe_ms": 9999.482}` | GET /api/v1/candles?tf=M1&limit=500, 4 clients |
| concurrent_read_with_ingest | `{"200": 4}` | `{"read_path": "/api/v1/candles?tf=M1&limit=500", "adaptive_reps_per_thread": 1, "probe_ms": 7102.306}` | GET /api/v1/candles?tf=M1&limit=500 during ingestion |
| concurrent_ingest_with_read | `{"200": 4}` | `{"ingest_batch": 500}` | POST /api/v1/candles/ingest during reads |
| news_ingest_200 | `{"200": 1}` | `{"stored": 200, "duplicates": 0}` | POST /api/v1/news/ingest, 200 articles |
| news_list | `{"200": 10}` | `{}` | GET /api/v1/news?limit=50 |
| news_history | `{"200": 10}` | `{}` | GET /api/v1/history?limit=50 |
| analysis_latest | `{"200": 10}` | `{}` | GET /api/v1/analysis/latest |
| health_v1 | `{"200": 10}` | `{}` | GET /api/v1/health/v1 |
| timeframes | `{"200": 10}` | `{}` | GET /api/v1/timeframes |
| sync_latest | `{"200": 10}` | `{}` | GET /api/v1/sync/latest |
| health | `{"200": 10}` | `{}` | GET /api/v1/health |

## Bottleneck analysis

Bottleneck is identified from the numbers above, not assumed.

- `read_M1_500` mean **7024.35 ms** vs `read_M15_500` mean **526.76 ms** vs `read_H4_500` mean **18.08 ms**.
- `read_M15_limit_100` mean **114.59 ms**, `limit_500` **527.82 ms**, `limit_1000` **1033.88 ms**.
- `ingest_batch_100` **2.86 ms**, `ingest_batch_500` **8.90 ms**, `ingest_batch_2000` **32.46 ms**.
- Concurrent M15 reads: threads=1 -> mean 534.27 ms, 1.9 req/s; threads=4 -> mean 2216.31 ms, 1.7 req/s; threads=8 -> mean 4136.13 ms, 1.8 req/s. Throughput does not rise with concurrency while per-request latency scales linearly, which is the signature of a single global serialization point.
- Same M1 read at growing store sizes: 10000 rows -> mean 642.80 ms; 40000 rows -> mean 2563.02 ms; 70000 rows -> mean 4381.48 ms; 100000 rows -> mean 7907.86 ms. Growth is linear in the number of stored rows, so the cost is inside the store query, not the transport.

## Isolated store-query experiment (no HTTP)

The same read (`/candles?tf=M1&limit=500`) is issued directly
against a scratch SQLite file built with the server's exact
schema + index, at the same real row counts, to separate the
store query from the HTTP/serialization path.

| stored M1 rows | current guard ms | precheck ms | plain query ms | precheck+plain ms | speedup |
|---|---|---|---|---|---|
| 10000 | 571.7 | 0.77 | 0.14 | 0.92 | 624.7x |
| 40000 | 2284.9 | 3.17 | 0.14 | 3.31 | 689.4x |
| 70000 | 4037.5 | 6.49 | 0.14 | 6.63 | 608.9x |
| 100000 | 6316.1 | 9.20 | 0.14 | 9.34 | 676.5x |

Measured with the real corpus rows listed above (`--m1-full` loads 100,008).

### Proposed optimization (NOT applied)

The measured cause is the alignment guard's formulation:

```sql
-- current (per-row correlated NOT EXISTS over the whole series)
SELECT ... FROM candles WHERE symbol=? AND timeframe=? AND
  NOT EXISTS (SELECT 1 FROM candles bad WHERE
    bad.symbol=candles.symbol AND bad.timeframe=candles.timeframe
    AND <misaligned(bad)>)
  ORDER BY open_time ASC LIMIT ?;
```

SQLite cannot hoist that subquery: the plan is
`SEARCH candles USING INDEX idx_candles_lookup (symbol=? AND
timeframe=?)` plus a `CORRELATED SCALAR SUBQUERY` that rescans
the whole `(symbol, timeframe)` series once per candidate row,
so a `LIMIT 500` read costs O(rows x 500). The measurement
above shows linear growth in stored rows, and the same cost
appears when the query runs with no HTTP in front of it (see
the isolated experiment), so the cost is the query, not the
transport or JSON serialization.

A guard that keeps the exact same semantics but is evaluated
ONCE per series is:

```sql
-- candidate: series-level precheck + plain indexed read
-- 1) is the series dirty?  (one indexed range scan, stops at 1)
SELECT 1 FROM candles WHERE symbol=? AND timeframe=?
  AND <misaligned(candles)> LIMIT 1;
-- 2) clean -> plain read; dirty -> withhold the whole series
SELECT ... FROM candles WHERE symbol=? AND timeframe=?
  ORDER BY open_time ASC LIMIT ?;
```

Both steps run under the store's existing mutex, so the pair is
atomic with respect to writes and the withholding semantics are
identical (a series with any misaligned row is withheld whole).
The same shape applies to `syncPoints`, `syncPointsForSource`,
`latestOpenTimeAnySource` and `allRows`.

Trade-offs to weigh before applying (NOT done here):

- It touches `src/persistence/CandleStore.cpp`, which is outside
  Agent E's owned file set, so it must be routed to the owning
  agent and re-verified by the existing CandleStore/ingest/
  data-quality tests.
- A simpler, non-source change would be to make misaligned rows
  unrepresentable at write time (the ingest guard already rejects
  them), so a one-time migration could drop the read guard's
  cost entirely; that is a data decision, not a perf decision.
- The `LIMIT`/`ORDER BY` interaction must be re-measured after
  any change; the numbers above are the baseline to beat.

## Companion resilience suite

`tests/AyzrPerfResilienceTests.cpp` (Agent E) pins the failure modes this
benchmark measures the cost of. It drives the real `astra_server` binary over
real loopback HTTP against a real SQLite file, using the same real corpus:

| Test case | What it pins |
|---|---|
| `ayzr_concurrent_disjoint_ingest_is_lossless` | 4 concurrent clients x 250 distinct real bars -> all 1000 stored exactly once |
| `ayzr_concurrent_duplicate_ingest_stores_once` | 8 concurrent clients post the same 200-bar batch -> exactly 200 `stored_new`, 1400 `already_stored`, one row per candle |
| `ayzr_concurrent_read_during_ingest_stays_consistent` | reads issued while ingestion is in flight never 5xx and never truncate |
| `ayzr_restart_preserves_candles_and_idempotency` | data + watermark survive stop/start; a replayed batch is still a duplicate |
| `ayzr_ai_failure_does_not_disable_candles_or_analysis` | dead LLM7 endpoint -> news `unavailable`, candles + `/analysis/latest` still 200 with real data |
| `ayzr_news_store_failure_does_not_disable_candles_or_analysis` | unusable `news.sqlite` -> news degrades empty, ASTRA keeps serving |
| `ayzr_ai_failure_is_not_latched` | repeated AI failure is per-request degradation, not a latched disabled state |

Run: `cd build-linux && ./AyzrPerfResilienceTests` (7 cases; 0 failures).
