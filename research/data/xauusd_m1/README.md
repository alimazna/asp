# XAUUSD M1 — real market data (Phase 5.1, closes E05)

Real XAU/USD one-minute bars, 2021-01-01 through 2025-12-31 (UTC). This is the
data that unblocks the mission's only hard publication blocker (E05): without it,
every calibration number is a synthetic pipeline check, not evidence about gold.

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
python3 tools/quality_check.py    # -> QUALITY.md
python3 tools/pack.py             # -> .csv.gz, sample/, checksums.sha256, metadata.json
```

## Files

| Path | Committed? | What |
|---|---|---|
| `fetch.sh` | yes | reproducible fetch (pinned collector) |
| `tools/quality_check.py` | yes | data-quality checks -> `QUALITY.md` |
| `tools/pack.py` | yes | gzip + samples + checksums + metadata |
| `<year>.csv` | no (gitignored) | raw BID M1 OHLCV, ~5 MB/yr |
| `<year>.csv.gz` | no (gitignored) | deterministic gzip of the raw CSV |
| `ask/<year>.csv` | no (gitignored) | raw ASK M1 (spread/cost, RULE B) |
| `sample/<year>.head.csv` | yes | first 1000 rows per year (provenance) |
| `checksums.sha256` | yes | sha256 of raw + gz + samples |
| `metadata.json` | yes | source, fetch time, row counts, header |
| `QUALITY.md` | yes | mandatory quality report |

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
