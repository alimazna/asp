# Audit Report — T29 (Part 1): real-data corpus & feature pipeline

- **Auditor:** Agent-D
- **Owners:** Lead (T25 corpus), Agent-A (T26 feature corpus)
- **Date:** 2026-10-08 09:15 UTC
- **Repo HEAD:** (Phase 5.2, post-`86a07f3`/`f8a3bd3`)
- **Verdict:** **PASS (corpus + feature pipeline).** Every artifact I could verify
  independently is **byte-reproducible** and every sampled number re-derives from the
  raw bars. Part 2 (calibration numbers) is **pending T27**. Motto applied: *do not
  trust the pipeline — re-derive.*

---

## 1. Corpus (`research/data/xauusd_m1/xauusd_m1_real.csv`)

**Checksum verified** — `sha256sum -c checksums.sha256` → both files OK.

**Independent quality re-derivation** (my own parser, not the tool):

| property | my count | QUALITY.md | agree |
|---|---|---|---|
| rows | 100,008 | 100,008 | ✅ |
| first .. last | 2026-06-24 11:08:00 .. 2026-10-08 10:30:00 | same | ✅ |
| duplicates | 0 | 0 | ✅ |
| non-monotonic | 0 | 0 | ✅ |
| not-minute-aligned | 0 | 0 | ✅ |
| OHLC violations | 0 | 0 | ✅ |
| NaN | 0 | 0 | ✅ |
| zero-volume | 0 | 0 | ✅ |
| gaps >5m | 76 | 76 | ✅ |
| weekend gaps | 15 | 16 | ⚠️ definitional |
| unexpected gaps | 0 | 0 | ✅ |
| price range | 3942.48 .. 4696.73 | same | ✅ |

**Gap classification independently characterised:** of the 76 gaps, 60 are ≤2.1 h
(the ~2 h daily maintenance break: gaps 23:0x→01:00), 15 are the Fri 22:59 → Mon
01:00 weekend closure (~50 h), and exactly **one** is a 3.52 h gap
(`2026-09-07 21:29 → 2026-09-08 01:00`, Mon→Tue) — a wider maintenance session, not
a data hole. **No trading session is lost.** The tool's `weekend=16` counts that
3.52 h gap as "weekend" because its threshold is `>ROUTINE_GAP_H (3.0)`, whereas my
Saturday-detection classified it non-weekend — a **cosmetic** definitional difference
only; both agree there are **0 unexpected gaps** and no repair is needed.

**Price-band WARN is honest and correctly not repaired:** observed 3942–4697 is
outside the directive's 1800–3000 band, reported as WARN, operator file treated as
authoritative for its own window. I agree with the reporting posture.

## 2. End-to-end reproducibility (the strongest check)

```
raw XAUUSDM1.csv (UTF-16, operator MT5 export)
  -> convert_mt5.py                -> xauusd_m1_real.csv   [BYTE-IDENTICAL to committed]
  -> run_features.py (C++ engine)  -> real_corpus.json     [BYTE-IDENTICAL to committed]
```
- Re-ran `convert_mt5.py --src XAUUSDM1.csv`: 100,008 parsed, 0 skipped, 0 dups →
  output **byte-identical** to the committed canonical CSV.
- Re-ran `run_features.py --m1 <corpus> --decisions 0`: 100,008 bars → 6,670 sets
  (2,497 valid) → `cmp` vs the gunzipped committed `real_corpus.json` →
  **BYTE-IDENTICAL** (both 41,923,413 bytes).

So the committed data and the committed feature corpus are exactly what the committed
tools produce from the committed raw export. Provenance is closed.

*(Minor, non-blocking:* the committed `real_corpus.json.gz` is a valid gzip of that
JSON; its gzip bytes are not identical to `gzip -9` output — `cmp` on content is
identical. README's "deterministic gzip" claim holds for `pack.py`'s `mtime=0` writer,
not necessarily for a plain `gzip -9`; I flag it, it does not affect content.)*

## 3. T26 FeatureSet corpus — content spot-checks (independent re-derivation)

- **Decision-label causality:** the top-level `close` label equals the raw M15 close
  of the bar at `asOfBarOpenSec - 900` for **6,670 / 6,670** sets (0 mismatch, 0
  missing). The label is the decision bar's own close — **no lookahead**.
- **M1 candle features re-derived from the raw bar** at `asOf=1782494100`:
  ```
  raw bar (open=asOf-60): O=4084.24 H=4085.05 L=4082.15 C=4082.20
  computed: bodyRatio=0.703448276 upperWick=0.279310345 lowerWick=0.017241379 dir=-1
  emitted : bodyRatio=0.703448276 upperWick=0.279310345 lowerWick=0.017241379 dir=-1
  ```
  Exact match — the engine's candle math is not fabricated.
- **Structures are honest:** `perTimeframe` is 9 objects (M1..MN1) each with
  `timeframe, asOfBarOpenSec, barsAvailable, windowUsed, quality, valid, values`;
  `cross` reports `h4M15Agreement/h4D1Agreement/mtfConflictScore/h4StructuralAuthority/
  m15TriggerState` with a `detail` explaining `D1 unavailable`. Early sets are honestly
  `INCOMPLETE` (thin warm-up), not fabricated — 2,497/6,670 valid.

## 4. Corpus timezone / window caveat (carried, not a defect)

The corpus is 2026-06-24 .. 2026-10-08 in **unlabelled broker server time** (the
exporter has no TZ marker; `convert_mt5.py` refuses to guess an offset — correct
call). Consequences that must be held honestly in the T27/T29 reports:
- This is **not** the originally-declared 2021–2025 Dukascopy window; it is the
  operator's single ~3.5-month MT5 window. Any "3-month / 9-timeframe methodology"
  claim now rests on ~3.5 months of one broker's M1.
- Broker time vs UTC can shift session boundaries; the pipeline is ordering-causal,
  so labels remain internally consistent, but cross-source comparability is limited.
- T27's dev/val/OOS split (2021–25) does not apply; the Lead directed a
  window-relative split — I will check that it is **causal** (no OOS tuning) in Part 2.

## 5. Part 2 — pending

When T27's real-data calibration report lands I will audit: split causality, Brier,
ECE, reliability, coverage, walk-forward, and the RULE C gate (ECE<0.05 probability /
0.05–0.10 score / >0.10 escalate). Nothing trusted on faith.

## Independence

Agent-D authored none of T25/T26. All numbers above re-derived with my own parser /
re-runs at the stated HEAD; no source touched. Reports under `AUDIT_REPORTS/`.
