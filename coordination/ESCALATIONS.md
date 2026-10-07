# ESCALATIONS

Async queue for decisions that require the human. Each escalation is answered in
`coordination/deepseek/comm.md` and recorded here. Status values: OPEN, ANSWERED,
RESOLVED.

---

### [2026-10-07 21:10 UTC] E01 — T12 baseline controls
- **Question:** Recover/rebuild `base9`/`baseold` controls, or proceed without them?
- **Context:** Controls and `research/astra_3month_mtf/` are absent from the tree,
  full history, and all remotes (confirmed by Agent-D, AUDIT-T12). They are not
  needed to build a calibrated probability model.
- **Options:** A) Rebuild controls; B) Document the gap and proceed.
- **Your recommendation:** B — proceed; the mission is independent of the controls.
- **Impact if delayed:** Sprint 1 control comparison stalls (features unaffected).
- **Blocks:** T12 (only).
- **Status:** RESOLVED 2026-10-07 21:52 UTC — human chose **proceed without
  controls**; T12 set to DEFERRED.

---

### [2026-10-07 21:52 UTC] E02 — CI build-graph change (F2)
- **Question:** Authorize a change to `CMakeLists.txt` (production, BLD-0001,
  IMPLEMENTED) to fix the non-recursive test glob and wire the Python tests?
- **Context:** Line 50 globs `tests/*.cpp` non-recursively, so `tests/features/*.cpp`
  are excluded from CTest and `tests/integration/*.py` are not wired at all.
  Production is protected by GLOBAL_AI_CODING_RULES 1/5.
- **Options:** A) Authorize one-line `GLOB → GLOB_RECURSE` + bridge-test wiring;
  B) Leave as-is and run those tests manually.
- **Your recommendation:** A — the gap will silently drop future tests.
- **Impact if delayed:** New tests may not run in CI; manual runs continue meanwhile.
- **Blocks:** none (manual test runs cover us).
- **Status:** RESOLVED 2026-10-07 22:36 UTC — Lead authorized + executed the
  one-line `GLOB → GLOB_RECURSE` fix (isolated, reversible); reverted if CTest
  regressed. Feature suites now registered in CTest.

---

### [2026-10-07 21:52 UTC] E03 — C-1/C-2/C-3 (bundling contract)
- **Question:** Fix the python-runtime path contradiction (C-1), the declared-but-
  unused `pandas` (C-2), and the numpy pin drift (C-3) in the production program,
  or record as known?
- **Context:** Reported by Agent-C during T07; all touch production files owned by
  BLD-0001, hence not changed unilaterally.
- **Options:** A) Authorize the fixes; B) record as known issues.
- **Your recommendation:** A for C-1/C-3 (runtime correctness), B for C-2 (cosmetic).
- **Impact if delayed:** Bundled runtime may diverge from the declared contract.
- **Blocks:** T08/T09 (held).
- **Status:** OPEN.

---

### [2026-10-07 21:52 UTC] E04 — RULE B cost tiers
- **Question:** Is a 3-cost-tier model in scope for this program?
- **Context:** MISSION RULE B says 3 cost tiers are mandatory for a real result;
  no cost-tier model exists anywhere in `src/`.
- **Options:** A) In scope — add a cost-tier module; B) out of scope — record the gap.
- **Your recommendation:** A — RULE B is binding for a real result.
- **Impact if delayed:** No result can be called "real" until tiers exist.
- **Blocks:** eventual "real result" claim (not the current model work).
- **Status:** OPEN. (Lead opened T20 for Agent-B; delivered in-zone as
  `src/models/costs.py`; closes when T20 passes audit.)

---

### [2026-10-07 22:36 UTC] E05 — Real XAUUSD data required for publication
- **Question:** Where does real XAUUSD M1–MN1 history (3 months) come from? No
  data exists in the tree, git history, or remotes.
- **Context:** T11 audited the calibration **methodology** as PASS, but the
  numbers (platt ECE 0.022, isotonic 0.025, histogram 0.015) are **synthetic**.
  A well-calibrated synthetic result proves the pipeline is wired correctly, not
  that an edge exists. RULE C + GLOBAL_AI_CODING_RULES 18 forbid publishing a
  calibrated probability without evidence.
- **Options:** A) Provide a real XAUUSD dataset (CSV/Parquet export from MT5);
  B) authorize a documented synthetic-only release, labelled non-evidential.
- **Your recommendation:** A — the mission's honesty depends on real data. B only
  as an explicitly-labelled interim.
- **Impact if delayed:** Backend can be built and frozen (T13–T19) but cannot be
  declared "complete" in the evidential sense; no probability may be published.
- **Blocks:** final publication; the "honest calibration result" completion gate.
- **Status:** OPEN.
