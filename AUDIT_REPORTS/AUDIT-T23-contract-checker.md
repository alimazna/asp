# Audit Report — T23 (frozen-contract + invariant checker)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner:** Agent-B (Probability & Calibration)
- **Date:** 2026-10-08 06:35 UTC
- **Repo HEAD at audit:** ada0e9f (then 38cceec — same tree)
- **Verdict:** **PASS.** The E06 two-layer point is real and its teeth are genuine.
  One non-blocking input-hardening gap (F23-1: non-finite numbers accepted).

---

## Claim

Agent-B, T23: `src/models/contract_checker.py` is the single canonical enforcement
point for the frozen API v1 contract — layer 1 *structure* (reads
`API_V1_SCHEMA.json`, does not restate it), layer 2 *semantics* (the frozen-null
set the JSON Schema cannot express) — plus `tests/models/test_contract_checker.py`
(17 tests), consumed by Agent-C's F17-1.

## Evidence inspected

- `src/models/contract_checker.py` (structure validator, `frozen_violations`,
  `analysis_contract_violations`, `require_valid_analysis`).
- `tests/models/test_contract_checker.py`.
- `scripts/mock_api.py` (parity partner).
- `tests/fixtures/api_v1/{valid,invalid,semantic}`.
- Agent-D independent probes (inline; not committed).

## Verification steps

```
$ PYTHONPATH=. python3 -m unittest discover -s tests/models -t .   -> 246 OK
$ python3 tests/integration/test_api_fixtures.py                   -> 51/51 PASS
$ python3 scripts/mock_api.py --check                              -> 0 failures
$ parity: contract_checker.validate_envelope vs mock_api.validate_envelope
           on all 23 fixtures                                       -> agree
```

Independent semantic probe (mutating the default fixture):
```
  calibrated + horizon populated      -> invariant: signal.horizon non-null   OK
  calibrated + levels populated       -> invariant: levels.sl_method non-null OK
  prob=0.0 with calibrated:false      -> invariant: probability non-null ...  OK
  score_is_probability true           -> invariant: not false (E07)           OK
  missing levels.tp_method            -> structure: missing required          OK
  dynamic fields changed freely       -> no violation (F22-3)                 OK
```

## Assessment of Agent-B's three claims

1. **Two layers enforce E06/E07 — TRUE.** `analysis_contract_violations` runs
   structure then semantics; the semantic set is complete for `/analysis/latest`
   (all four frozen-null containers + `score_is_probability` + the
   probability⇄calibrated conditional). Independently reproduced.
2. **Parity test has teeth — TRUE.** It executes both validators on every
   `valid`+`invalid` fixture and asserts agreement; I reproduced agreement and
   confirmed both share the same dialect (so a dialect drift on either side WOULD
   show as disagreement).
3. **No frozen field passes by omission — TRUE for the analysis surface.**
   Absent frozen keys are reported as violations (present-and-null required),
   teeth-tested.

## Findings

### F23-1 — non-blocking: the checker accepts non-finite numbers

`contract_checker._validate_properties` mirrors the schema dialect's range check
(`value < minimum` / `value > maximum`), so `NaN` passes (both comparisons are
false) and `inf` passes where no bound is set:
```
  calibrated + NaN probability   -> []      (should reject)
  NaN score                      -> []      (should reject)
  inf score (no min/max)         -> []      (should reject)
```
Agent-C added exactly this `math.isfinite` guard to `scripts/mock_api.py` under
F22-4b-v, so the two readers of the one schema are now **inconsistent**: the mock
validator rejects non-finite numbers, the "canonical" checker does not. Because
F17-1 now consumes `contract_checker`, a live response carrying a non-finite
number would pass the freeze check. No current payload emits one (scores/levels
are bounded; probability is in [0,1]; Python's `json.loads` would accept bare
`NaN`/`Infinity`), so this is an input-hardening gap, not a live defect.
**Fix (in-zone `src/models/`):** add the `math.isfinite` guard to
`_validate_properties` and a regression case, for parity with F22-4b-v.

## Result

**PASS.** The two-layer enforcement point is correctly built, reads the schema as
the single source of truth, and has genuine anti-vacuity teeth; F22-4b-v parity
holds. F23-1 is a narrow, non-blocking follow-up (finite guard). Recommend
Agent-B close it before T23 → DONE; no other residual.

## Notes

- **Independence.** Agent-D authored none of the audited module; probes inline,
  uncommitted; reproduces from `ada0e9f`.
- **Scope.** T23 vs Agent-A's `invariant_violations` is legitimate division of
  labour (module-side checker vs fixture-side self-check), unified by the parity
  test; no clip.
