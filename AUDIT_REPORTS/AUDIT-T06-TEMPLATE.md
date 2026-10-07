# Audit Report — T06 (MT5 bridge) — BRIDGE

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** <fill on audit>
- **Repo HEAD at audit:** <fill>
- **Verdict:** PASS | FAIL | NEEDS WORK

---

## Claim

<What Agent-C claimed. Quote their "ready for audit" comm.md entry verbatim,
with timestamp and commit hash. State acceptance criteria: MT5 bridge works,
loopback-only, closed-bar semantics, no fabrication, production protected,
live trading disabled.>

---

## Evidence inspected

- **Commit(s):** <hash(es) claimed>
- **Files:** `bridge/mt5_python/*`, `src/mt5/*`, any tests
- **Test output:** <exact command(s) run and raw output — Agent-D reruns them>
- **Raw commands + output:** <paste below>

```
<paste exact commands and output>
```

---

## Verification steps

1. **Loopback only.** Confirm the bridge binds 127.0.0.1 only and rejects any
   other host. Attempt a non-loopback bind and record the refusal.
2. **Handshake / versioning.** Confirm protocol/schema version handshake and
   that a mismatched client version is rejected with a structured error.
3. **Closed-bar causality.** Confirm `closed_only=true` semantics: the forming
   bar is never returned as decision-grade.
4. **No fabrication.** With MT5 unavailable, confirm a structured error
   (`MT5_TERMINAL_UNAVAILABLE`) and *no* invented candles.
5. **Nine timeframes.** Confirm all nine can be requested and are canonical-ordered.
6. **Error model.** Confirm structured code/state/message/recovery on failure.
7. **Production protected.** Confirm no changes to frozen contracts
   (`src/foundation/`) or forbidden zones; diff against the protected baseline.
8. **Live trading disabled.** Confirm no live-execution path exists and the
   Guardian structurally denies it (`allowLiveExecution == false`).
9. **Reproduce independently.** Boot the bridge from a clean checkout and probe
   the endpoints with raw curl; do not trust the claim.

---

## Result

PASS | FAIL | NEEDS WORK

<One-line justification. If FAIL/NEEDS WORK, list exact failures with the
command and observed output.>

---

## Notes

<Whether real MT5/broker data was exercised or only the unavailable-path; port
numbers observed; whether the bridge was a separate supervised child process.>
