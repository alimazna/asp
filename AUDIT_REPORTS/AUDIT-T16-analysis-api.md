# Audit Report — T16 (Analysis API endpoints)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 23:06 UTC
- **Repo HEAD at audit:** 8265401 (T16 commit 77aee94)
- **Verdict:** **PASS.** The analysis surface is honest: it shares ONE RULE C gate
  with `/probability`, emits `null`/`UNKNOWN` for everything it cannot source,
  returns 503 when a dependency is absent, and its real payloads validate against
  the frozen schema. Non-blocking observations below.

---

## Claim

Agent-C, `coordination/agent-c/comm.md` (T16 submission):

> GET /api/v1/analysis/latest, /analysis/history?limit=N, /context/latest,
> /health/v1. RULE C: both /analysis and /probability read ONE gate
> (ProbabilityApi::evaluate/view), so they cannot diverge. When uncalibrated,
> signal.probability=null, probability_calibrated=false, score_is_probability=false.
> Levels come from the live risk proposal. Fields the backend cannot source are
> null/UNKNOWN. Query: limit clamped 0..500, default 50.

Acceptance: RULE C enforced through a single gate; no fabricated values; loopback
JSON; additive within v1.

---

## Evidence inspected

- **Files:** `src/api/AnalysisApi.{h,cpp}`, `src/api/BackendFacade.{h,cpp}`,
  `tests/AnalysisApiTests.cpp`.
- Agent-D built a probe that constructs the real `BackendFacade` and dumps
  payloads in three modes (no record / uncalibrated-with-record / calibrated),
  then validated every payload against `API_V1_SCHEMA.json` with the repo's own
  validator.

```
$ ctest -R "AnalysisApi|ProbabilityApi"   -> 2/2 passed
$ /tmp/t17_probe  (real facade dumps)
$ validate 12 payloads vs frozen schema   -> 0 failures
```

---

## Verification steps

1. **Single RULE C gate (owner's core claim).** `AnalysisApi::latest` takes
   `const ProbabilityApi*` and reads `ProbabilityApi::Gate`; it does not compute a
   probability itself. Verified in source and by behaviour: with one ledger record
   that is `probabilityCalibrated=true` but the gate **un-audited**, the analysis
   `signal.probability` is `null`, `probability_calibrated=false`. With the gate
   audited, `probability=0.72`, `probability_calibrated=true`. Same gate as
   `/probability`. PASS.
2. **No fabrication.** In uncalibrated mode: `signal.horizon`, `confidence_lo/hi`,
   `model_version` are `null`; `features_contributing` is `[]`; `levels.*`
   including `sl_method`/`tp_method` are `null`; `meta.data_freshness_sec` is
   `null`; `context.mtf_agreement` is `null`. Agent-C explicitly did NOT invent
   `horizon="next_4xM15"` or `sl_method="atr_1.5x"` from the DRAFT T15 design.
   PASS.
3. **Envelope + schema.** Every real payload is `{api:"v1", schema:"1.0",
   data:...}` and validates against the frozen schema (12/12). PASS.
4. **No-record path.** With an empty ledger, `/analysis/latest` returns a valid
   envelope with `available`/null fields; `/analysis/history` returns `[]`. No
   crash, no invented data. PASS.
5. **Levels from the risk proposal, not recomputed.** `levelsJson` reads
   `context.risk.*`; it does not call the T15 `suggest_levels`. Consistent with
   the freeze decision (levels stay null until T15 wiring, a v1.x change). PASS.
6. **Query handling.** `/analysis/history?limit=N`: LoopbackApiServer splits
   `path?query`; limit clamped to 0..500, default 50 (matches schema `minimum 0`
   / `maximum 500` / `default 50`). PASS.
7. **Error path.** A missing dependency yields 503 `dependency_unavailable` (flat
   error object), which the mock and schema both model. PASS.
8. **Additive only.** The change adds an `AnalysisApi*` dependency to
   `FacadeDependencies` and routes; existing routes/tests unaffected; ctest green.
   PASS.

---

## Observations (non-blocking)

- **O-T16-1 — RULE C gate is an in-process bool (C-1 lineage).** Same caveat as the
  T09 audit: `meta.score_is_probability` / `probability_calibrated` reflect
  `setCalibrationAudited`. Agent-C (commit `8828f9f`, C-1) bound this to the
  durable T11 audit artifact — that work is **not yet in this head** and should be
  re-verified when it lands.
- **O-T16-2 — header comment route list.** `AnalysisApi.h` lists `/health` in the
  surface; the frozen implementation exposes `/health/v1` (and the pre-existing
  `/health` keeps its monitor shape). Documentation nit only; T17 doc is correct.
- **O-T16-3 — context fields are UNKNOWN.** The live `context.regime/h4_bias/
  m15_trigger/volatility_state` come from the decision context, which is empty in
  headless mode, so they are `UNKNOWN`. Honest; a real bridge fills them.

---

## Result

**PASS.** The analysis surface honours RULE C through the shared gate, fabricates
nothing, degrades explicitly, and conforms to the frozen schema. Ready for DONE
(contingent on the T17 freeze being accepted).

## Notes

- **Independence.** Agent-D authored none of the audited code; probe in `/tmp`,
  uncommitted; reproduces from `77aee94`.
- **Synthetic/headless.** Payloads verified with a constructed facade; no live
  bridge or real data (E05).
