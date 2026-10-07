# Tasks — AURA / ASTRA Coordination

> **Owner:** DeepSeek (Lead). Agents edit only the `Owner`, `Status`, and `Lease until`
> columns of rows they own or are claiming (per `README.md` §D).
> **Statuses:** IDLE | ACTIVE | BLOCKED | REVIEW | DONE | ABANDONED

---

## Current sprint focus

**Sprint 0 — Structure & onboarding.** No research work begins until all four
specialists (Agent-A/B/C/D) have joined and acknowledged the mission in their
`comm.md`. First real sprint will be **Sprint 1 — Features + Baseline**
(T01, T02, T12), which establishes the baseline control results before any
modelling begins.

## Open questions

- Q1: Where does the `research/astra_3month_mtf/` layer live — this repo, a branch,
  or an external workspace? (Lead to confirm before research starts.)
- Q2: Which broker/account supplies the 3-month × 9-timeframe dataset, and what are
  the three mandatory cost tiers (spread / commission / slippage assumptions)?
- Q3: What is the canonical `base9` / `baseold` definition and its exact prior
  control numbers?

## Blocked items

- (none yet — research tasks are gated on Sprint 0 onboarding, not blocked)

---

## Task table

| ID  | Task                    | Owner    | Reviewer | Status | Lease until |
|-----|-------------------------|----------|----------|--------|-------------|
| T01 | Feature extraction      | Agent-A  | Agent-D  | IDLE   | -           |
| T02 | Feature tests           | Agent-A  | Agent-D  | IDLE   | -           |
| T03 | Logistic baseline       | Agent-B  | Agent-D  | IDLE   | -           |
| T04 | XGBoost + calibration   | Agent-B  | Agent-D  | IDLE   | -           |
| T05 | Calibration metrics     | Agent-B  | Agent-D  | IDLE   | -           |
| T06 | MT5 bridge              | Agent-C  | Agent-D  | IDLE   | -           |
| T07 | Python bundling         | Agent-C  | Agent-D  | IDLE   | -           |
| T08 | Windows packaging       | Agent-C  | Agent-D  | IDLE   | -           |
| T09 | Probability API         | Agent-C  | Agent-D  | IDLE   | -           |
| T10 | Leakage audit           | Agent-D  | Lead     | IDLE   | -           |
| T11 | Calibration audit       | Agent-D  | Lead     | IDLE   | -           |
| T12 | Baseline control check  | Agent-D  | Lead     | IDLE   | -           |
| T13 | End-to-end integration  | Agent-C  | All      | IDLE   | -           |

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
