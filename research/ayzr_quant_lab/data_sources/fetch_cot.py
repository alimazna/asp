#!/usr/bin/env python3
"""Fetch CFTC Commitments of Traders (disaggregated) history for gold.

Source: cftc.gov public files (US federal data, public domain). One request per
year, no scraping, minimal volume. Gold = CFTC contract market code 088691.

Produces datasets/cftc_cot_gold.csv with the point-in-time fields a strategy can
legitimately use, plus the *publication* date (COT for Tuesday is released the
following Friday ~15:30 ET), so downstream joins can respect the release lag
instead of pretending the Tuesday number was known on Tuesday.

Usage: python3 data_sources/fetch_cot.py
"""

from __future__ import annotations

import csv
import io
import json
import os
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
DEST = os.path.join(LAB, "datasets")
GOLD_CODE = "088691"
YEARS = [2020, 2021, 2022, 2023, 2024, 2025]
UA = "Mozilla/5.0 (AYZR research)"


def fetch_year(year: int) -> list:
    url = f"https://www.cftc.gov/files/dea/history/fut_disagg_txt_{year}.zip"
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        blob = r.read()
    z = zipfile.ZipFile(io.BytesIO(blob))
    name = [n for n in z.namelist() if n.endswith(".txt")][0]
    raw = z.read(name).decode("utf-8", "replace")
    reader = csv.reader(io.StringIO(raw))
    header = [h.strip() for h in next(reader)]
    idx = {h: i for i, h in enumerate(header)}
    rows = []
    for row in reader:
        if len(row) < len(header):
            continue
        if row[idx["CFTC_Contract_Market_Code"]].strip() != GOLD_CODE:
            continue
        rep = row[idx["Report_Date_as_YYYY-MM-DD"]].strip()

        def num(col):
            try:
                return float(row[idx[col]])
            except (KeyError, ValueError):
                return None

        mm_long = num("M_Money_Positions_Long_All")
        mm_short = num("M_Money_Positions_Short_All")
        cm_long = num("Prod_Merc_Positions_Long_All")
        cm_short = num("Prod_Merc_Positions_Short_All")
        rows.append({
            "report_date": rep,
            "open_interest": num("Open_Interest_All"),
            "mm_long": mm_long, "mm_short": mm_short,
            "mm_net": (mm_long - mm_short) if (mm_long is not None and mm_short is not None) else None,
            "comm_long": cm_long, "comm_short": cm_short,
            "comm_net": (cm_long - cm_short) if (cm_long is not None and cm_short is not None) else None,
        })
    return rows


def main() -> int:
    all_rows = {}
    for y in YEARS:
        try:
            rows = fetch_year(y)
            for r in rows:
                all_rows[r["report_date"]] = r
            print(f"  {y}: {len(rows)} gold rows")
        except Exception as e:  # noqa: BLE001
            print(f"  {y}: FAILED {e}")

    ordered = [all_rows[k] for k in sorted(all_rows)]
    os.makedirs(DEST, exist_ok=True)
    path = os.path.join(DEST, "cftc_cot_gold.csv")
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["report_date", "open_interest", "mm_long",
                                           "mm_short", "mm_net", "comm_long",
                                           "comm_short", "comm_net"])
        w.writeheader()
        w.writerows(ordered)
    with open(path.replace(".csv", ".meta.json"), "w") as fh:
        json.dump({
            "source": "CFTC Commitments of Traders (disaggregated futures)",
            "market": "GOLD - COMMODITY EXCHANGE INC.", "code": GOLD_CODE,
            "license": "US federal data, public domain",
            "url": "https://www.cftc.gov/MarketReports/CommitmentsofTraders/index.htm",
            "note": "Report date is Tuesday; public release is the following Friday ~15:30 ET.",
            "rows": len(ordered),
        }, fh, indent=2)
    print(f"wrote {path}: {len(ordered)} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
