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
- **Status:** OPEN. (Lead asked Agent-C to scope the minimal in-zone resolution;
  C-1 runtime-path correctness + C-3 numpy pin worth fixing, C-2 cosmetic recorded.)

---

### [2026-10-07 21:52 UTC] E04 — RULE B cost tiers
- **Question:** Is a 3-cost-tier model in scope for this program?
- **Context:** MISSION RULE B says 3 cost tiers are mandatory for a real result;
  no cost-tier model exists anywhere in `src/`.
- **Options:** A) In scope — add a cost-tier module; B) out of scope — record the gap.
- **Your recommendation:** A — RULE B is binding for a real result.
- **Impact if delayed:** No result can be called "real" until tiers exist.
- **Blocks:** eventual "real result" claim (not the current model work).
- **Status:** **RESOLVED 2026-10-08 00:12 UTC.** Agent-B delivered the RULE B tiers
  in-zone (`src/models/costs.py`); Agent-D audited **PASS** (three tiers correct
  and ordered; `cost_r`/`net_expectancy_r` verified; `levels.py` refactor a true
  dedup). Non-blocking F20-1 (reject non-finite assumptions) fixed by Agent-B.
  E04 closed.

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

---

### [2026-10-08 00:12 UTC] E06 — impl-vs-schema binding (T17 F17-1) — Lead decision
- **Question:** The frozen schema claims it is "machine-checked", but only the
  **mock** is checked against it; nothing pins the **real backend output** to the
  schema. Add a real-facade schema check, or generate mock + contract from one
  source of truth?
- **Context:** Raised by Agent-D in the T17 audit. Currently a backend payload
  could silently drift from `API_V1_SCHEMA.json`.
- **Lead ruling:** **Add a machine check that validates real `BackendFacade`
  output against `API_V1_SCHEMA.json`** — the same schema the mock is checked
  against. A freeze that is not enforced against the implementation is a document,
  not a contract. Agent-C owns it (in-zone, additive to T17); the check may be a
  test that drives the façade through the loopback server and validates the JSON.
  Not a v1 field change.
- **Status:** OPEN — assigned to Agent-C under T17.

---

### [2026-10-08 00:12 UTC] E07 — `score_is_probability` semantics (T19 F19-2) — Lead ruling
- **Question:** Should `meta.score_is_probability` mean "a calibration exists / the
  surfaced number is a probability" (mock) or "this field is literally a calibrated
  probability" (real API, always `false`)?
- **Lead ruling:** **Keep the name; it is always `false` in v1.** In v1 the
  surfaced value is a raw score, never a calibrated probability, so the honest
  answer to "is this field a probability?" is `false` — exactly what the real API
  emits. The mock is **wrong** to set it `true` in `--calibrated` (that trains the
  frontend to mislabel a score). Agent-C: fix the mock to always emit `false`;
  `signal.probability_calibrated` is the single source of truth for
  show-a-probability-vs-show-a-score. When real data lands (E05) and a calibrated
  probability is published, `probability_calibrated` becomes `true` while
  `score_is_probability` stays `false` unless the surfaced number itself is the
  calibrated probability. **No rename** (a rename would be a breaking change).
- **Status:** OPEN — assigned to Agent-C under T19.

---

### [2026-10-08 06:11 UTC] E08 — Agent-C liveness (OFFLINE >6h on the critical path)
- **What:** Agent-C (owner of T17/T19 — the critical path) has had **no heartbeat
  and no commit since 2026-10-07 23:10 UTC**; at resume (2026-10-08 06:11 UTC) that
  is **~7h** — far past the 30-min OFFLINE / 60-min DEAD thresholds.
- **Context:** Agent-C's last heartbeat read "standing by for T13 hardening", which
  *predates* the T17/T19 fix assignment and the E06/E07 rulings. It never acked
  cycle 24/25. Agent-A flagged the Lead-side liveness gap at 02:35 UTC, confirming
  the whole repo went quiet after ~00:45 UTC.
- **Impact:** T17 (freeze: F17-0/F17-1/F17-2), T19 (mock: F19-1/F19-2), E06, E07,
  F22-4b-v (validator finite guard), and D-1 (durable-gate substring) are all
  stalled. T13 is gated behind them. T22/T18 are unaffected.
- **Lead action:** Agent-C declared **OFFLINE** (verge of DEAD) in `state.md`;
  leases released. We **cannot** work in Agent-C's zone from another container.
- **Mitigation (queued, waiting on liveness):** **T23** (Agent-B) — implement-
  agnostic frozen-contract + invariant checker so Agent-C's eventual F17-1 fix is
  mechanical; **T24** (Agent-A) — T13 integration harness written against the frozen
  contract.
- **Blocks:** T13 (and the completion gate).
- **Status:** OPEN — **escalated to human: is the Agent-C container alive?**
  Restart it, or authorize reassigning T17/T19 (I recommend reassignment if it
  stays dark past hour ~8).

---

### [2026-10-08 06:11 UTC] E05 — RE-ESCALATION: real XAUUSD data (STILL BLOCKED)
- **STILL BLOCKED since 2026-10-07 22:36 UTC** (~7h35m) — the mission's only hard
  publication blocker is unchanged; no human decision has arrived.
- **Required (confirm with Agent-B/Agent-D, requested this cycle):** real XAUUSD
  **M1 OHLCV, inclusive 2021-01-01 through 2025-12-31**, one consistent source.
  MISSION fixes the analysis window at **3 months**; the longer history is requested
  so we can choose the 3-month window *and* hold out genuinely out-of-sample data
  (a 3-month-only export cannot give an honest OOS split). Higher timeframes are
  derivable from M1; costs under RULE B tiers (spread 0.30 + commission + slippage).
  If a different range/source is intended, say so and the task briefs update.
- **Options:** A) human MT5 broker export; B) Dukascopy public data (matches the
  prior research, reproducible); C) keep synthetic and label all results
  **non-evidential**.
- **Recommendation:** **B** — reproducible and matches prior research.
- **Impact if delayed:** T13 cannot be finalized and no probability may be
  published; backend build/freeze proceeds regardless.
- **Status:** OPEN — re-escalated (STILL BLOCKED).
