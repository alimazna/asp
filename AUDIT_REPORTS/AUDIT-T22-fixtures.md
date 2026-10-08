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

---

# ADDENDUM A — re-audit after F22 fixes

- **Date:** 2026-10-07 23:39 UTC
- **Repo HEAD at re-audit:** 621d032
- **Verdict:** **NEEDS WORK (one residual).** F22-1 fixed in the **default**
  fixture; F22-2 done (invariant set expanded to the full E06 set); checker now
  41/41. But the **calibrated** fixture still pins frozen-null fields (F22-1b).

| Item | Status | Evidence |
|---|---|---|
| F22-1 default fixture (`model_version`/`features_contributing`) | **FIXED** | `valid/analysis_latest.json`: `model_version:null`, `features_contributing:[]`; two new default-shape assertions. |
| F22-1b calibrated fixture frozen-nulls | **OPEN** | `valid/analysis_latest_calibrated.json` still sets `horizon:"H4"`, `confidence_lo:0.54`, `confidence_hi:0.68`, `context.mtf_agreement:0.67` — all on the freeze's null list this release. |
| F22-2 invariant set | **FIXED** | `invariant_violations` now covers horizon/confidence_lo/hi/model_version, all `levels.*`, `meta.data_freshness_sec`, `context.mtf_agreement` (when uncalibrated), and non-null-probability ⇒ calibrated-true. |
| F22-3 live fields | **ACK** | direction ruling noted; not compared for equality. |

### F22-1b — residual: the calibrated fixture pins unsanctioned values

`BACKEND_FRONTEND_API_V1.md` lists `signal.horizon`, `signal.confidence_lo/hi`,
and `context.mtf_agreement` as **null this release** — the freeze does not pin
values for them, and the real backend emits them null **even with the calibration
gate open** (verified: my calibrated-mode probe returns `horizon:null`,
`confidence_lo/hi:null`, `mtf_agreement:null`, `model_version:null`). The
calibrated fixture instead invents `"H4"`, `0.54`, `0.68`, `0.67`. Under the
Lead's sharpened ruling ("a fixture may never introduce a value the freeze does
not sanction"), those four must be `null` until T15/T11 freeze them.

The checker does not catch it because `invariant_violations` only inspects the
**uncalibrated** branch (`if probability is None`), and the frozen-null assertions
only run against the default fixture. The calibrated fixture should satisfy the
same frozen-null set (only `probability`, `probability_calibrated`, and
`coverage_tier` legitimately differ on the calibrated branch — per
`ProbabilityApi::latest`, which additionally reports `confidence_interval:null`
and `model_version:null`).

**Fix (in-zone):** in `valid/analysis_latest_calibrated.json` set
`signal.horizon=null`, `confidence_lo=null`, `confidence_hi=null`,
`context.mtf_agreement=null`; add calibrated-branch frozen-null assertions to the
checker. Re-audit is trivial: 41+ checks green with those asserted null.

Everything else in T22 is sound and the two-fix response was fast and correct.

---

# ADDENDUM B — re-audit after F22-1b fix

- **Date:** 2026-10-07 23:45 UTC
- **Repo HEAD at re-audit:** 0fc7083
- **Verdict:** **PASS.** F22-1b fixed; the invariant model was corrected to the
  right abstraction (frozen nulls are unconditional), and a branch-diff guard was
  added. 50/50 checks pass. I confirmed the guard has teeth.

| Item | Status |
|---|---|
| F22-1 (default `model_version`/`features`) | FIXED |
| F22-1b (calibrated frozen-nulls) | **FIXED** — `horizon`, `confidence_lo/hi`, `mtf_agreement` now `null` |
| F22-2 (E06 invariant set) | FIXED, and corrected: frozen nulls are asserted **unconditionally** (both branches), not only when uncalibrated |
| F22-3 (live fields) | Handled — branch-diff allow-list pins only frozen/calibration fields |

Re-verification.
```
$ python3 tests/integration/test_api_fixtures.py   -> 50 checks, 0 failed
$ teeth test (injected drift):
    levels.stop_loss non-null  -> FAIL   (caught)
    signal.horizon="H4"        -> FAIL   (caught)
    context.regime changed     -> PASS   (documented live field, correctly allowed)
```

The new `branch diff is only calibration + documented live fields` guard is the
strongest part: it reduces the two analysis fixtures to only the fields legitimately
allowed to differ (`probability`, `probability_calibrated`, `coverage_tier`, plus
documented live snapshot state), so drift in either file fails CI. **T22 → PASS.**

## Notes

- **Independence.** Agent-D authored none of the audited fixtures; drift test
  performed on a scratch copy and reverted; reproduces from `0fc7083`.
- **Consumability.** `invariant_violations` is now exactly the reusable semantic
  check the Lead ruled Agent-C's F17-1 must consume.

---

# ADDENDUM C — validator soundness probe (the checker's own foundation)

- **Date:** 2026-10-08 00:40 UTC
- **Verdict:** sound; two benign gaps, neither affecting the fixture set.

Since every fixture check and the mock self-check route through
`mock_api.validate_envelope`/`validate_properties`, I adversarially probed the
validator itself (mutating the default fixture):

```
  bool True as score          -> rejected (type)
  probability "0.5" (string)  -> rejected (type)
  symbol as integer 123       -> rejected (type)
  levels as string            -> rejected (type)
  coverage_tier null          -> rejected (enum)
  data null                   -> rejected (object)
  probability inf             -> rejected (maximum)
  extra property injected     -> VALIDATED     (F22-4a)
  probability NaN             -> VALIDATED     (F22-4b)
```

Types, enums, ranges and required-keys are genuinely enforced — the 50/50 is not
vacuous. Two gaps, both **non-blocking**:
- **F22-4a** additional properties are accepted (no `additionalProperties:false`).
  This is consistent with the additive-v1.x policy and forward-compatibility, so
  treat as by-design; note only that a typo'd field name would not be caught.
- **F22-4b** `NaN` passes range checks (`NaN < min`/`> max` are both false). JSON
  has no NaN literal, and no fixture or mock emits one, so no current payload is
  affected; worth a finite guard for consistency with the T20/F20-1 fix if the
  validator is ever pointed at untrusted input.

---

# ADDENDUM D — F22-4b response re-audit

- **Date:** 2026-10-08 00:45 UTC
- **Repo HEAD at re-audit:** 14ed481
- **Verdict:** **PASS (T22 unchanged); F22-4b addressed fixture-side.**

Agent-A added a recursive `non_finite` scan over every `valid/invalid/semantic/
errors` fixture, asserted as `no fixture carries NaN/inf` (51/51). Teeth-tested:
injecting `NaN` into a fixture score → **FAIL**; restored → PASS. This is the
correct in-zone response — the fixtures are guarded, and the *validator* finite
guard (untrusted-input hardening in `scripts/mock_api.py`, Agent-C's zone) is
correctly left to Agent-C and flagged for them (F22-4b-v). No residual on T22.
