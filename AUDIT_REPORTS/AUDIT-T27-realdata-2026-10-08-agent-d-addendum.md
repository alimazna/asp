# AUDIT (ADDENDUM) — T27 real-data calibration (PROOF-OF-CONCEPT, single window)

> **Addendum to the Lead-substitute audit** (`AUDIT-T27-realdata-2026-10-08.md`).
> The Lead declared MISSION CLOSE at 11:05 UTC reading Agent-D STALE; my
> independent pass completed just after and is filed here as the genuinely
> independent re-audit the Lead's close message explicitly invited ("a genuinely
> independent re-audit is welcome — addendum, not a blocker"). It **corroborates
> every Lead/Agent-B number to the digit** and adds one material finding the
> substitute audit did not cover (§6, corpus INCOMPLETE).

- **Auditor:** Agent-D (Verification & Audit) — independent
- **Date:** 2026-10-08
- **Repo HEAD audited:** `31ac614` (branch `main`), fresh clone
- **Artifacts audited:** `research/features_real/t27_poc_report.json`,
  `research/features_real/T27_POC_NOTE.md`, `src/models/realdata.py`,
  `src/models/calibrated.py`, `src/models/dataset.py`,
  `research/features_real/corpus/real_corpus.json.gz`
- **Verdict:** **APPROVED** — every published number reproduces **to the digit**;
  split causality is sound; nothing is fabricated. **One blocking disclosure
  finding (F-T27-1)** and one publication-posture caveat. See §7.

Directive reference: `coordination/deepseek/comm.md` [10:10 UTC] FINAL DIRECTIVE.
I audited none of the code I am judging (Agent-A/B/C own the corpus, loader,
model, calibrator, report).

---

## 1. Independent reproduction (fresh clone)

Reproduced two ways: (a) the committed CLI path
`python3 -m src.models.realdata --corpus .../real_corpus.json.gz --partition-mode
fraction --wf-train 300 --wf-test 100 --l2 1e-6`; (b) a from-scratch in-process
script mirroring `run_calibrated` (load → split → `fit_logistic` → Platt →
`calibration_report`).

| Quantity | Committed report | My CLI run | My in-process run |
|---|---|---|---|
| partitions dev/val/oos | 3997 / 1333 / 1339 | **same** | **same** |
| OOS n | 1339 | 1339 | 1339 |
| OOS Brier (calibrated) | 0.249945 | **0.249945** | **0.249945** |
| OOS Brier skill | +0.000218 | **+0.000218** | **+0.000218** |
| OOS ECE (10 bins) | 0.00178 | **0.00178** | **0.00178** |
| OOS MCE | 0.00178 | **0.00178** | **0.00178** |
| OOS accuracy (positive rate) | 0.4922 | 0.4922 | **0.4922** |
| Verdict | probability | probability | — |

**All numbers reproduce exactly.** The report is not overclaiming on its own
terms.

## 2. Reliability diagram attack

The calibrated OOS probabilities span only **[0.4857, 0.4955]** — the entire OOS
set falls in **one 10-bin bucket, [0.40, 0.50)** (n=1339, mean_pred 0.4904,
empirical 0.4922, gap +0.0018). My independent diagram is identical.

**Assessment.** ECE 0.0018 is real but **trivially earned**: the model outputs
essentially the base rate (~0.49), so predicted ≈ observed by construction. This
is a *degenerate* reliability diagram — one bin, no spread, **no discriminative
signal**. The MCE==ECE equality is a symptom of the single occupied bin. The
report states this ("all OOS mass in one bin"), and Agent-B's note correctly
reframes the formal "probability" as a practical **score**. Honest.

## 3. Tier thresholds (p≥0.55 / 0.60 / 0.65)

Independently recomputed: **0 / 0 / 0 coverage** — no OOS probability reaches
0.55 (max 0.4955). Matches the report. RULE D empty-tier honesty holds.

## 4. Directional accuracy vs LONG/SHORT

- Positive-rate accuracy: **0.4922** (n=1339). Report's note table says 0.508.
- LONG (p≥0.5): **n=0** (max probability 0.4955 — **no OOS point is ever a long**).
- SHORT (p<0.5): **n=1339**, accuracy 0.4922.

**Finding (minor, F-T27-2).** The note's "Directional accuracy 0.5078" is
**1 − 0.4922** (correct only under the inverted "SHORT wins when price falls"
convention). As plain "predicted direction = actual" it is **0.4922**, i.e. below
a coin flip. The two figures coexist in the same artifact (headline 0.5078,
LONG/SHORT table 0.4922) with no stated convention. Cosmetic but should be pinned.

## 5. Causality / no-lookahead of the single-window split

- `dataset.build_labeled_examples` is causal: label at instant *i* reads close at
  *i+horizon*; the last `horizon` snapshots are dropped. `labels.py` future
  mutation is covered by T21.
- `assert_partitions_separated` is called inside `run_calibrated` **and**
  `_fit_calibrate_eval` — overlap/inversion is a hard error, so dev/val/OOS are
  pairwise disjoint by construction. Verified: n_examples 6,669 vs 6,670 instants
  (last row dropped for horizon).
- `load_corpus` enforces strictly-increasing timestamps and rejects duplicates.
- **No lookahead found.** The single-window caveat (no held-out period) is
  correctly labelled; no walk-forward is claimed. Good.

## 6. The corpus is 63% INCOMPLETE — development partition is 100% INCOMPLETE

**This is the material finding (F-T27-1, blocking for the headline).**

The MT5 corpus has **6,670 sets, of which only 2,497 are `valid`** (corpus
README + my count). The other **4,173 are honest `INCOMPLETE` warm-up sets**, and
they are a **contiguous block at the window start**:

- invalid indices **0 .. 4172** (of 6,670); first valid at index **4173**.
- invalid span 2026-06-24 11:15 .. 2026-08-28 23:00; valid 2026-08-31 .. 2026-10-08.

`realdata.py` has **no `valid` filter** — it calibrates on all rows. Mapping the
fraction split through the real code path:

| Partition | rows | valid | INCOMPLETE |
|---|---|---|---|
| development | 3997 | **0** | **3997 (100%)** |
| validation | 1333 | 1157 | 176 |
| OOS | 1339 | 1339 | 0 |

**The base logistic model is trained entirely on warm-up-INCOMPLETE rows.** The
calibrator is fit on a validation set that is 13% INCOMPLETE. The reported OOS is
clean (all valid), but its "probability" verdict depends on a base model fit on
data the pipeline itself flags `INCOMPLETE`.

### Sensitivity — decisive

Same pipeline, restricted to the **2,497 valid sets the directive names**:

| Corpus | n_instants | dev/val/oos | OOS Brier | skill | **OOS ECE** | **RULE C verdict** |
|---|---|---|---|---|---|---|
| full (6,670, incl. 4,173 INCOMPLETE) | 6670 | 3997/1333/1339 | 0.24995 | +0.0002 | **0.0018** | **probability** |
| valid-only (2,497) | 2497 | 1493/505/498 | 0.2616 | −0.047 | **0.1070** | **report_and_pivot** |

With only the valid sets, OOS calibrated **ECE = 0.1070 > 0.10** → RULE C is
**`report_and_pivot`: calibration does not hold; do not present as a probability.**
The headline "probability" verdict is therefore an **artifact of training on
INCOMPLETE rows**; the honest valid-data result is a **pivot**.

Neither the report nor `T27_POC_NOTE.md` discloses the INCOMPLETE counts or the
valid-only sensitivity. The directive itself says "100,008 M1 bars; **2,497 valid**
FeatureSets" — so the audited artifact contradicts the directive's own corpus
description.

## 7. Verdict

**APPROVED** on reproducibility and causality: every published number
reproduces to the digit in a fresh clone, the split is causal and leakage-guarded,
and the honest-negative framing (skill ≈ 0, coin flip, zero confident coverage) is
correct — audits are doing their job.

**Publication posture (RULE C).** Formally ECE 0.0018 < 0.05 → *probability*. But
that gate is met only on the INCOMPLETE-inclusive corpus; on the 2,497 valid sets
the verdict is **`report_and_pivot` (ECE 0.107)**. I therefore rule the defensible
publication value is a **SCORE, not a probability** — which is already what the
live API does (`signal.probability = null`, `probability_calibrated = false`).

**Required before the headline is final (F-T27-1, blocking):** the report/note
must (a) disclose the 4,173 INCOMPLETE sets and that **development is 100%
INCOMPLETE**, and (b) record the valid-only sensitivity (ECE 0.107 → pivot). The
Lead may then publish with the POC label and the score posture. If the corpus is
taken to be the 2,497 valid sets, the verdict is **pivot**, not probability.

**Minor (F-T27-2, non-blocking):** pin the directional-accuracy convention
(0.4922 plain, not 0.5078) and keep LONG/SHORT consistent with it.

## 8. What I could not fully verify

- The valid-only in-process run (my second computation) had not returned at
  filing time; the valid-only CLI run (`report_and_pivot`, ECE 0.10696) and the
  full in-process run both completed and are cited above.
- `realdata.py` does not persist `l2` in the report; the directive's default-l2
  run is inferred from the artefact (ECE 0.00178 differs from the earlier l2=0.05
  run's 0.0017). Recommend storing `l2` in the report for auditability.

---

## 9. Cross-check vs the Lead-substitute audit

The Lead's substitute audit (`9ea21ab`) reproduced the same POC metrics on a
separate path (`research/reports/tools/poc_metrics_lead.py`) and reached the same
verdict (SCORE; no edge). We agree on every substantive number. Two differences
worth recording:

1. **Tiny metric drift.** The Lead's ECE/skill read 0.001699 / approx -0.00003 vs
   the report's 0.00178 / +0.000218 — attributable to the ridge (`l2=0.01` vs the
   report default `1e-6`), as the Lead notes. My runs at the **default `l2=1e-6`**
   reproduce the report's numbers **exactly** (ECE 0.00178, skill +0.000218),
   confirming the report used the default.
2. **The INCOMPLETE finding (§6) is absent from both the Agent-B report/note and
   the substitute audit.** Neither discloses that 4,173/6,670 sets are INCOMPLETE
   or that the development partition is 100% INCOMPLETE, and neither reports the
   valid-only sensitivity (ECE 0.107 → pivot). This addendum records it so the
   close-out file is complete for any downstream reader.

**Bottom line for the close-out.** The POC pipeline is correctly wired and its
number is reproducible — I APPROVE that. The publication value is correctly stated
as a **SCORE** with no demonstrated edge. My only addition is that the headline
"probability/ECE 0.0018" rests on training on INCOMPLETE rows; on the **2,497 valid
sets the directive names**, the honest RULE C outcome is **`report_and_pivot`**.
Recommend the FINAL_REPORT state this in the §3/T27 caveat; it does not change the
SCORE verdict.

<!-- AI agent (OpenHands/agent-d) on behalf of the operator -->
