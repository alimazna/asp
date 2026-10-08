# REPORT — T23: Frozen-contract + invariant checker

**Agent:** Agent-B (Probability & Calibration)
**Task:** T23 — frozen-contract + invariant checker (E06/E07 enforcement point)
**Zone:** `src/models/` (+ `tests/models/`) — in-zone, no writes outside
**Status:** REVIEW (submitted to Agent-D for audit)
**Date:** 2026-10-08

## Deliverable

- `src/models/contract_checker.py` — the single canonical enforcement point for
  the frozen API v1 contract.
- `tests/models/test_contract_checker.py` — 17 deterministic tests.
- `src/models/README.md` — module table + T23 status section.

## What it does (two layers, per the Lead's E06 ruling)

1. **Structure** — `validate_envelope(endpoint, spec, payload)` validates any
   enveloped response body against `docs/architecture/API_V1_SCHEMA.json`. The
   module **reads the frozen schema file**; it does not restate the contract, so
   there is one source of truth.
2. **Semantics** — `frozen_violations(data)` reports the frozen-null invariants
   the JSON Schema cannot express:
   - while `signal.probability` is null, `signal.horizon`, `confidence_lo`,
     `confidence_hi`, `model_version`, every `levels.*`,
     `meta.data_freshness_sec`, and `context.mtf_agreement` must be null
     (present-and-null; an absent field is also a violation);
   - `meta.score_is_probability` is always `false` (E07);
   - a null probability requires `probability_calibrated:false`; a non-null
     probability requires `probability_calibrated:true`.

`analysis_contract_violations(payload)` runs both and returns a list of
`structure:`/`invariant:` messages; `require_valid_analysis(payload)` raises
`ContractError` otherwise.

## Consumers (as directed by the Lead)

- **Agent-C F17-1** (T17): import `contract_checker.analysis_contract_violations`
  rather than re-implementing the frozen-null set. This is the "single shared
  frozen-set helper" the Lead asked for.
- **Agent-D audit**: same helper, so the audit and the implementation check the
  same invariants.

## Non-duplication / single source of truth

Agent-A's `tests/integration/test_api_fixtures.py:invariant_violations` remains
the **fixture-side** check. T23 does not replace it. Instead
`test_parity_with_mock_api_validator` runs this module's structural validator and
Agent-A's `scripts/mock_api.validate_envelope` over **every** fixture (valid +
invalid) and asserts they agree, so the two readers of the one schema cannot
silently drift. The frozen-null *set* is now stated once, in
`contract_checker.py`, and re-exported conceptually to both consumers.

## Evidence

| Check | Result |
|---|---|
| `python3 -m unittest tests.models.test_contract_checker` | 17/17 PASS |
| `python3 -m unittest discover -s tests/models -t .` | 243 PASS (226 + 17) |
| `python3 tests/integration/test_api_fixtures.py` | 51/51 PASS, RESULT: PASS |
| `python3 scripts/mock_api.py --check` | 0 failure(s) |
| Parity: this validator vs `mock_api.validate_envelope` on all 23 fixtures | agree |

## Teeth (anti-vacuity)

- Every frozen-null field is populated one at a time and must be caught.
- Live/dynamic fields (`symbol`, `timestamp`, `meta.degraded`,
  `signal.{direction,score}`, `context.{regime,h4_bias,m15_trigger,volatility_state}`,
  `meta.disclaimer`) may change freely without tripping an invariant (F22-3).
- An absent frozen field is a violation, not a silent pass.
- Inputs are not mutated.

## Scope notes

- RULE C unaffected: this module validates shapes; it publishes nothing.
- `src/models/api_contract.py` (the older DRAFT probability shape, T03 era) is
  untouched and remains superseded by the frozen v1 schema; not consumed here.
- No write outside `src/models/`, `tests/models/`, `coordination/agent-b/`.

## Request

@agent-d: please audit. Focus: the two layers actually enforce the E06/E07 set,
the parity test has teeth, and no frozen field can pass by omission.

## F23-1 fix (Agent-D audit, 06:35 UTC)

Agent-D audited T23 **PASS** with one non-blocking input-hardening gap: the
structural validator mirrored the schema dialect's range check, so `NaN`/`inf`
passed (`NaN < min` and `NaN > max` are both false). Since F17-1 now consumes
`contract_checker`, the gap was load-bearing and inconsistent with the
`math.isfinite` guard Agent-C added to `scripts/mock_api.py` (F22-4b-v).

Fix: added `math.isfinite` to `_validate_properties` (rejects non-finite for any
numeric node) plus regression tests `test_rejects_non_finite_numbers` and
`test_non_finite_parity_with_mock_validator`. Both readers of the schema now
reject non-finite numbers identically.

Re-verified at current head: 248 models tests OK, fixtures 51/51, mock `--check`
0 failures. Resubmitted T23 -> REVIEW for Agent-D re-audit.
