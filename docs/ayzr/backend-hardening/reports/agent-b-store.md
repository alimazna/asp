# Agent B — Candle data integrity & persistence (backend hardening)

Scope: `src/persistence/CandleStore.{h,cpp}` (note: the store lives under
`src/persistence/`, not `src/data/`) plus two new test files. No other
production file was touched. Shadow-only: no order execution, no invented data,
no secrets. No deploy/push/delete.

## Files changed

Production:
- `src/persistence/CandleStore.cpp`
- `src/persistence/CandleStore.h`

Tests (new):
- `tests/CandleStoreIntegrityTests.cpp`
- `tests/AyzrConcurrencyTests.cpp`

Report:
- `docs/ayzr/backend-hardening/reports/agent-b-store.md` (this file)

## Commands run

```bash
# cmake was absent in this image
sudo apt-get install -y cmake
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j4
cd build-linux && for t in *Tests; do ./"$t" || echo "FAIL $t"; done
```

All tests use an isolated temp DB path (`std::filesystem::temp_directory_path()`,
unique per run, removed at the end). No live LLM7 calls; no network.

## Exact test results

Full suite after the changes:

- **32 test binaries, 265 test cases, 0 failed, 0 assertion failures.**
- Baseline before my changes: 30 binaries, all passing (so 2 new binaries added).

New tests:
- `CandleStoreIntegrityTests` — **7 cases, 0 failed**
- `AyzrConcurrencyTests` — **4 cases, 0 failed**

Stability: `AyzrConcurrencyTests` run 10× consecutively → 0 failures;
`CandleStoreIntegrityTests` run 5× consecutively → 0 failures.

## Audit findings (measured, then fixed)

Baseline was **measured before changing anything** by writing the new tests and
running them against the unmodified store. Three cases failed on baseline,
proving they detect real defects:

| # | Defect | Effect | Fix |
|---|--------|--------|-----|
| 1 | `recent()` used `ORDER BY open_time ASC LIMIT ?` | Returned the **oldest** N bars, not the newest N. The candles route publishes `newest_closed_time = bars.back()`, so any series longer than `limit` reported a **stale watermark as the newest closed candle** (data-honesty defect). | Inner `ORDER BY open_time DESC LIMIT ?` re-sorted ascending outside; quarantine predicate kept inside the inner query. |
| 2 | Non-positive `limit` in `recent()`/`allRows()` | SQLite reads a negative `LIMIT` as "no limit" → **unbounded scan** of the whole series. | Explicit `if (limit <= 0) return {};` guard in both. |
| 3 | `insertBatch()` ignored the return codes of `BEGIN IMMEDIATE`, `COMMIT`, `ROLLBACK` | If `BEGIN` failed, the loop ran **outside a transaction** and `COMMIT` failed silently while the function returned `true` — a **false durability ack** for a write that may not be durable. | `BEGIN` failure fails fast with an error; `COMMIT` failure is reported and unwound; `ROLLBACK` failure is surfaced. |

Additional consistency fix (latent, unused in production routes):

| # | Item | Fix |
|---|------|-----|
| 4 | `latestOpenTime(source,symbol,tf)` was the **only** read path without the grid-alignment quarantine (its sibling `latestOpenTimeAnySource` has it). A misaligned row for that identity could masquerade as a real watermark. | Applied `cleanSourceSeriesExpr()` to it; header updated. It is not called by any production route (only tests), so no API contract changes. |

## Deliberately NOT changed (to preserve exact semantics)

- `allRows()` ordering stays **oldest-first**. The operator quarantine tool
  (`scripts/quarantine_admin.py`) and its documented purpose (find the earliest
  legacy/misaligned rows) depend on this. Only the `limit<=0` guard was added.
- The quarantine semantics are preserved **exactly**: a single misaligned row
  withholds the whole `(symbol,timeframe)` series from `recent`,
  `latestOpenTimeAnySource`, `syncPoints`, `syncPointsForSource`; source-scoped
  reconciliation stays keyed to the sender's **own** rows (verified by test);
  `allRows` still exposes the offending row so it can be purged one-by-one via
  `deleteCandle`.
- `PRAGMA synchronous` is left at SQLite's default (FULL) with a comment; the
  store is low-throughput, so trading durability for fewer fsyncs is not
  warranted. (An intermediate NORMAL change was reverted.)
- `open()`, the schema, the primary key `(source_id,symbol,timeframe,open_time)`
  and `idx_candles_lookup(symbol,timeframe,open_time)` are unchanged; no API
  contract was altered.

## Test-seam note

A single, clearly-marked seam was added to the store so batch atomicity can be
proven deterministically without fabricating data:

```cpp
void setFaultAfterInsertsForTest(int n) noexcept;  // -1 = disabled (default)
```

It makes the next `insertBatch` abort and roll back after `n` successful
INSERTs. It is a no-op unless a test calls it, is one-shot, and is not used by
any production path. The atomicity test confirms a mid-batch failure leaves
**zero** rows behind and the store stays usable.

## Tests added

`tests/CandleStoreIntegrityTests.cpp` (7):
- `RealM1RoundTripAndRestart` — loads 5,000 genuine M1 bars from the committed,
  checksummed MT5 export (`research/data/xauusd_m1/xauusd_m1_real.csv`), verifies
  exact OHLCV fidelity on read-back, idempotent re-ingest, and durability across
  close/reopen. Every bar is asserted grid-aligned.
- `RecentReturnsNewestAscending` — newest-N + ascending, full series when
  `limit >= count`.
- `NonPositiveLimitIsEmptyNotUnbounded` — `0`/negative limits return empty.
- `OutOfOrderAndGapsPreserved` — out-of-order/gapped input stored as received,
  ordered on read, duplicates collapsed.
- `QuarantineWithholdsThenRestores` — misaligned row withholds the series
  cross-source; source-scoped view stays per-sender; `allRows` exposes it; purge
  restores the clean series and the newest-N window.
- `StoreUsableAfterEmptyAndRepeatBatches` — empty batch, repeat batch.
- `BatchIsAtomicOnMidBatchFailure` — injected mid-batch failure → no partial write.

`tests/AyzrConcurrencyTests.cpp` (4):
- `ReadersAgainstSingleWriter` — 3 readers vs 1 writer on one handle; watermark
  never goes backwards; full series present afterwards.
- `DuplicateBatchesStoredExactlyOnce` — 6 threads × 200 identical identities →
  exactly one `stored_new` per identity, all others `already_stored`.
- `MultiConnectionContentionUsesBusyTimeout` — 4 connections to the same file,
  disjoint writes; busy timeout absorbs contention, all 400 rows land.
- `DurableAcrossConnectionsAndReopen` — a second connection sees the first's
  committed rows; all rows survive reopen.

## Unresolved issues / residual risks

1. **`latestOpenTime()` semantics changed** (now quarantined). It is unused by
   production routes, but any external caller relying on the old raw value would
   now see `0` for a contaminated series. This is intentional and documented in
   the header.
2. **Concurrency is connection-serialized.** A single shared `CandleStore` is
   mutex-serialized; multiple connections rely on `busy_timeout=5000 ms`. Under
   sustained multi-writer contention beyond 5 s a writer can still get
   `SQLITE_BUSY`; it now correctly **reports failure** instead of falsely acking.
   Not observed in any test.
3. **Quarantine predicate is duplicated as SQL string literals**
   (`misalignedExpr` in `CandleStore.cpp`) rather than derived from
   `Mt5BridgeContract::hasAlignedGrid`. Adding a timeframe requires updating both.
   Left as-is to avoid changing behavior; flagged as a maintenance risk.
4. **Correlated `NOT EXISTS` quarantine cost** grows with series size when a
   misaligned row is present. Bounded by series length; not measured as a
   bottleneck here.
5. `AGENTS.md` was **not** edited: the task scope restricted edits to the owned
   files, and `AGENTS.md` is shared across worktrees. Learnings are recorded here
   instead.
