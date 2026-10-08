# ASTRA / AURA — Final Report (Lead)

> **Status: DRAFT — two sections pending.** This report is honest by
> construction: every number is either cited to an audited artifact or marked
> **PENDING**. Nothing here is estimated. It is finalized when T27 (real-data
> calibration) and T13 (evidential end-to-end) land and are audited.

- **Repo:** `alimazna/asp` @ `main`
- **Mission:** AURA (technical) / ASTRA (product) — a decision-support backend for
  **XAUUSD**, SHADOW mode only, no live trading.
- **Team:** 5 agents across 5 containers, Git-only coordination
  (`coordination/`).
- **Report owner:** DeepSeek (Lead).

---

## 1. What was built

| Area | Deliverable | Status |
|---|---|---|
| Market data | Operator MT5 M1 corpus → canonical CSV + quality report | ✅ delivered, audited (T25) |
| Features | M1→multi-TF feature sets, frozen `FeatureSet`, causality/no-lookahead | ✅ green (T26/A) |
| Model + calibration | Calibrated probability, Brier/ECE/reliability/coverage | ⏳ **PENDING T27** |
| Backend host | `aura_backend_host`, startup/shadow lifecycle, persistence | ✅ green (T09/T11) |
| Analysis API | Frozen v1 contract, 15 routes, loopback-only | ✅ frozen (T17, tag `api-v1.0`) |
| Mock | `scripts/mock_api.py`, frozen contract, `--check` 0 failures | ✅ (T18) |
| Frontend handoff | `docs/frontend/FRONTEND_HANDOFF_GUIDE.md` | ✅ (T16) |
| Decision model | Horizon + SL/TP, justified | ✅ (T15) |
| End-to-end | Real host + real data, evidential | ⏳ **PENDING T13 (D1)** |

## 2. Data — what we actually have

The operator uploaded a MetaTrader 5 **XAUUSD M1** export. It was converted with a
stdlib tool (`research/data/xauusd_m1/tools/convert_mt5.py`) to a canonical,
sorted, deduplicated CSV and validated (`quality_check_mt5.py` → `QUALITY.md`):

- **100,008** M1 bars, **2026-06-24 11:08 .. 2026-10-08 10:30** (broker time).
- **0** duplicates, **0** OHLC violations, **0** NaN, **0** off-grid timestamps,
  **0** unexpected gaps.
- Price band WARN: observed 3942.48..4696.73, outside the directive's
  1800..3000 — a real gold move, **reported, not repaired**.

**Coverage honesty (RULE D).** This is a **single ~3.5-month, single-regime
window**. It supports a **proof-of-concept** run of the whole pipeline on real
gold. It does **not** support a multi-regime chronological development /
validation / OOS split or a decision-grade walk-forward. Any split of this window
is a **causal, in-window** split and the numbers are in-sample-ish evidence, not
out-of-sample proof. This limitation is documented in the data `README.md`,
`QUALITY.md`, and the corpus `README.md`.

Features were extracted from the real corpus with the real C++ engine:
**6,670** decision sets, **2,497** valid, committed as
`research/features_real/corpus/real_corpus.json.gz`.

## 3. Real-data calibration — PROOF-OF-CONCEPT

> **PENDING T27 (Agent-B).** Ruled by the Lead (2026-10-08 08:05 UTC): the
> year partition (2021-2025) does **not** apply to a 2026-only window; T27 must use
> a **causal, in-window** split, apply the RULE C gate, and report the result as a
> **PROOF-OF-CONCEPT**, explicitly **not** the mission's publication verdict.

To be filled from the audited T27 report (Brier, ECE, reliability, coverage,
walk-forward or its honest "window too short" note), with the RULE C gate outcome.
**RULE C:** no probability is published unless it is calibrated and audited; if
the honest ECE does not clear the gate, the product presents a **score**, not a
probability (`score_is_probability=false`).

## 4. End-to-end — real host, real data

> **PENDING T13 (Agent-C + Agent-D).** The real-data path is wired and
> reproducible; it is blocked on a **foundation defect D1** (`Json.cpp`
> `Parser::parseNumber` typed every parsed number as a string). Independently
> reproduced by the Lead; fix **authorized** under `DEC-021` and in progress.
> D2/D3 (conditional `levels` / `proposal_reason`) ruled under `DEC-022`.

What already passes on the **real binary** with **no data** (honest DEGRADED
posture): T13 host harness **52/52**.

## 5. Verification evidence (all independently auditable)

| Suite | Result |
|---|---|
| C++ `ctest` | **18/18 pass** |
| Frozen-contract v1 e2e (T24) | **88/88** |
| Analysis-API schema fixtures (T22) | **52/52** |
| Mock API (T19) | **39/39** |
| Contract checker (T16) | **36/36** |
| T28 data paths | **20/20** |
| T30 shape guard | **19/19** |
| Models suite | **277 OK** |
| Features suite | **13/13** |
| Mock `--check` | **0 failures** |

Audits live in `AUDIT_REPORTS/` (Agent-D). Key ones: T29 Part 1 (corpus +
pipeline **byte-reproducible**, provenance closed), T30 (F23-3 fixtures refreshed,
teeth two-sided and green).

## 6. Baseline & production

- Baseline controls (`base9`, `baseold`) and `docs/archive/` are **untouched**
  (verifiable by `git log`).
- **No live-trading path** exists; mode is SHADOW.
- The only authorized production `src/` change is the **D1 bugfix** (`DEC-021`),
  a defect fix in our own code, wire-shape byte-unchanged, regression-tested.

## 7. Frontend readiness

The frontend can build against the **frozen v1 contract** today using
`scripts/mock_api.py` (offline), then swap to the live loopback host. The handoff
guide documents all **15** routes, field-by-field interpretation, the
`null`/`UNKNOWN` = *unavailable* rule, error handling (OFFLINE/DEGRADED/STALE),
and integration rules (frontend reads the API only).

## 8. Verdict

**PENDING** — finalized once T27 and T13 land. Honest posture now: the backend,
contract, docs, and real-data feature pipeline are **complete and green**; the
real-data calibration number and the evidential real-data end-to-end are **not yet
published**. Per mission §10.7, "complete" does **not** require profit — it
requires an honest calibration result, a stable API, a complete guide, and
frontend-readiness. The honest calibration result is the one missing piece.

<!-- Draft by the Lead (DeepSeek) agent, on behalf of the operator. -->
