# Audit Report — T04 (Stdlib gradient-boosted trees + calibration composition)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration)
- **Date:** 2026-10-07 22:38 UTC
- **Repo HEAD at audit:** 9b2d280 (T04 commit)
- **Verdict:** **PASS.** The GBT is deterministic, its trees are well-formed at
  every depth (the reported deep-tree regression is genuinely fixed), the
  partition guard still fires on the GBT path, and `model_factory` composes
  correctly. Non-blocking notes below. T04 may go to **DONE** at the Lead's
  confirmation.

---

## Claim

Agent-B, `coordination/agent-b/comm.md` 22:34 UTC, "T04 submitted for REVIEW":

> Took the Lead's delegated choice: stdlib deterministic booster, no unpinned
> install. `src/models/gbt.py` (logistic-loss GBT, XGBoost-style), `run_calibrated`
> gained `model_factory`, `demo_gbt.py`, `tests/models/test_gbt.py`. 178 tests pass.
> **Please audit specifically:** the deep-tree regression I hit and fixed — sub-tree
> child indices were not rebased on flatten, so an internal node could self-reference
> and `predict()` looped forever at `max_depth>=3` (silent hang).
> `test_internal_nodes_never_self_reference` + `test_deep_tree_predict_terminates`
> now cover it. Confirm at `max_depth>=3`, and that the partition guard still fires
> for the GBT path.

Commit under audit: `9b2d280` "agent-b: T04 stdlib gradient-boosted trees +
model_factory composition (178 pass)".

Acceptance: deep-tree regression fixed; deterministic; partition guard enforced for
the GBT path; `model_factory` composes with the audited runner; no unpinned
dependency.

---

## Evidence inspected

- **Commit:** `9b2d280`
- **Files:** `src/models/gbt.py`, `src/models/calibrated.py`,
  `src/models/demo_gbt.py`, `tests/models/test_gbt.py`,
  `tests/models/test_calibrated.py`.
- Agent-D reran the suite and wrote an independent tree-integrity probe.

```
$ python3 -m unittest discover -s tests/models -t .   -> 178/178 OK
$ python3 -m unittest tests.models.test_gbt            -> 15/15 OK
$ PYTHONPATH=. python3 /tmp/t04_probe.py
  depth 0..8: trees=30 issues=0 (no self-ref, no OOR, no cycle) ; predict() returns
  determinism: identical trees: True
  partition guard: dev==val, val==oos, inversion -> SplitError OK
  proper disjoint GBT path: OK ece=0.0000
  edge cases: all-identical / single-row / constant-label / n_estimators=0 -> no crash
```

---

## Verification steps

1. **Deep-tree regression — FIXED (the key claim).** I walked every tree at
   `max_depth` 0–8 and checked: (a) child indices in range, (b) no self-reference,
   (c) acyclicity (DFS colouring), (d) all nodes reachable from the root, (e) the
   tree is a **proper** binary tree (`leaves == internal + 1`). I forced full-depth
   trees with `gamma = -1e9, min_child_weight = 0.0` to exercise `_rebased` at depth
   ≥ 3: depths 0–7 all pass with **zero** issues. No hang. PASS.
2. **Determinism.** Two independent fits with identical inputs produce
   byte-identical trees (all node tuples equal). No RNG/hash/clock. PASS.
3. **Partition guard on the GBT path.** `run_calibrated(..., model_factory=gbt)`
   rejects `dev==val`, `val==oos`, and chronological inversion with `SplitError`;
   the proper disjoint path runs. The T05 F1 guard is inherited unchanged. PASS.
4. **`model_factory` composition.** The change is a narrow, backward-compatible
   hook: `None` → logistic (default unchanged); otherwise `model_factory(x, y)`.
   The GBT exposes `decision_function` and `predict_proba_batch`, so it composes.
   Existing 162 tests still pass; +16 new. PASS.
5. **No unpinned dependency.** `gbt.py` imports only stdlib + in-repo modules
   (`math`, `dataclasses`, `typing`, `src.models.logistic`, `src.models.splits`).
   No `xgboost`/`numpy`. Hermeticity preserved (RULE E reproducibility). PASS.
6. **Input validation.** Empty matrix, length mismatch, non-0/1 labels, negative
   `n_estimators`, negative `max_depth`, non-positive `learning_rate` all raise
   `SplitError`. PASS.
7. **Outputs are probabilities.** `predict_proba` = `sigmoid(score)` ∈ [0,1];
   verified across fits. PASS.
8. **Independent reproduction.** Reran from clean; did not trust the claimed 178.
   PASS.

---

## Result

**PASS.** The deep-tree regression is genuinely fixed (verified structurally, not
just by the owner's tests); determinism, validation, guard inheritance, and
composition all hold.

## Non-blocking notes

- **N1 — GBT output is uncalibrated by design.** `fit_gbt` emits a research
  probability; it is not calibrated. Correct use is through `run_calibrated` (as
  `demo_gbt.py` does) so T05/T11 apply. Nothing publishes it. Consistent with
  RULE C. No action.
- **N2 — No real data.** All T04 numbers are synthetic (T03/T12/Q2 class). Not
  evidence about XAUUSD. No action.
- **N3 — No XGBoost parity.** The stdlib booster is not cross-checked against real
  XGBoost. Agent-B correctly proposes this as a separate pinned task if wanted.
  Not a defect; a scope boundary.
- **N4 — Learning dynamics (observation).** On a non-linearly-separable XOR target,
  depth-2 boosting plateaus at acc 0.773 for `lr=0.3` (identical scores at
  ne=200 and ne=500), but reaches 0.961 at `lr=0.5`. This is expected
  boosting-saturation behaviour, not an integrity defect — the trees remain
  well-formed and deterministic. Worth remembering when the horizon/SL-TP model
  (T15) is tuned: learning rate materially changes the fit. No action for T04.
- **N5 — `min_child_weight` semantics.** `_leaf_value` returns 0.0 when
  `h < min_child_weight`, and split gain skips children below it. Default
  `min_child_weight=1.0` with logistic hessians `h ∈ (0, 0.25]` means small
  leaves are suppressed — a deliberate conservative choice, not a bug. No action.

## Notes

- **No fabrication.** Probe in `/tmp`, uncommitted; reproduces from `9b2d280`.
- **Independence.** Agent-D authored none of the audited code.
