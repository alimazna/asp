# Audit Report — T09 (Probability API, RULE C gate)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 22:22 UTC
- **Repo HEAD at audit:** 69e9449 (T09 commit)
- **Verdict:** **PASS** on the RULE C gate. No path presents an uncalibrated,
  unaudited, non-directional, or out-of-range value as a probability. One honest
  caveat recorded (C-1): the audit gate is an in-process switch, not yet bound to
  a persisted T11 artifact. T09 may go to **DONE** at the Lead's confirmation.

---

## Claim

Agent-C, `coordination/agent-c/comm.md` 22:17 UTC, "submitted for independent
audit … does the gate really refuse every non-calibrated path, and is the tier
boundary honest vs `calibration.py`?":

> `probability` is emitted ONLY when ALL of: the audit gate is open
> (`setCalibrationAudited(true)`), the ledger record says
> `probabilityCalibrated=true`, the direction is LONG/SHORT (not NONE), and the
> value is in [0,1]. Every other path returns `calibrated=false` and
> `probability:null`; the uncalibrated `score` is always carried and labelled
> `score_is_probability:false`. An out-of-range calibrated value is rejected,
> never clamped. No interval/model_version is invented (reported null).

Commit under audit: `69e9449` "agent-c: T09 Probability API (RULE C gate)".

Acceptance: RULE C — an uncalibrated value must never be presented as a
probability; tier boundary must match the producer contract.

---

## Evidence inspected

- **Commit:** `69e9449`
- **Files:** `src/api/ProbabilityApi.{h,cpp}`, `src/api/BackendFacade.{h,cpp}`,
  `tests/ProbabilityApiTests.cpp`; cross-checked `src/models/calibration.py`.
- Agent-D reran the 10 API cases, then added an independent RULE C probe
  (NaN/inf, out-of-range, boundary values, tier totality).

```
$ ./build/ProbabilityApiTests     -> 10/10 PASS
$ g++ ... /tmp/t09_probe.cpp ... && /tmp/t09_probe
  t09_nan_and_inf_never_presented .................... PASS
  t09_out_of_range_rejected_not_clamped .............. PASS
  t09_boundary_values_are_presented_with_correct_tier  PASS
  t09_unaudited_calibrated_stays_uncalibrated ........ PASS
  t09_tier_sweep_is_total_on_unit_interval ........... PASS
$ ctest --test-dir build          -> 13/13 passed
```

---

## Verification steps

1. **Reran the 10 T09 cases.** All pass.
2. **Independent RULE C probe (Agent-D).** For a record that is
   `probabilityCalibrated=true` and gate open, fed:
   - **NaN / +inf / -inf** → `calibrated:false`, `probability:null` (NaN fails
     the `>= 0 && <= 1` range test). PASS.
   - **-0.001, 1.001, -5, 42** → rejected, not clamped. PASS.
   - **0.0, 0.5, 1.0** → presented with tiers `low`, `medium`, `high`. PASS.
   - **unaudited but calibrated** → stays `calibrated:false`. PASS.
   - `score_is_probability` is `false` on every path. PASS.
3. **Gate is the conjunction the claim states.** `presentable = audited_ &&
   probabilityCalibrated && directionValid && inRange`. Every false branch emits
   `calibrated:false` and `probability:null`; the raw score is carried separately
   and labelled. Direction `NONE` is never a probability (verified).
4. **Tier boundary honest vs producer.** `TIER_BOUNDS` in `calibration.py` is
   `low [0,1/3)`, `medium [1/3,2/3)`, `high [2/3,1]` (producer predicate
   `lower <= p < upper or (name=="high" and p >= upper)`). C++
   `probabilityTier` is `p<1/3 → low`, `p<2/3 → medium`, else `high`. On the
   validated domain [0,1] these are identical; a 1001-point sweep of [0,1] is
   total (no value without a tier). PASS.
5. **No fabrication.** `confidence_interval` and `model_version` are reported
   `null` (not sourced yet) rather than invented; `probabilityTier` returns
   `nullptr` for out-of-range, never guesses a tier.
6. **Additive only.** The change adds a route and an optional facade dependency;
   `BackendFacade` degrades to 503 when the surface is absent. No
   production/baseline path altered. `ctest` 13/13 (the new test is wired).
7. **Determinism.** Payload byte-identical for a fixed record (rerun).

---

## Result

**PASS.** The RULE C gate refuses every non-calibrated path I could construct,
including NaN/inf and out-of-range values; the tier boundary matches the producer
contract exactly. T09 may go to **DONE**.

## Caveat C-1 (honest, non-blocking)

The audit gate is an in-process boolean defaulting to **false**
(`setCalibrationAudited`). It is **not** bound to a persisted T11 audit artifact.
So today the route is *honestly uncalibrated by construction*, but the gate could
be opened by any caller that sets the flag. Agent-C disclosed this. When T11
produces a durable audit artifact, the gate should be sourced from it (e.g. a
signed/versioned audit record) so that "audited" is a verified fact, not a
runtime toggle. Recorded for T11/T13; not a defect in the current task.

## Notes

- **No fabrication.** Probe in `/tmp`, uncommitted; reproduces from `69e9449`.
- **Independence.** Agent-D authored none of the audited code.
- **RULE C at system level remains open:** the backend can *present* a calibrated
  probability only after T11; nothing here publishes one.
