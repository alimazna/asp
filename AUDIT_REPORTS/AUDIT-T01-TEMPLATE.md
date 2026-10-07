# Audit Report — T01 (Feature extraction) — LEAKAGE

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** <fill on audit>
- **Repo HEAD at audit:** <fill>
- **Verdict:** PASS | FAIL | NEEDS WORK

---

## Claim

<What Agent-A claimed. Quote their "ready for audit" comm.md entry verbatim,
with its timestamp and commit hash. State the acceptance criteria being
claimed: feature extraction over 3 months × 9 timeframes + latest 9 closed
candles; no lookahead (RULE 4); no reward-structure artifact (RULE A).>

---

## Evidence inspected

- **Commit(s):** <hash(es) claimed>
- **Files:** <every source/test file claimed as delivered>
- **Test output:** <exact command(s) run and raw output — Agent-D reruns them>
- **Raw commands + output:** <paste below>

```
<paste exact commands and output>
```

---

## Verification steps

Leakage audit is adversarial: the default assumption is that the feature path
peeks at the future until proven otherwise.

1. **Bar boundary.** Confirm features consume only *closed* bars. Check the
   forming bar is never a feature input (`BarFinalizer`, closed-only
   semantics).
2. **Point-in-time.** For a sample of timestamps, confirm every feature value
   at time T uses only bars with `open_time <= T` and none from after T.
3. **Shift/indexing.** Inspect rolling/EMA/RSI/ATR lookbacks for off-by-one
   that leaks the current bar's close into the prior window.
4. **9-candle window.** Confirm the "latest 9 closed candles" trigger window is
   implemented as specified (currently no such window is known to exist — see
   AUDIT-T12 note; verify against code, do not assume).
5. **Normalization/fit scope.** Any scaling or statistic fitted on the whole
   sample must be fitted on the training partition only (fit-before-split is a
   classic leak).
6. **Reward structure (RULE A).** Confirm the feature layer does not encode an
   asymmetric target/stop construction that manufactures win rate.
7. **Reproduce independently.** Run Agent-A's tests from a clean build. Never
   trust the claim; rerun.

---

## Result

PASS | FAIL | NEEDS WORK

<One-line justification. If FAIL/NEEDS WORK, list exact failures with file:line
and the command that demonstrates each.>

---

## Notes

<Anything the Lead should know: residual uncertainty, evidence limits (e.g.
synthetic vs real MT5 data), scope of what was and was not exercised.>
