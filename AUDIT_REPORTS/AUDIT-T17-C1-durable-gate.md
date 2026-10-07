# Audit Report — T17 slice: C-1 durable calibration gate

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 23:58 UTC
- **Repo HEAD at audit:** b69aee3 (slice commit 8828f9f, 22:45Z)
- **Verdict:** **NEEDS WORK.** The correct behaviour on the *real* T11 report is
  right (gate stays closed; both surfaces read one gate), the parser is
  fail-closed on missing files, and ctest is 18/18. But the verdict parser uses a
  substring test that **opens the RULE C probability gate on a non-PASS verdict
  containing the substring "pass"** — F17-0, materially the inverse of the
  failure this slice exists to prevent.

---

## Claim

Agent-C, C-1 (commit 8828f9f): the RULE C gate is now bound to the durable T11
audit artifact instead of an in-process toggle; the gate opens **only** when the
report shows a PASS **and** does not withhold publication authorisation; on the
real synthetic-data report it stays closed.

Acceptance: (a) real T11 report ⇒ gate closed; (b) a PASS that withholds
publication ⇒ closed; (c) an authorised PASS ⇒ open; (d) missing artifact ⇒
closed; (e) one gate shared by `/probability` and `/analysis`.

## Evidence inspected

- `src/api/ProbabilityApi.{h,cpp}` — `applyCalibrationAudit`, `reportValue`,
  `reportLine`, `withholdsPublication`, `evaluate`/`view`.
- `src/platform/windows/AuraBackendHost.cpp` — default artifact path, `--calibration-audit`.
- `tests/ProbabilityApiTests.cpp` — 4 new cases.
- `AUDIT_REPORTS/AUDIT-T11-calibration.md` (the real artifact).
- Agent-D probe `/tmp/t11_gate_probe.cpp` (uncommitted): calls
  `applyCalibrationAudit()` directly on the real report and on crafted reports.

## Verification steps

```
$ cmake --build build && (cd build && ctest)     -> 18/18 pass
$ /tmp/t11_gate_probe   (parsed verdict -> gate):
  REAL T11 report            present=1 passed=1 pubAuthorised=0 gateOpen=0  "(PASS but publication not authorised)"
  missing path               present=0 passed=0 pubAuthorised=0 gateOpen=0  "artifact not found"
  PASS, no caveat            present=1 passed=1 pubAuthorised=1 gateOpen=1
  PASS + NOT authorised      present=1 passed=1 pubAuthorised=0 gateOpen=0
  FAIL                       present=1 passed=0 pubAuthorised=0 gateOpen=0
  REJECT                     present=1 passed=0 pubAuthorised=0 gateOpen=0
```

(a) PASS on real report. (b) withholds. (c) authorised PASS. (d) missing ⇒ closed.
(e) shared gate: `AnalysisApi` uses the same `ProbabilityApi::evaluate`/`Gate`
(inherited from T16 — verified there). All correct.

## Findings

### F17-0 — BLOCKING: a NOT-PASS verdict that contains "pass" opens the gate

`applyCalibrationAudit` sets `audit.passed` with a substring test:
```cpp
audit.passed = verdictLower.find("pass") != std::string::npos;   // verdictLower = lower(Verdict: value)
```
So any verdict text containing the letters `pass` counts as a PASS — including
**"NOT PASS"** and **"FAIL (did not pass)"**:

```
  verdict NOT PASS              passed=1 pubAuthorised=1 gateOpen=1
  verdict FAIL (did not pass)   passed=1 pubAuthorised=1 gateOpen=1
```

Because the withheld-check is a *separate* token scan (`not authoris`/`withheld`/
…), a non-PASS verdict with no such token opens the gate. Example payload:
`"- **Verdict:** NOT PASS"` ⇒ `audit.passed=true`, `publicationAuthorised=true`,
`calibrationAudited()=true` — a **rejected** calibration would render as a
calibrated probability. This is the mirror image of the F19-1/F17 class the slice
exists to close: it opens the gate the mission says must stay shut. Fail-open, not
fail-closed.

It is material and reachable: an auditor could reasonably write "Verdict: NOT PASS"
or "FAIL (did not pass)"; the committed T11 report happens to be phrased
"PASS (methodology)", so the real artifact is safe — but the parser must be
correct independent of one document's phrasing. Agent-C's own suite has no
NOT-PASS case (it tests FAIL, withheld-PASS, authorised-PASS, missing), so the hole
is untested.

**Fix (in-zone):** test the **leading verdict token**, not a substring — e.g.
`passed = verdictLower.rfind("pass", 0) == 0` (verdict begins with "pass"), or
parse the first alphanumeric word and compare `== "pass"`. Add cases for
"NOT PASS", "FAIL (did not pass)", "PASSING"/"bypassed". Keep the fail-closed
default.

### F17-0b — non-blocking: whole-content withheld scan is over-broad (fails safe)

`withheld` includes `withholdsPublication(lowerCopy(content))`, so the token
"withhold" **anywhere** in the report (e.g. a sentence explaining that the gate
withholds when unauthorised) closes the gate. That direction fails *closed*, which
is safe, but it is fragile and can silently suppress a legitimate authorised PASS.
Recommend scanning the Verdict line and an explicit `Publication:` line only.

## Result

**NEEDS WORK.** Behaviour on the real report and the missing-file path is correct
and the shared gate is good, but F17-0 lets a NOT-PASS verdict open the RULE C
gate via substring matching. Fix the verdict test (leading token) and add the
NOT-PASS regression cases; F17-0b optional hardening.

## Notes

- **Independence.** Agent-D authored none of the audited code; probe in `/tmp`,
  uncommitted; reproduces from `8828f9f`.
- **Provenance.** The gate correctly derives provenance from the artifact
  (`Audit{auditor,date,source,reason}`) and stays closed on absence — that part is
  a genuine improvement over the toggle and is sound.
- **Scope.** This audits the C-1 slice only. The T17/T19 mock-fidelity and tag
  fixes (F19-1/F19-2/F17-1/F17-2) had not landed at this HEAD.
