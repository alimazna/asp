# XAUUSD M1 — real market data

Two real XAU/USD one-minute corpora live here. Together they unblock the
mission's only hard publication blocker (E05) and support the real-data
calibration.

| Corpus | Window | Bars | Role |
|---|---|---|---|
| **Dukascopy BID+ASK** | 2021-01-03 .. 2025-12-30 | 1,695,651 | **multi-regime** — the only corpus that can carry a decision-grade dev/val/OOS walk-forward |
| **Operator MT5** | 2026-06-24 .. 2026-10-08 | 100,008 | newer broker export — an **independent cross-check** and a 3.5-month POC window |

The Dukascopy corpus covers five calendar years across different gold regimes
(as low as ~1680, as high as ~4400 USD/oz); the MT5 export is a single recent
3.5-month window. A **decision-grade** result needs the multi-regime corpus; the
MT5 window alone supports only a **proof-of-concept** (see its own section
below). Neither corpus is repainted or repaired — anomalies are reported, not
fixed.

Commit policy: the raw per-minute CSVs are large; the **deterministic gzip**
(`<year>.csv.gz`, ~24 MB total for BID, ~22 MB for ASK) is committed so the
distributed team can consume the multi-year corpus, while the raw CSVs stay
reproducible via `fetch.sh`.

## Source

- **Provider:** Dukascopy Bank SA — public historical datafeed
  (`https://datafeed.dukascopy.com`).
- **Licence:** Dukascopy public historical data — free for personal/research use.
  No broker account required for the research phase.
- **Why this source:** it is the source used in the prior ASTRA research
  (EXP-0019 / EXP-0020), it is free, public and reproducible, and it covers the
  full requested window.

## Collector

`fetch.sh` uses the open-source **`dukascopy-node` v1.50.0** CLI (MIT), pinned in
the script. The raw Dukascopy datafeed intermittently returns HTTP 503 under load
from shared/datacenter IPs; the collector retries each artifact (8 tries, 1.5 s
pause) so the fetch is robust and idempotent. Re-running produces the same files.

```
cd research/data/xauusd_m1
./fetch.sh            # BID (with tick volume) + ASK (spread reference)
python3 tools/quality_check.py    # -> QUALITY_dukascopy_2021_2025.md
python3 tools/pack.py             # -> .csv.gz, sample/, checksums_/metadata_dukascopy_*.{sha256,json}
```

## Files

| Path | Committed? | What |
|---|---|---|
| `fetch.sh` | yes | reproducible fetch (pinned collector) |
| `tools/quality_check.py` | yes | data-quality checks -> `QUALITY_dukascopy_2021_2025.md` |
| `tools/pack.py` | yes | gzip + samples + checksums + metadata |
| `<year>.csv` | no (gitignored) | raw BID M1 OHLCV, ~5 MB/yr |
| `<year>.csv.gz` | **yes** | deterministic gzip of the raw CSV (committed so the team can consume the multi-year corpus) |
| `ask/<year>.csv` | no (gitignored) | raw ASK M1 (spread/cost, RULE B) |
| `sample/<year>.head.csv` | yes | first 1000 rows per year (provenance) |
| `checksums_dukascopy_2021_2025.sha256` | yes | sha256 of raw + gz + samples |
| `metadata_dukascopy_2021_2025.json` | yes | source, fetch time, row counts, header |
| `QUALITY_dukascopy_2021_2025.md` | yes | mandatory quality report |

Raw multi-hundred-MB files are deliberately **not** committed. The committed
`fetch.sh` + checksums + samples are sufficient to reproduce and verify the
corpus byte-for-byte.

## Format

`timestamp_ms_utc,open,high,low,close,volume`
- `timestamp_ms_utc`: bar-open time, milliseconds since epoch, UTC.
- `volume`: Dukascopy tick volume (millions of units in the raw collector output,
  as emitted by `dukascopy-node -v`); it is a **tick count proxy, not real
  traded volume** — treat it as an activity signal, not size.
- Prices in USD per troy ounce.

Higher timeframes (M5..MN1) are derivable from these M1 bars; only M1 is stored.

## Scope / honesty

- BID bars with tick volume are the primary series; ASK is kept for spread.
- RULE B (three cost tiers) still applies when converting prices to results.
- No lookahead: bars are causal, timestamped at bar open.

---

# Phase 5.2 — operator MT5 corpus (primary real-data source)

The operator supplied a real MetaTrader 5 XAUUSD M1 export, which is now the
authoritative real-data corpus for the mission (it closes E05 for every
downstream check). The Dukascopy fetch above is retained as a reproducible
secondary source.

## Source

- **Provider:** the operator's MT5 broker (broker export, uploaded as
  `XAUUSDM1.csv`).
- **Raw file:** UTF-16-LE (BOM), no header, comma-separated, columns
  `<DATE> <TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<TICKVOL>,<SPREAD>`.
- **The raw upload is not committed** (13.5 MB, and it is an immutable input);
  it is `.gitignore`d. The **converted canonical corpus is committed** so any
  agent can consume it directly. `tools/convert_mt5.py` is reproducible.

## Convert + validate

```
python3 research/data/xauusd_m1/tools/convert_mt5.py   # -> xauusd_m1_real.csv
python3 research/data/xauusd_m1/tools/quality_check_mt5.py  # -> QUALITY.md (MT5 corpus)
```

## Files

| Path | Committed? | What |
|---|---|---|
| `XAUUSDM1.csv` | no (gitignored) | raw MT5 export (UTF-16) |
| `xauusd_m1_real.csv` | **yes** | canonical corpus: `timestamp,open,high,low,close,volume` |
| `sample_first_1000.csv` | yes | first 1000 bars (provenance) |
| `checksums.sha256` | yes | sha256 of the canonical corpus + sample |
| `QUALITY.md` | yes | mandatory quality report (MT5 corpus) |
| `tools/convert_mt5.py` | yes | reproducible converter |
| `tools/quality_check_mt5.py` | yes | quality checks |

## Format

`timestamp,open,high,low,close,volume`
- `timestamp`: ISO `YYYY-MM-DD HH:MM:SS` (MT5 bar-open time).
- `open/high/low/close`: USD per troy ounce.
- `volume`: MT5 tick volume (tick count proxy; the SPREAD column is dropped).

## Timezone

The export carries **broker server time** exactly as written; there is no
timezone label or DST marker in the file, so the converter does not convert it
and does not guess an offset. The gap structure (a ~2h break at 23:00-01:00 and
Fri-night -> Mon weekend closures) is consistent with a UTC+2/+3 broker clock.
The operator should confirm the server timezone; until then all times are
documented as broker time and treated consistently (causal ordering is
unaffected by a constant offset). No downstream analysis depends on the
absolute UTC label, only on ordering and gaps.

## Coverage

- **100,008** M1 bars.
- **2026-06-24 11:08 .. 2026-10-08 10:30** (broker time).
- 0 duplicates, 0 OHLC violations, 0 NaN, 0 off-grid timestamps, 0 unexpected gaps.
- Observed range 3942.48..4696.73 USD/oz.

## Coverage limitation (read before using these numbers)

This corpus is a **single ~3.5-month window** (2026-06-24 .. 2026-10-08), a
couple of thousand M15 decision instants. It is enough to run the full pipeline
end-to-end on **real gold** and to produce a **proof-of-concept** calibration
number — it is **not** enough for a decision-grade, multi-regime walk-forward.

- There is only **one calendar regime** in the window, so a chronological
  development / validation / OOS split (the T27 year partition, 2021-2025) does
  **not** apply. Any split of this window is a **causal, in-window** split, and
  the numbers are **in-sample-ish** evidence, not out-of-sample proof.
- A walk-forward folded at M15 cadence gives very few folds on ~3.5 months; the
  T27 runner degrades honestly (records a note) rather than fabricating folds.
- Therefore the T27 result on this corpus is reported as a **PROOF-OF-CONCEPT**
  (does the real-data pipeline run and calibrate?), **not** as the mission's
  final publication verdict. The final verdict requires a multi-year, multi-regime
  corpus, which this window cannot supply.

This limitation is a property of the operator's uploaded window, not a defect;
the file is authoritative for what it covers and nothing is extrapolated beyond
it. **The final publication verdict does not rest on this window** — it rest on
the Dukascopy 2021-2025 corpus above, which supports the multi-regime
dev/val/OOS split. The MT5 window is the independent POC / cross-check.
