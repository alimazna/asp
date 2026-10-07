# Audit Report — T22 (Analysis-API schema fixtures)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** 2026-10-07 23:29 UTC
- **Repo HEAD at audit:** dcee202 (T22 commit 203924b)
- **Verdict:** **NEEDS WORK.** The fixture set is well-built and the 39/39
  self-check is sound — the `valid/ | invalid/ | semantic/ | errors/` split is
  exactly the honesty the schema needs. But the **default** fixture
  (`valid/analysis_latest.json`) contradicts the freeze doc and the real backend
  on `model_version` and `features_contributing` (F22-1). Because the Lead ruled
  the fixtures DEFINE the expected implementation shape, F22-1 must be fixed
  before Agent-C's F17-1 check consumes them — otherwise the check fails, or
  someone "fixes" the backend to fabricate a model version (the F19-1 failure
  mode, inverted).

---

## Claim

Agent-A, `coordination/agent-a/comm.md` (T22 submission) + README:

> every fixture here is *derived from* `API_V1_SCHEMA.json`. A fixture that
> disagrees with the schema is a bug in the fixture. `tests/integration/test_api_fixtures.py`
> enforces that: each `valid/` payload must pass the schema validator and each
> `invalid/` payload must be rejected. `semantic/` = structurally schema-valid but
> violating a contract invariant the schema cannot express.

Acceptance: fixtures derived from the schema; default is the uncalibrated shape;
`invalid/` genuinely rejected; `semantic/` trip invariants; errors match
`error_schema`.

---

## Evidence inspected

- **Files:** `tests/fixtures/api_v1/{valid,invalid,semantic,errors}/*`,
  `tests/fixtures/api_v1/README.md`, `tests/integration/test_api_fixtures.py`.
- Agent-D ran the checker, inspected every invalid fixture's rejection reason, and
  field-diffed the default/calibrated fixtures against the real backend dump
  (`/tmp/t17_probe`).

```
$ python3 tests/integration/test_api_fixtures.py   -> 39 checks, 0 failed
$ invalid/ rejection reasons (all correct):
   missing_levels_field  -> missing required 'reward_risk'
   bad_coverage_tier_const -> 'HIGH' not in enum
   probability_above_one -> 1.4 above maximum 1
   probability_below_zero -> -0.1 below minimum 0
   not_enveloped -> envelope missing 'api'
   missing_envelope_schema_key -> envelope missing 'schema'
   wrong_envelope_api_const -> api != v1
```

---

## Verification steps

1. **Derivation from the schema.** Every `valid/` fixture validates against its
   schema endpoint; `VALID_ROUTES` covers all 15 frozen endpoints (analysis/latest
   has two files). PASS.
2. **`invalid/` genuinely rejected (owner's core claim).** All seven invalid
   fixtures are rejected, each for the intended reason (see list). I checked the
   *reason*, not just the rejection — each targets the named defect. PASS.
3. **`semantic/` split.** Both semantic fixtures pass structural validation and
   trip an invariant (`score_is_probability:true` while `probability:null`;
   `levels.stop_loss` non-null while uncalibrated). The schema cannot express
   these, so keeping them out of `invalid/` is correct. The README explains it
   precisely. PASS.
4. **`errors/`.** 503/404/405 bodies carry `error:"true"`, `code`, `message`.
   PASS.
5. **Default is uncalibrated.** `valid/analysis_latest.json` has
   `probability:null`, `probability_calibrated:false`, `score` present,
   `score_is_probability:false`, all `levels.*` null. PASS.
6. **Calibrated branch.** `probability` in [0,1], `probability_calibrated:true`,
   `score_is_probability:false` (E07). PASS.
7. **Fidelity to freeze + implementation — FAIL.** See F22-1.

---

## Findings

### F22-1 — BLOCKING: default fixture contradicts the freeze doc and the backend

`valid/analysis_latest.json` (the default shape) sets:
```
signal.model_version = "logistic-t03"
signal.features_contributing = ["structureTrend", "momentumNorm"]
```
but:
- `BACKEND_FRONTEND_API_V1.md` (frozen) lists **`signal.model_version` among the
  fields that are `null`** in this release ("`signal.horizon`,
  `signal.confidence_lo/hi`, **`signal.model_version`**, `levels.sl_method`,
  `levels.tp_method`, `meta.data_freshness_sec`, `context.mtf_agreement` are
  `null` until the decision model and calibration freeze them"), and
- the real backend emits `model_version:null` and `features_contributing:[]` in
  **both** branches (verified by probe).

So the fixture asserts availability neither the doc nor the backend grants. Per
the Lead's ruling ("the fixtures define the expected shape; the implementation
conforms — not the reverse"), Agent-C's F17-1 structure check will load this
fixture and compare it against real `BackendFacade` output, which will **differ**
on `model_version`. The failure could go two ways, both bad: (a) the check is
built to ignore the discrepancy, weakening F17-1; or (b) someone "aligns" the
backend to emit `model_version="logistic-t03"` to satisfy the fixture — fabricating
a model version the pipeline does not source, exactly the F19-1 failure mode one
layer over. `features_contributing` has the same defect but is not in the doc's
null list; it should also be `[]` to match the backend.

**Fix (in-zone):** set `signal.model_version = null` and
`signal.features_contributing = []` in **both** `valid/analysis_latest.json` and
`valid/analysis_latest_calibrated.json`, and add a fixture assertion that
`model_version` is null in the default shape (it is a frozen null this release).

### F22-2 — Non-blocking: invariant helper is narrower than the E06 ruling

`invariant_violations` covers two invariants (score_is_probability-while-null;
levels-while-uncalibrated). The Lead's E06 ruling lists more: when
`signal.probability` is null, also `horizon`, `sl_method`, `tp_method`,
`meta.data_freshness_sec`, `context.mtf_agreement` must be null; and a non-null
probability requires `probability_calibrated:true`. These fixtures are the natural
home for the full invariant set; Agent-C's F17-1 check should consume this helper
rather than re-implement it. Recommend expanding it (in-zone) and exposing it.

### F22-3 — Info: `degraded:true` in the default fixture

The fixture sets `meta.degraded:true`; the backend's headless default is `false`.
`degraded` is a live boolean, not a frozen null, so this is a plausible synthetic
state rather than a contradiction — but the F17-1 comparison must not treat
`degraded`/`symbol`/`timestamp` as fixed, only the frozen-null set and the
invariants. Record for the check design.

---

## Result

**NEEDS WORK.** The fixture architecture and its self-check are excellent and
39/39 pass, but the default fixture claims a `model_version` the freeze forbids and
the backend never emits (F22-1). Fix F22-1 in both valid analysis files; consider
F22-2 to make the invariant set complete. Re-audit is small: confirm
`model_version:null`/`features_contributing:[]` and re-run 39/39.

## Notes

- **Independence.** Agent-D authored none of the audited fixtures; probe in `/tmp`,
  uncommitted; reproduces from `203924b`.
- **Timing.** This should land before F17-1 is built on the fixtures, so the
  structure check is written against a shape the backend actually produces.
