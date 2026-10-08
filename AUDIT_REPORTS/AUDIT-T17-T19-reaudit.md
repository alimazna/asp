# Audit Report — T17/T19 re-audit (freeze, mock, gate) + C-1 re-audit

- **Auditor:** Agent-D (Verification & Audit)
- **Owner:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-08 06:35 UTC
- **Repo HEAD at audit:** ada0e9f (`tag api-v1.0`)
- **Verdict:** **PASS.** Every previously-open finding is fixed and independently
  reproduced. T17/T19 are ready for DONE.

---

## Previously-open findings — status

| Finding | What | Status |
|---|---|---|
| **F17-0** | durable-gate verdict substring let "NOT PASS" open the RULE C gate | **FIXED** |
| **F17-1** | freeze not checked against the implementation | **FIXED** |
| **F17-2** | `api-v1.0` tag claimed but absent | **FIXED** |
| **F19-1** | mock default not frozen-null | **FIXED** |
| **F19-2 / E07** | `score_is_probability` semantics | **FIXED** |
| **F22-4b-v** | validator finite guard | **FIXED** |
| **C-1 D-1** | snapshot UNOBSERVED branch emitted string `quality` | **FIXED** |
| F22-1 (T22 fixtures) | 4 mock-derived fixtures stale after schema correction | **FIXED** (Lead-ruled refresh) |

## Verification steps (all independently reproduced at ada0e9f)

```
$ cmake --build build && ctest                         -> 18/18
$ ./ProbabilityApiTests                                -> 16/16 (was 14)
$ python3 tests/integration/test_api_fixtures.py       -> 51/51 (was 4 FAIL)
$ PYTHONPATH=. unittest discover -s tests/models       -> 246 OK (was 243, 1 err)
$ python3 scripts/mock_api.py --check                  -> 0 failures
$ python3 tests/integration/test_mock_api_t19.py       -> 39/39
$ python3 tests/integration/test_contract_t16.py       -> 36/36 (0 known defects)
```

### F17-0 — FIXED (re-probe)
The verdict's **leading token** now decides (`verdictToken == "pass" || "passed"`).
Re-ran my probe through `applyCalibrationAudit`:
```
  PASS / PASS (methodology) / PASSED   -> gate OPEN (correct)
  NOT PASS / FAIL (did not pass)       -> passed=0 gateOpen=0   (was OPEN)
  PASSING / NOT PASSING                -> passed=0 gateOpen=0   (was OPEN)
  FAIL text containing "bypass"        -> passed=0 gateOpen=0   (was OPEN)
  PASS + "publication NOT authorised"  -> passed=1 gateOpen=0   (correct)
  real AUDIT-T11-calibration.md        -> passed=1 gateOpen=0   (correct)
```
Regression cases for NOT PASS / FAIL (did not pass) / PASSING / NOT PASSING were
added. The false-open hole is closed.

### F17-1 — FIXED (wired to the canonical checker)
`tests/integration/test_contract_t16.py` now validates the **live binary's**
responses against the schema and the **semantic** layer via
`contract_checker.analysis_contract_violations` — the Lead's ruling to consume
Agent-B's helper rather than re-implement the frozen set is honoured. The
`KNOWN_DEFECTS` allow-list is **empty** and no `[KNOWN]` line is emitted; D-1 is
genuinely fixed, not suppressed. `BackendFacade.cpp` line 166 emits
`qualityJson(...)` (object) in the unobserved branch — matching the rest of the
route and the schema (`type: object`). 36/36.

### F17-2 — FIXED
`git ls-remote --tags` shows `refs/tags/api-v1.0` → `ada0e9f`
("T17 completion — D-1 fix, fixture refresh, F17-1 full"). The annotated tag
peels to that commit. The claim is now true.

### F19-1 / F19-2 / E07 — FIXED
Default mock (`analysis_obj(False)`): `probability:null`, `probability_calibrated:false`,
`horizon/confidence_lo/hi/model_version` null, all `levels.*` null,
`data_freshness_sec` null. Calibrated mock: `probability` set,
`probability_calibrated:true`, **`score_is_probability` stays `false`** (E07),
frozen-null fields remain null. `--check` prints "E07 score_is_probability false
in both branches" and exits 0 failures. T19 self-test 39/39.

### F22-4b-v — FIXED
`scripts/mock_api.py:97` adds `if not math.isfinite(value)` (rejects NaN/inf).
Independently probed: a non-finite field is rejected.

### C-1 D-1 — FIXED
The snapshot UNOBSERVED branch object shape is corrected; the contract test drops
the allow-list and passes.

### F22-1 fixture refresh — FIXED
4 mock-derived fixtures (`timeframes`, `timeframe_snapshot`, `shadow_positions`,
`shadow_outcomes`) now match the corrected schema; `test_api_fixtures` is
**51/51** green. No analysis fixtures were touched (no T22 reopen).

## Findings

### F17-D1 — non-blocking: `git tag -l` is empty on a fresh clone
The tag exists on the remote and is fetched when present, but `git clone`
without `--tags` on an already-existing checkout did not surface it until I ran
`git fetch --tags`. Not a defect in the work; noting so no auditor mistakes
"local `git tag -l` empty" for "tag absent". The tag is real and correct.

### F17-D2 — note: the canonical checker lags the mock on non-finite numbers
Same root as **F23-1** (reported under T23). `scripts/mock_api.py` now rejects
non-finite numbers (F22-4b-v) but `src/models/contract_checker.py` — which F17-1
*now consumes* — does not. No current payload emits a non-finite number, so T17
is not blocked; closing F23-1 restores the two readers to parity.

## Result

**PASS.** F17-0/F17-1/F17-2/F19-1/F19-2/E07/F22-4b-v and C-1 D-1 are all fixed and
verified against the live build and the frozen artifacts; the tag is real; the
tree is fully green (18 ctest / 16 gate tests / 51 fixtures / 246 models / 39 mock
/ 36 contract). Recommend T17 → DONE and T19 → DONE. Only open item is the
non-blocking F23-1/F17-D2 parity guard in Agent-B's zone.

## Notes

- **Independence.** Probes rebuilt from `ada0e9f`; no source edited by Agent-D. The
  `cmake`/`ctest` binaries were installed from PyPI (`cmake`) to run the suite —
  tooling only, nothing committed.
- **C-1 durable gate** re-audited here as part of F17-0; report supersedes
  `AUDIT-T17-C1-durable-gate.md`'s NEEDS WORK verdict.
