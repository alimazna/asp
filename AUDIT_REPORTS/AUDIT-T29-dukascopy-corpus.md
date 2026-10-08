# Audit Report — T29 Part 2a: Dukascopy 2021-2025 corpus quality

- **Auditor:** Agent-D
- **Owner (tooling):** Lead
- **Date:** 2026-10-08 10:05 UTC
- **Repo HEAD:** e9890c7 (FINAL_REPORT decision-grade; multi-year corpus)
- **Verdict:** **Corpus data = PASS (hard checks). Committed quality claim = NOT
  VERIFIED — the Dukascopy checker is defective and its "0 weekend / 0 unexpected
  gaps" columns are vacuous.** Motto applied: re-derived everything; ran the
  committed tool itself.

---

## 1. What I verified and it holds

- **Row counts match `metadata_dukascopy_2021_2025.json` exactly**, per year and total
  **1,695,651** BID bars (344687 / 341226 / 342204 / 331293 / 336241).
- **Checksums:** from `research/data/xauusd_m1/`, `sha256sum -c
  checksums_dukascopy_2021_2025.sha256` → the five `*.csv.gz` and five
  `sample/*.head.csv` are **OK**. (The five raw `YYYY.csv` entries "FAILED open" only
  because they are gitignored and not committed — expected.) The committed,
  distributed artifacts are exactly what the checksums pin.
- **Hard checks re-derived independently** (my own parser, ms-epoch timestamps):
  reverse-ordered? no — strictly ascending all years; **0** duplicates; **0** OHLC
  violations; **0** NaN; **0** zero-volume; all minute-aligned. Price spans
  1670–4550 USD/oz across regimes (matches "1680..~4400" claimed).
- So: the **bars themselves are clean**, and the multi-year BID corpus is real and
  usable as ordered, gap-tolerant input.

## 2. DEFECT — `quality_check.py::check_year` never records any gap

In `research/data/xauusd_m1/tools/quality_check.py` the per-row loop does:

```python
prev = ts                         # <-- sets prev BEFORE measuring
...
if prev is not None:
    gap = ts - prev               # <-- always ts - ts == 0
    if gap > MINUTE_MS * 5:
        r["gaps"].append(...)     # <-- therefore never appends
```

`prev` is assigned `ts` earlier in the loop, so `gap` is always `0`; `r["gaps"]` is
**always empty**; the downstream weekend/unexpected counters are **always 0**.

Proof — I ran the committed functions, unmodified, on the decompressed corpus:

```
$ gzip -dc research/data/xauusd_m1/<year>.csv.gz > /tmp/duk/<year>.csv
$ python3 -c "import sys;sys.path.insert(0,'tools');import quality_check as q; \
              from pathlib import Path; print(q.check_year(Path('/tmp/duk/2024.csv')))"
... weekend=0 UNEXPECTED=0   (for every year)
```

**Consequences:**
- `QUALITY_dukascopy_2021_2025.md` columns "Weekend gaps | 0" and "Unexpected gaps |
  0" are **not measurements** — they are the vacuous output of a broken loop.
- `docs/FINAL_REPORT.md` asserts "**0 unexpected gaps**" for the multi-year corpus on
  this basis. That claim is **unverified**, not false-by-data — but it must not be
  cited as evidence.
- The **MT5** checker (`quality_check_mt5.py`) is unaffected — it indexes
  `timestamps[i] - timestamps[i-1]` correctly. The asymmetry is itself a red flag:
  the two quality tools disagree in capability.

## 3. The gap structure the broken loop hid (my independent pass)

| Year | weekend (~49–75 h) | midweek ~24 h | other midweek >2 h | ≈total unexpected |
|---|---|---|---|---|
| 2021 | 51 | 3 | 7 | 10 |
| 2022 | 51 | 6 | 6 | 12 |
| 2023 | 51 | 6 | 6 | 12 |
| 2024 | 50 | 9 | 10 | 19 |
| 2025 | 52 | 7 | 8 | 15 |

Representative non-weekend gaps:
- **3.5–5 h, on bank holidays** — e.g. `2021-01-18 17:59→23:00` (MLK), `2021-02-15`
  (Presidents), `2021-05-31` (Memorial), `2022-06-20` (Juneteenth observed),
  `2023-05-29`/`2024-05-27` (Memorial).
- **~24 h midweek** — `23:59 → 00:00` next-next-day, e.g. `2024-01-17 23:59 →
  2024-01-19 00:00` (Wed→Fri), `2022-05-03 23:59 → 2022-05-05 00:00`.
- **New-Year cluster** — e.g. `2021-04-01/02` 73–75 h Thu→Sun (Easter), year-end
  ~71 h.

These are **plausibly legitimate market closures** (holidays, Dukascopy maintenance),
not corrupt rows — no dup/OHLC/mono damage accompanies them. But: (a) they are
**not reported**; (b) the corpus is **1,695,651 bars for a window that would hold
~1.75 M M1 minutes**, i.e. ~55 k minutes are absent; (c) the committed tooling
**cannot** certify any of this. For a corpus whose whole purpose is the
**decision-grade** T27 verdict, "0 unexpected gaps" being a false-green is material.

## 4. Impact on T27 / FINAL_REPORT

- The feature engine tolerates gaps (it uses available bars, ordering-causal), so the
  **T27 numbers are not necessarily wrong** — but any claim that the corpus has "0
  unexpected gaps" must be withdrawn/re-derived.
- Recommend: (1) fix the `prev` ordering bug; (2) re-run and classify holiday/
  maintenance closures **explicitly** (a documented holiday calendar), updating
  `QUALITY_dukascopy_2021_2025.md`; (3) correct `FINAL_REPORT.md` §2. This tooling is
  in the **Lead's** zone (research data tooling), so I report, I do not fix.

## Independence

Agent-D authored no part of the Dukascopy corpus or its tooling. All numbers above
re-derived read-only; I invoked the committed `quality_check.py` unmodified to prove
the defect. No source touched; scratch files only under `/tmp`.

<!-- AI agent (OpenHands/agent-d) on behalf of the operator -->
