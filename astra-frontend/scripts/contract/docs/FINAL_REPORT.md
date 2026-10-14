# ASTRA / AURA — Final Report (Lead)

> **Status: DRAFT — two sections pending.** This report is honest by
> construction: every number is either cited to an audited artifact or marked
> **PENDING**. Nothing here is estimated. It is finalized when T27 (real-data
> calibration) and T13 (evidential end-to-end) land and are audited.
>
> **Update 2026-10-08 08:40 UTC:** the multi-regime **Dukascopy 2021-2025**
> corpus was found complete (it had been gitignored/invisible) and committed, so
> T27 is now **decision-grade** on the real year partition — not a POC-only run.
> The 3.5-month MT5 window becomes the POC / independent cross-check.

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
| Model + calibration | Calibrated probability, Brier/ECE/reliability/coverage | ✅ decision-grade, **no edge** (T27); audit pending T29 Part 2c |
| Backend host | `aura_backend_host`, startup/shadow lifecycle, persistence | ✅ green (T09/T11) |
| Analysis API | Frozen v1 contract, 15 routes, loopback-only | ✅ frozen (T17, tag `api-v1.0`) |
| Mock | `scripts/mock_api.py`, frozen contract, `--check` 0 failures | ✅ (T18) |
| Frontend handoff | `docs/frontend/FRONTEND_HANDOFF_GUIDE.md` | ✅ (T16) |
| Decision model | Horizon + SL/TP, justified | ✅ (T15) |
| End-to-end | Real host + real data, evidential | ✅ **green (T13 96/96)** |

## 2. Data — what we actually have

Two real corpora, both validated with a stdlib tool and documented (never
silently repaired):

- **Dukascopy 2021-2025** (BID+ASK) — the **decision-grade** corpus:
  1,695,651 M1 BID bars, 2021-01-03..2025-12-30, spanning several gold regimes
  (~1670..~4550 USD/oz). Bar integrity is clean: 0 duplicates, 0 OHLC violations,
  0 non-monotonic timestamps, 0 off-grid bars. The venue's **68 off-session
  closures** (bank holidays + maintenance, 3.5h..98h) are **reported and listed**,
  not silently labelled "expected" — an earlier checker bug made that count
  vacuously zero; fixed and re-derived (see §5 / AUDIT-T29). Committed as
  deterministic per-year gzip (`2021..2025.csv.gz`, BID+ASK).
  (`QUALITY_dukascopy_2021_2025.md`.)
- **Operator MT5 2026** — an independent, newer 3.5-month window:
  100,008 M1 bars, 2026-06-24 11:08..2026-10-08 10:30 (broker time), 0 dups /
  0 OHLC violations / 0 NaN / 0 off-grid / 0 unexpected gaps. Price band WARN:
  observed 3942..4697, outside the directive's 1800..3000 — a real gold move,
  reported not repaired. (`QUALITY.md`.)

The Dukascopy corpus is the one T27's chronological **development (2021-22) /
validation (2023-24) / OOS (2025)** partition was designed for; it supports a
multi-regime walk-forward. The MT5 window is the **proof-of-concept / independent
cross-check** on a second provider and a newer period.

Features were extracted from both with the real C++ engine:

- `real_corpus_2021_2025.json.gz` — 1,695,651 bars → **113,083** decision sets
  (**107,403** valid), all years 2021-2025. **Decision-grade.**
- `real_corpus.json.gz` — 100,008 bars → 6,670 sets (2,497 valid). **POC.**

## 3. Real-data calibration — DECISION-GRADE (Dukascopy 2021-2025)

**Config:** real C++ `FeatureSet` corpus `real_corpus_2021_2025.json.gz`
(113,083 sets / 113,082 examples), horizon=1, Platt calibration, ridge `--l2 0.05`
(a convergence aid at p=185 features, **not** tuned — proof below). Split by real
calendar year: **dev 2021-22 = 45,735 / val 2023-24 = 44,922 / OOS 2025 = 22,425**.

**OOS 2025 (calibrated):** Brier **0.2497**, skill **+0.0012**, ECE **0.0015**,
MCE 0.0053, accuracy **0.5170**. Raw (uncalibrated) Brier 0.2515, ECE 0.0314 —
calibration is doing real work (ECE 0.031 → 0.0015).
**Walk-forward:** 56 folds, pooled n=28,000, non-overlapping; pooled Brier 0.2551,
ECE **0.0454**, accuracy 0.5021.
**RULE C verdict:** **probability** — both ECEs < 0.05, pending T29 Part 2c.
**RULE D coverage:** `low`/`high` tiers = **zero coverage** (the model never becomes
confident); `medium` = 1.000. MCE ≫ ECE — a tier is worse than average; flagged.
**RULE E — honest negative (the important part):** Brier skill ≈ **+0.0012**, i.e.
the calibrated score is **essentially a coin flip** (base rate ≈ 0.5 ⇒ Brier ≈
0.25). Calibration is excellent; **discrimination is ~zero**. The pipeline
calibrates; the model does not yet predict — it carries **no claimed edge**.

**Independent reproduction (Lead).** I ran the same corpus and split at a
**different ridge, `--l2 0.01`**, and got identical numbers — Brier 0.2497, skill
0.0012, ECE 0.0012, accuracy 0.517; raw 0.2515 / 0.0314. This confirms `--l2` is a
convergence **speed** knob, not a result knob. Runtime ≈ 17 min, single process.

## 3b. POC cross-check (MT5 3.5-month, in-window fraction split)

Reproduced by the Lead **and** Agent-D, byte-for-byte, pending audit. OOS
calibrated Brier **0.2499**, ECE **0.0017** (n=1339); raw 0.2915 / 0.1774.
Walk-forward: 63 folds, pooled Brier 0.2546, ECE **0.0489**, accuracy **0.4992**.
`low`/`high` coverage 0. Same honest conclusion: excellent calibration, weak
discrimination. A second provider, a newer period — the cross-check agrees with the
decision-grade result.

## 4. End-to-end — real host, real data — **PASS (T13 96/96)**

The real-data path runs the **real binary** on **real data** through the full
stack. D1 (`Json.cpp` typed every parsed number as a string) was fixed under
`DEC-021`; D2 (conditional `levels` / `proposal_available`) under `DEC-022`; D3
under `DEC-023`. The Lead independently reproduced the D1 probe (a JSON literal
number now reports `isNumber=1`, `asDouble=12`) and `ctest` 19/19.

**T13 evidential = 96/96 — PASS**, independently reproduced by Agent-D in two
environments (its workspace **and** a fresh clone + fresh CMake build), and
re-verified after the harness fix below. With **no data** the same harness reports
an honest **DEGRADED** posture, **52/52** — nothing fabricated.

Two harness defects were found by audit and closed (no product code changed):

- **Bridge port-leak (fixed, Agent-C, `f34839e`).** The host-spawned
  `bridge_service.py` binds fixed port 8791; the real-data run killed the host but
  not the child, so a second run hit `EADDRINUSE` and **false-failed 91/96**. Now
  the host runs in its own process group and `reap()` `killpg`s it; two back-to-back
  runs give **96/96, 0 stray processes**.
- **Corpus-mix (open, minor).** `load_corpus(DIR)` globs both `.json` and `.json.gz`,
  so the corpus dir would silently concatenate the MT5 and Dukascopy providers.
  Run T27 with one explicit corpus file; a resolver fix is requested.

## 5. Verification evidence (all independently auditable)

| Suite | Result |
|---|---|
| C++ `ctest` | **19/19 pass** |
| Frozen-contract v1 e2e (T24) | **88/88** |
| Analysis-API schema fixtures (T22) | **52/52** |
| Mock API (T19) | **39/39** |
| Contract checker (T16) | **36/36** |
| T28 data paths | **20/20** |
| T30 shape guard | **19/19** |
| Models suite | **298 OK** |
| Features suite | **14/14** |
| T13 evidential (real host + real data) | **96/96 PASS** |
| Mock `--check` | **0 failures** |

Audits live in `AUDIT_REPORTS/` (Agent-D). Key ones: T29 Part 1 (corpus +
pipeline **byte-reproducible**, provenance closed), T30 (F23-3 fixtures refreshed,
teeth two-sided and green), T29 Part 2a (Dukascopy corpus — **found and closed a
false-green**: the gap loop read `prev` after advancing it, so off-session
closures were never counted; fixed, re-derived to 68 listed closures, no bar
damage).

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

**PENDING ONE AUDIT.** Everything except the T29 Part 2c audit of the calibration
number is complete and green:

- Backend host, frozen v1 API contract (15 routes, loopback-only), mock, frontend
  handoff guide — **complete and green**.
- Real-data pipeline (Dukascopy 2021-2025 + MT5) — **validated, byte-reproducible**.
- T13 evidential end-to-end (real binary + real data) — **96/96 PASS**, reproduced
  independently.
- T27 calibration — **decision-grade** on the real year partition: OOS Brier 0.2497,
  ECE 0.0015, accuracy 0.5170; walk-forward ECE 0.0454. **Honest negative:** Brier
  skill ≈ +0.0012 — the score is **essentially a coin flip**; calibration is
  excellent but **discrimination is ~zero**. No edge is claimed.

Per mission §10.7, "complete" does **not** require profit — it requires an honest
calibration result, a stable API, a complete guide, and frontend-readiness. Those
now exist. The single remaining gate is **T29 Part 2c** (independent audit of the
T27 numbers, the split boundaries, and the `--l2` choice), after which this verdict
is finalized. The honest headline: **a working, calibrated, honestly-measured
decision-support backend whose probability model has no demonstrated predictive
edge on XAUUSD.**

<!-- Draft by the Lead (DeepSeek) agent, on behalf of the operator. -->

