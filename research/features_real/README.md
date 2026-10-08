# T26 — real XAUUSD M1 → frozen FeatureSet

Feeds real Dukascopy M1 bars through the **real C++ `AnalyticalFeatureEngine`**
and emits the frozen `FeatureSet` JSON that the Python model layer
(`src/models/features.py`, `parse_feature_set`) consumes. This is the T26
boundary for Phase 5.1: real data in, honest engine output out.

## No feature recompute on the Python side

Feature values are computed **only** by
`src/analysis/features/AnalyticalFeatureEngine.cpp`, linked into the
`aura_feature_dump` executable. The Python driver (`run_features.py`) never
computes a feature; it only aggregates M1 bars into the nine canonical
timeframes and validates the JSON.

## Pipeline

```
research/data/xauusd_m1/<year>.csv          (real M1, T25)
        │  run_features.py: calendar aggregation (UTC)
        ▼
<workdir>/{M1,M5,M15,M30,H1,H4,D1,W1,MN1}.csv
        │  aura_feature_dump: engine.computeAll per decision instant
        ▼
FeatureSet JSON  ──►  src.models.features.parse_feature_set  (validate)
```

## Build

```
cmake --build build --target aura_feature_dump
```

## Run

```
# real corpus
python3 research/features_real/run_features.py \
    --m1 research/data/xauusd_m1/2025.csv \
    --out research/features_real/sample_2025.json --decisions 500

# hermetic self-test (no data files needed)
python3 research/features_real/run_features.py --synthetic \
    --out /tmp/t26.json --decisions 30
```

`--decisions 0` emits one set per M15 bar open. `--m1` accepts several yearly
files; they are concatenated in time order.

## Honesty properties

- **Causal.** The dumper drops any bar that has not **fully closed** at the
  decision instant (`closeTimeSec() <= asOf`), then the engine pins every stream
  to that single instant. A partially formed M15/H4/D1 bar is never read, so no
  future information can enter a feature. Regression:
  `tests/features/test_real_data_harness.py::test_causality_later_bars_do_not_alter_early_sets`.
- **Deterministic.** Identical inputs give byte-identical output (no wall-clock,
  no randomness; doubles are rendered at fixed precision).
- **Honest about thin history.** Streams with too little context report
  `valid=false`, `quality=INCOMPLETE/DEGRADED`; nothing is fabricated. Young
  corpora (e.g. the first months of a series) yield a `DEGRADED` set — that is
  correct, not a bug.
- **No silent interpolation.** The M1 reader rejects duplicate / out-of-order
  timestamps rather than repairing them.

## Limits (stated, not hidden)

- Higher timeframes are **calendar-aggregated from M1** (UTC buckets, no DST):
  first open, max high, min low, last close, summed tick volume. Dukascopy's own
  higher-TF bars are not used; aggregation is logged here so any divergence is
  visible.
- The dumper supplies a bounded trailing window (300 bars ≥ the engine's
  200-bar context). `barsAvailable` is therefore a lower bound on total history;
  it does not affect any feature value (verified equal to the unbounded run).
- The synthetic self-test series is a logic fixture, **not** evidence about
  gold. The real-data sample is generated from the T25 corpus.

## Deliverables

| Path | What |
|---|---|
| `src/analysis/features/emit_feature_set.cpp` | C++ dumper (owns `main()`); real engine over CSV → frozen JSON |
| `research/features_real/run_features.py` | data-prep driver + validation (no feature math) |
| `tests/features/test_real_data_harness.py` | causal / deterministic / validating regression |
| `research/features_real/sample_<year>.json` | small real-data FeatureSet sample (once T25 corpus lands) |
