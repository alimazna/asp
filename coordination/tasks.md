# Tasks — AURA / ASTRA Coordination

> **Owner:** DeepSeek (Lead). Agents edit only the `Owner`, `Status`, and `Lease until`
> columns of rows they own or are claiming (per `README.md` §D).
> **Statuses:** IDLE | ACTIVE | BLOCKED | REVIEW | DONE | ABANDONED

---

## Current sprint focus

**Sprint 1 — Features & Control Baseline.**
Goal: feature extraction plus baseline control verification.
Status: **T01 DONE, T02 DONE** (both audited by Agent-D). **T12 DEFERRED** by
human decision (baseline controls unavailable; revisit only if needed).
Sprint 1 features complete; control comparison deferred.

All task statuses remain **IDLE** until each agent claims their first task with a
lease (per `README.md` §F).

## Open questions

- Q1: Where does the `research/astra_3month_mtf/` layer live — this repo, a branch,
  or an external workspace? (Lead to confirm before research starts.)
- Q2: Which broker/account supplies the 3-month × 9-timeframe dataset, and what are
  the three mandatory cost tiers (spread / commission / slippage assumptions)?
- Q3: What is the canonical `base9` / `baseold` definition and its exact prior
  control numbers?
- Q5: Discord webhook configured? (yes/no)
- Q6: Which remote branch is canonical? (main)

## Blocked items

- (none yet — research tasks are gated on Sprint 0 onboarding, not blocked)

---

## Task table

| ID  | Task                    | Owner    | Reviewer | Status | Lease until |
|-----|-------------------------|----------|----------|--------|-------------|
| T01 | Feature extraction      | Agent-A  | Agent-D  | DONE   | -           |
| T02 | Feature tests           | Agent-A  | Agent-D  | DONE   | -           |
| T03 | Logistic baseline       | Agent-B  | Agent-D  | DONE   | -           |
| T04 | XGBoost + calibration   | Agent-B  | Agent-D  | IDLE   | -           |
| T05 | Calibration metrics     | Agent-B  | Agent-D  | ACTIVE | 22:40 UTC   |
| T06 | MT5 bridge              | Agent-C  | Agent-D  | DONE   | -           |
| T07 | Python bundling         | Agent-C  | Agent-D  | DONE   | -           |
| T08 | Windows packaging       | Agent-C  | Agent-D  | IDLE   | -           |
| T09 | Probability API         | Agent-C  | Agent-D  | IDLE   | -           |
| T10 | Leakage audit           | Agent-D  | Lead     | DONE   | -           |
| T11 | Calibration audit       | Agent-D  | Lead     | IDLE   | -           |
| T12 | Baseline control check  | Agent-D  | Lead     | DEFERRED | -         |
| T13 | End-to-end integration  | Agent-C  | All      | IDLE   | -           |
| T14 | Feature bounds/NaN guards| Agent-A | Agent-D  | IDLE   | -           |

---

## Task notes

- **T01/T02 (Agent-A):** feature extraction over 3 months × 9 timeframes plus the
  latest 9 closed candles; feature tests must prove no lookahead (RULE 4) and no
  reward-structure artifact (RULE A).
- **T03/T04/T05 (Agent-B):** logistic baseline, then XGBoost with calibration;
  calibration **before** any probability output (RULE C).
- **T06/T07/T08/T09 (Agent-C):** MT5 bridge, Python bundling, Windows packaging,
  probability API — all must keep production protected and live trading disabled.
- **T10/T11/T12 (Agent-D):** independent audits — leakage, calibration, baseline
  control. Agent-D reports to the Lead, not to the task owner.
- **T13 (Agent-C, reviewed by All):** end-to-end integration once T01–T09 are DONE.

## Change log

| When (UTC)         | Who      | Change                                                    |
|--------------------|----------|-----------------------------------------------------------|
| 2026-10-07 20:34   | DeepSeek | Initialized task table (T01–T13), all IDLE.               |
| 2026-10-07 21:05   | DeepSeek | Roles assigned (A/B/C/D); Sprint 1 activated (T01,T02,T12).|
| 2026-10-07 21:20   | DeepSeek | T06 PASS by Agent-D → DONE. T01 in review. F1 zone ratified.|
| 2026-10-07 21:29   | DeepSeek | T07 PASS → DONE. T01/T10 REJECTED (leakage) → T01 ACTIVE.  |
| 2026-10-07 21:35   | DeepSeek | T01 F1/F2 fixed by Agent-A → REVIEW; T10 re-audit requested.|
| 2026-10-07 21:42   | DeepSeek | T01/T10 re-audit PASS → DONE. T02/T03/T08 opened.          |
| 2026-10-07 21:47   | Agent-B  | T03 claimed (lease 22:20 UTC) — logistic baseline.        |
| 2026-10-07 21:50   | DeepSeek | T02 PASS → DONE. Sprint 1 T01/T02 complete; T12 blocked.    |
| 2026-10-07 21:52   | DeepSeek | T12 → DEFERRED (human decision). Proceed without controls.  |
| 2026-10-07 21:55   | Agent-B  | T03 submitted → REVIEW (dataset/logistic/baseline, 119 tests).|
| 2026-10-07 21:56   | DeepSeek | T03 audit PASS → DONE. T05 (calibration) unblocked.         |
| 2026-10-07 22:00   | Agent-B  | T05 claimed (lease 22:25) — calibrators + metrics; T04 dep decision pending.|
| 2026-10-07 22:00   | DeepSeek | Phase 3.0 autonomy; T14 (bounds guards) added for Agent-A.  |
| 2026-10-07 22:05   | Agent-B  | T05 submitted → REVIEW (calibrators/calibrated runner, 150 tests).|
| 2026-10-07 22:06   | Agent-D  | T05 audit FAIL (F1 runner overlap guard) → back to ACTIVE.  |
