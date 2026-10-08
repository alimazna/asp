# XAUUSD M1 — Data Quality Report (operator MT5 export)

- Generated: 2026-10-08T08:00:24+00:00
- Source: MetaTrader 5 broker export, uploaded by the operator (XAUUSDM1.csv, UTF-16, no header)
- Converter: tools/convert_mt5.py -> xauusd_m1_real.csv
- Columns: timestamp,open,high,low,close,volume (MT5 spread column dropped)
- Timezone: **broker server time as written in the export** (not converted; no DST marker in the file). See README.md.

- Bars: **100008**
- Coverage: **2026-06-24 11:08:00 .. 2026-10-08 10:30:00** (broker time)
- Sessions gaps: 76 (of which weekend: 16)
- Zero-volume bars: 0

| Check | Result | Detail |
|---|---|---|
| Parsed successfully | PASS | 0 unparseable rows |
| Monotonic timestamps | PASS | strictly ascending |
| Minute-aligned timestamps | PASS | 0 off-grid |
| No duplicate timestamps | PASS | 0 duplicates |
| No NaN in OHLCV | PASS | 0 NaN rows |
| No OHLC violations | PASS | 0 violations |
| No unexpected gaps | PASS | 76 session gaps, 16 weekend, 0 unexpected |
| Price range plausible | WARN | observed 3942.48..4696.73 (directive band 1800..3000) |

**Verdict: PASS (hard checks) with a price-band WARN — the observed gold range is outside the directive's 1800-3000 band; this is a real market move, not a defect, and the operator's file is authoritative**

## Coverage limitation (read before using these numbers)

This corpus is a **single ~3.5-month window**; it supports a full real-data *proof-of-concept* run, **not** a decision-grade multi-regime walk-forward.

- Only one calendar regime is present, so the T27 chronological development/validation/OOS year partition (2021-2025) does **not** apply. Any split of this window is a **causal, in-window** split; the numbers are in-sample-ish evidence, not out-of-sample proof.
- A walk-forward at M15 cadence yields very few folds here; the T27 runner degrades honestly (records a note) rather than fabricating folds.
- The T27 result on this corpus is therefore reported as a **PROOF-OF-CONCEPT**, not the mission's final publication verdict.
