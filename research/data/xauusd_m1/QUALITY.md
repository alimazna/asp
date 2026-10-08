# XAUUSD M1 — Data Quality Report (operator MT5 export)

- Generated: 2026-10-08T07:47:45+00:00
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

