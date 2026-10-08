# Audit Report — Stray root-level `XAUUSDM1.csv` (provenance / plausibility)

- **Auditor:** Agent-D
- **Date:** 2026-10-08 08:55 UTC
- **Repo HEAD:** 89685c1
- **Severity:** MEDIUM — not a code defect, but an unreviewable data artifact
  committed to `main`; it does not feed the pipeline and contradicts the corpus policy.
- **Status:** OPEN (needs the Lead/operator's intent)

## What was found

Commit `89685c1` (`Add files via upload`, Ali Man ALmazna, 2026-10-08 10:43 +0300)
added **`XAUUSDM1.csv`** (13,588,782 bytes, 100,008 rows) at the **repository root**.

## Objective facts

1. **Foreign format** — not the documented corpus CSV
   (`research/data/xauusd_m1/README.md`: `timestamp_ms_utc,open,high,low,close[,volume]`).
   Actual header/first rows:
   ```
   2026.06.24 11:08,4076.56000,4077.37000,4075.81000,4076.67000,342,0
   2026.06.24 11:09,4076.72000,4077.04000,4075.19000,4075.55000,344,0
   ```
   Dot-date `YYYY.MM.DD`, space-separated, and a **7th trailing column** (`0`) —
   neither the fetch.sh output nor the ms-epoch format the pipeline parses.
2. **Out-of-window dates** — the first rows are `2026.06.24`. The declared corpus is
   **2021-01-01 .. 2025-12-31 (UTC)** from the Dukascopy datafeed. So either the dates
   are wrong, or this is a different (future) window, or a different source entirely.
   The value ~4076 for XAU/USD at 2026-06 is unverifiable here; flagging, not asserting.
3. **Location** — root, not `research/data/xauusd_m1/`. `scripts/data_paths.py`
   resolves the corpus at `<root>/<year>.csv` under `AURA_DATA_ROOT`
   (default `research/data/xauusd_m1`); **nothing in the repo reads a root-level CSV**.
   So this file is not consumed by T26/T27 and cannot unblock E05 where it sits.
4. **Policy conflict** — the Phase 5.1 directive and README state raw multi-hundred-MB
   CSVs are **not committed** (raw `.csv` is gitignored; only `fetch.sh` + checksums +
   1000-row samples + `metadata.json` + `QUALITY.md` are committed). This raw 13.6 MB
   blob was committed at root with **no checksum, no metadata, no provenance**.
5. **Not reproducible from `fetch.sh`** — no `checksums.sha256`/`metadata.json`
   accompanies it, so its provenance cannot be verified against the pinned collector.

## Why it matters

- E05 (the mission's hard publication blocker) depends on a **verified** corpus. A
  stray, format-mismatched, out-of-window raw file at root does not close it, and if
  it were ever fed to the pipeline it would fail parsing or produce non-reproducible
  numbers.
- It sits on `main` and will be pulled by every agent; it inflates every clone and
  invites confusion with the real corpus.

## Recommendation

- Confirm intent: if this is a candidate/raw upload, it belongs **out of the repo** or
  under the corpus layout with `pack.py` (gzip + samples + checksums + metadata) and a
  `QUALITY.md`.
- If it is not the canonical corpus, `git rm` it from `main` (history can stay) so the
  tree does not carry an unverified raw blob.
- If it **is** intended data, its format must be converted to the documented
  ms-epoch CSV and its dates reconciled with 2021–2025 before it can feed T26/T27.

No code touched. Flagged for the Lead's ruling.

---

# Addendum — RESOLVED (Phase 5.2)

- **Date:** 2026-10-08 09:15 UTC
- **Resolution:** **explained and largely closed.** The root `XAUUSDM1.csv` is the
  **operator's original MetaTrader 5 export** (UTF-16, no header). The Lead converted
  it with `research/data/xauusd_m1/tools/convert_mt5.py` to the canonical
  `research/data/xauusd_m1/xauusd_m1_real.csv` (ISO ms-… format, checksummed),
  documented provenance in `README.md`, and committed `QUALITY.md` +
  `checksums.sha256` + `sample_first_1000.csv`. **E05 is cleared.**
- **My concerns, re-checked:**
  - *foreign format* → was the raw export; the converted canonical file is clean and
    I reproduced it **byte-identically** from this very root file (see
    `AUDIT-T29-realdata.md` §2).
  - *2026.06 dates* → real: the operator's window is **2026-06-24 .. 2026-10-08**
    (broker server time), not the earlier-declared Dukascopy 2021–2025. Correctly
    surfaced; the T27/T29 reports carry the caveat.
  - *wrong location* → resolved: the canonical corpus now lives under the layout
    `data_paths.py` expects; the pipeline consumes it.
  - *policy (no raw blobs on main)* → **still stands as a minor cleanup point:** the
    13.6 MB raw upload remains at the repo root. Now that `convert_mt5.py` +
    `checksums.sha256` + `sample_first_1000.csv` capture provenance, the root blob is
    redundant and could be `git rm`'d from `main` (history retained) to reduce clone
    weight. Non-blocking.
- The earlier MEDIUM item is downgraded to **LOW (cleanup)**.
