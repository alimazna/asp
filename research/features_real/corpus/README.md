# Real-data feature corpus (T26 output) — Phase 5.2

The frozen `FeatureSet` JSON that T27 calibration consumes, computed by the
**real** C++ `AnalyticalFeatureEngine` from the operator's real MT5 XAUUSD M1
corpus.

## Provenance (fully reproducible)

```
# 1. operator MT5 export -> canonical CSV
python3 research/data/xauusd_m1/tools/convert_mt5.py

# 2. canonical CSV -> frozen FeatureSet JSON via the C++ engine
python3 research/features_real/run_features.py \
    --m1 research/data/xauusd_m1/xauusd_m1_real.csv \
    --out research/features_real/corpus/real_corpus.json --decisions 0

# 3. the committed artifact is the deterministic gzip of that JSON
gzip -9 -c real_corpus.json > real_corpus.json.gz
```

## Input / output

- **Input:** `research/data/xauusd_m1/xauusd_m1_real.csv`
  (100,008 real M1 bars, 2026-06-24 .. 2026-10-08).
- **Output:** `real_corpus.json` — an array of 6,670 `FeatureSet` objects
  (one per M15 bar close), each with the frozen shape
  (`asOfBarOpenSec, perTimeframe, cross, quality, valid`) plus the top-level
  `close` sibling T27 reads as the decision-bar label.
- 2,497 sets are `valid=true`; the rest are honest `INCOMPLETE` from thin
  warm-up history at the start of the window — nothing is fabricated.

## Committed artifact

`real_corpus.json.gz` (≈3 MB) is committed; the raw 42 MB `real_corpus.json`
is `.gitignore`d. To use it:

```
gunzip -k research/features_real/corpus/real_corpus.json.gz
python3 -m src.models.realdata --corpus research/features_real/corpus
```

T28 resolver: `--corpus` > `AURA_FEATURES_DIR` > `ASTRA_FEATURE_CORPUS` >
`<repo>/research/features_real`.
