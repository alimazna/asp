#!/usr/bin/env python3
"""Fetch external research datasets (free / open sources only).

Sources, all free and license-permissive for research:

  FRED   (fred.stlouisfed.org, public domain US-gov data, keyless CSV export)
         DGS10  US 10y Treasury yield          DFII10  10y real (TIPS) yield
         DGS2   US 2y Treasury yield           DTWEXBGS broad USD index
         VIXCLS CBOE VIX
  Yahoo  (query1.finance.yahoo.com chart JSON, personal/research use)
         GC=F   COMEX gold front-month futures (daily OHLCV)
         DX-Y.NYB / ^VIX cross-checks
  CFTC   (cftc.gov, public domain) weekly Commitments of Traders, gold.

Each dataset is written to ``datasets/`` as CSV with a provenance sidecar. Nothing
here is bought or scraped behind a paywall, and request volume is minimal
(one call per series, no pagination loops).

Usage: python3 data_sources/fetch_external.py
"""

from __future__ import annotations

import csv
import io
import json
import os
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
DEST = os.path.join(LAB, "datasets")
UA = "Mozilla/5.0 (AYZR research; contact repo owner)"

FRED_SERIES = {
    "DGS10": "10-Year Treasury Constant Maturity Rate (% , daily)",
    "DGS2": "2-Year Treasury Constant Maturity Rate (% , daily)",
    "DFII10": "10-Year TIPS real yield (% , daily)",
    "DTWEXBGS": "Nominal Broad US Dollar Index (daily)",
    "VIXCLS": "CBOE Volatility Index (daily)",
}

YAHOO_SYMBOLS = {
    "GC=F": "COMEX Gold front-month futures (daily OHLCV)",
    "DX-Y.NYB": "US Dollar Index (ICE, daily)",
    "SI=F": "COMEX Silver front-month futures (daily OHLCV)",
}


def _get(url: str, timeout: int = 30) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def fetch_fred(series: str) -> list:
    url = f"https://fred.stlouisfed.org/graph/fredgraph.csv?id={series}"
    raw = _get(url).decode("utf-8", "replace")
    rows = []
    for line in raw.splitlines()[1:]:
        parts = line.split(",")
        if len(parts) != 2:
            continue
        date, val = parts[0], parts[1].strip()
        if val in ("", ".", "NA"):
            continue
        rows.append((date, float(val)))
    return rows


def fetch_yahoo(symbol: str, rng: str = "10y") -> list:
    sym = urllib.parse.quote(symbol)
    url = (f"https://query1.finance.yahoo.com/v8/finance/chart/{sym}"
           f"?range={rng}&interval=1d")
    data = json.loads(_get(url).decode("utf-8", "replace"))
    res = data["chart"]["result"][0]
    ts = res["timestamp"]
    q = res["indicators"]["quote"][0]
    out = []
    for i, t in enumerate(ts):
        if q["open"][i] is None:
            continue
        out.append((time.strftime("%Y-%m-%d", time.gmtime(t)),
                    q["open"][i], q["high"][i], q["low"][i], q["close"][i],
                    q.get("volume", [None] * len(ts))[i]))
    return out


def write_csv(name: str, header: list, rows: list, meta: dict) -> None:
    os.makedirs(DEST, exist_ok=True)
    path = os.path.join(DEST, name)
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    with open(path.replace(".csv", ".meta.json"), "w") as fh:
        json.dump(meta, fh, indent=2)
    print(f"  wrote {name}: {len(rows)} rows")


def main() -> int:
    print("FRED series:")
    for sid, desc in FRED_SERIES.items():
        try:
            rows = fetch_fred(sid)
            write_csv(f"fred_{sid}.csv", ["date", "value"], rows, {
                "source": "FRED", "series": sid, "description": desc,
                "license": "US federal data, public domain",
                "url": f"https://fred.stlouisfed.org/series/{sid}",
                "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            })
        except Exception as e:  # noqa: BLE001
            print(f"  FAILED {sid}: {e}")

    print("Yahoo series:")
    for sym, desc in YAHOO_SYMBOLS.items():
        try:
            rows = fetch_yahoo(sym)
            write_csv(f"yahoo_{sym.replace('=','_').replace('.','_')}.csv",
                      ["date", "open", "high", "low", "close", "volume"], rows, {
                          "source": "Yahoo Finance chart API", "symbol": sym,
                          "description": desc,
                          "license": "Yahoo ToS, personal/research use",
                          "url": f"https://finance.yahoo.com/quote/{sym}",
                          "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                      })
        except Exception as e:  # noqa: BLE001
            print(f"  FAILED {sym}: {e}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
