# Datasets — provenance

All datasets here are free/open and reproducible via `data_sources/fetch_*.py`.
Each has a `.meta.json` sidecar with source, license, URL and fetch time.

| File | Source | License | Coverage |
|---|---|---|---|
| `fred_DGS10.csv` | FRED | US public domain | 10y Treasury yield, 1962→ |
| `fred_DGS2.csv` | FRED | US public domain | 2y Treasury yield |
| `fred_DFII10.csv` | FRED | US public domain | 10y TIPS real yield, 2003→ |
| `fred_DTWEXBGS.csv` | FRED | US public domain | Broad USD index, 2006→ |
| `fred_VIXCLS.csv` | FRED | US public domain | VIX, 1990→ |
| `yahoo_GC_F.csv` | Yahoo Finance | research use | COMEX gold futures daily |
| `yahoo_SI_F.csv` | Yahoo Finance | research use | COMEX silver futures daily |
| `yahoo_DX-Y_NYB.csv` | Yahoo Finance | research use | US Dollar Index daily |
| `cftc_cot_gold.csv` | CFTC | US public domain | Gold COT, 2020–2025, weekly |

The spot XAUUSD corpus lives in `../../../data/xauusd_m1/` (Dukascopy + operator
MT5 export).

**Point-in-time rule** (`ayzr_lab/external.py`): FRED/Yahoo values are shifted to
the next business day, COT (Tuesday report) to the following Monday, so no value
is visible to a decision before it was actually published.
