# Real-data feature corpus (T26 output)

Two frozen `FeatureSet` JSON corpora, both computed by the **real** C++
`AnalyticalFeatureEngine` (no Python feature math). They are the T27 calibration
inputs.

| Artifact | Corpus | Bars | Decision sets | Valid | Role |
|---|---|---|---|---|---|
| `real_corpus_2021_2025.json.gz` | Dukascopy 2021-2025 | 1,695,651 | 113,083 | 107,403 | **decision-grade** (year partition applies) |
| `real_corpus.json.gz` | operator MT5 2026-06-24..10-08 | 100,008 | 6,670 | 2,497 | POC / independent cross-check |

Only the gzip is committed; the raw `.json` is `.gitignore`d. Use
`gunzip -k` or let `src/models/realdata.py` read `.json.gz` directly.

## Provenance (fully reproducible)

```
# multi-year (decision-grade)
python3 research/features_real/run_features.py \
    --m1 research/data/xauusd_m1/2021.csv research/data/xauusd_m1/2022.csv \
         research/data/xauusd_m1/2023.csv research/data/xauusd_m1/2024.csv \
         research/data/xauusd_m1/2025.csv \
    --out research/features_real/corpus/real_corpus_2021_2025.json --decisions 0
gzip -9 -c real_corpus_2021_2025.json > real_corpus_2021_2025.json.gz

# MT5 POC (operator export)
python3 research/data/xauusd_m1/tools/convert_mt5.py
python3 research/features_real/run_features.py \
    --m1 research/data/xauusd_m1/xauusd_m1_real.csv \
    --out research/features_real/corpus/real_corpus.json --decisions 0
gzip -9 -c real_corpus.json > real_corpus.json.gz
```

Each row is a frozen `FeatureSet` (`asOfBarOpenSec, perTimeframe, cross, quality,
valid`) plus a top-level `close` sibling that T27 reads as the decision-bar label,
one per M15 bar close. `valid=false` sets are honest `INCOMPLETE` warm-up sets
(thin history at a window start) — nothing is fabricated.

## Which one is T27?

- **Decision-grade:** `real_corpus_2021_2025.json.gz`. It spans five calendar years
  across different gold regimes, so the chronological **dev (2021-22) / val
  (2023-24) / OOS (2025)** year partition and the walk-forward are supportable.
  This is the publication verdict.
- **POC / cross-check:** `real_corpus.json.gz` (3.5-month, single-regime 2026
  window). It proves the real-data pipeline runs and calibrates and independently
  cross-checks the provider, but a year split does not apply to it. Any split of
  this window is a causal, in-window split — in-sample-ish evidence, not
  out-of-sample proof.

## Consuming it

```
gunzip -k research/features_real/corpus/real_corpus_2021_2025.json.gz
python3 -m src.models.realdata --corpus research/features_real/corpus
```

T28 resolver: `--corpus` > `AURA_FEATURES_DIR` > `ASTRA_FEATURE_CORPUS` >
`<repo>/research/features_real`.
