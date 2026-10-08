#!/usr/bin/env python3
"""Package the fetched XAUUSD M1 corpus for git.

For each year CSV produced by fetch.sh:
  * write <year>.csv.gz (deterministic gzip: mtime=0)
  * write sample/<year>.head.csv (first 1000 data rows) — committed, small
  * record sha256 of the raw CSV, the .gz and the sample in checksums.sha256
  * record metadata.json (source, fetch date, row counts, coverage)

Raw CSVs and .gz stay on disk but are .gitignore'd; only samples + fetch.sh +
checksums + QUALITY.md + README + metadata are committed, per the directive
("do NOT commit multi-hundred-MB raw files directly to git").
"""

from __future__ import annotations

import datetime as dt
import gzip
import hashlib
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent  # research/data/xauusd_m1
YEARS = (2021, 2022, 2023, 2024, 2025)
SAMPLE_ROWS = 1000


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def gz(raw: Path) -> Path:
    out = raw.with_suffix(".csv.gz")
    with raw.open("rb") as src, gzip.GzipFile(filename="", mode="wb", fileobj=out.open("wb"), mtime=0) as dst:
        shutil.copyfileobj(src, dst, 1 << 20)
    return out


def sample(raw: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with raw.open() as src, dest.open("w") as out:
        out.write(src.readline())  # header
        for _ in range(SAMPLE_ROWS):
            line = src.readline()
            if not line:
                break
            out.write(line)


def main() -> int:
    sums, meta = [], {"source": "Dukascopy public datafeed", "collector": "dukascopy-node 1.50.0",
                      "symbol": "XAUUSD", "price_type": "bid", "timeframe": "m1",
                      "utc": True, "years": {}}
    for y in YEARS:
        raw = HERE / f"{y}.csv"
        if not raw.exists():
            print(f"missing {raw}")
            continue
        n = sum(1 for _ in raw.open()) - 1
        pack = gz(raw)
        samp = HERE / "sample" / f"{y}.head.csv"
        sample(raw, samp)
        sums.append(f"{sha256(raw)}  {raw.name}")
        sums.append(f"{sha256(pack)}  {pack.name}")
        sums.append(f"{sha256(samp)}  sample/{samp.name}")
        with raw.open() as fh:
            head = next(fh).strip().split(",")
            first = next(fh).split(",")[0]
        meta["years"][str(y)] = {"rows": n, "header": head, "first_ts_ms": int(first)}
    (HERE / "checksums_dukascopy_2021_2025.sha256").write_text("\n".join(sums) + "\n")
    meta["fetched_utc"] = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    (HERE / "metadata_dukascopy_2021_2025.json").write_text(json.dumps(meta, indent=2) + "\n")
    print("packaged:", len(sums) // 3, "years")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
