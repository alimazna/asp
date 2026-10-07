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
| T04 | XGBoost + calibration   | Agent-B  | Agent-D  | DONE   | -           |
| T05 | Calibration metrics     | Agent-B  | Agent-D  | DONE   | -           |
| T06 | MT5 bridge              | Agent-C  | Agent-D  | DONE   | -           |
| T07 | Python bundling         | Agent-C  | Agent-D  | DONE   | -           |
| T08 | Windows packaging       | Agent-C  | Agent-D  | IDLE   | -           |
| T09 | Probability API         | Agent-C  | Agent-D  | DONE   | -           |
| T10 | Leakage audit           | Agent-D  | Lead     | DONE   | -           |
| T11 | Calibration audit       | Agent-D  | Lead     | DONE   | -           |
| T12 | Baseline control check  | Agent-D  | Lead     | DEFERRED | -         |
| T13 | End-to-end integration  | Agent-C  | All      | IDLE   | -           |
| T14 | Feature bounds/NaN guards| Agent-A | Agent-D  | DONE   | -           |
| T15 | Decision model (horizon+SL/TP) | Lead+Agent-B | Agent-D | DONE | -           |
| T16 | Analysis API endpoints  | Agent-C  | Agent-D  | REVIEW | -           |
| T17 | Freeze API v1           | Agent-C  | Agent-D  | REVIEW | -           |
| T18 | Frontend handoff guide  | Lead     | Agent-D  | ACTIVE | 23:35 UTC   |
| T19 | Mock data generator     | Agent-C  | Agent-D  | REVIEW | -           |
| T20 | Cost-tier model (RULE B)| Agent-B  | Agent-D  | REVIEW | -           |
| T21 | Integration causality test | Agent-A | Agent-D | DONE  | -           |

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
- **T15 (Lead+Agent-B):** decision model — prediction horizon + SL/TP methods,
  justified and tested. Deliverable `docs/architecture/DECISION_MODEL.md`.
- **T16/T17/T19 (Agent-C):** analysis API endpoints, API v1 freeze, mock generator.
- **T18 (Lead, Agent-C input):** frontend handoff guide.
- **T20 (Agent-B):** 3-cost-tier model (RULE B) — spread 0.30 + commission, plus
  slippage; required for any decision-grade result. **Path ruling (Lead, 23:25
  UTC): canonical model lives in-zone at `src/models/costs.py`** (not `src/costs/`);
  the deliverable path was a detail — one canonical definition in the owner's zone
  beats a cross-zone directory. `levels.py` refactored onto it.
- **T21 (Agent-A):** integration causality test (T13 support) — interior-instant
  equals truncated-prefix across streams; already prototyped in-zone.

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
| 2026-10-07 22:13   | Agent-B  | T05 F1 fixed (structural separation guard) → REVIEW; 162 pass.|
| 2026-10-07 22:14   | Agent-D  | T05 re-audit PASS → REVIEW; T11 ready to open.             |
| 2026-10-07 22:15   | Agent-A  | T14 bounds/NaN guards + interpretability index → REVIEW.    |
| 2026-10-07 22:16   | DeepSeek | T05 PASS; T11 ACTIVE. Phase 4.0: T15–T19 added (T14 taken). |
| 2026-10-07 22:22   | Agent-D  | T14 audit PASS; T09 audit PASS (caveat C-1).                |
| 2026-10-07 22:24   | DeepSeek | T05/T09/T14 → DONE; T11 go/no-go granted.                   |
| 2026-10-07 22:30   | Agent-D  | T11 audit PASS (methodology); publication gated on real data.|
| 2026-10-07 22:36   | DeepSeek | T11 → DONE; T20 (cost tiers) + T21 (T13 causality) added.   |
| 2026-10-07 22:50   | Agent-B  | T04 submitted (stdlib GBT, 178 tests); T15 submitted (212). |
| 2026-10-07 23:05   | Agent-C  | T16/T17/T19 delivered (frozen schema, mock 39/39).         |
| 2026-10-07 23:05   | Agent-D  | T04 audit PASS (deep-tree fix verified).                    |
| 2026-10-07 23:08   | DeepSeek | T04 → DONE; T15/H=1 caveat + T18 corrections addressed.     |
| 2026-10-07 23:20   | Agent-B  | T20 cost tiers in-zone (src/models/costs.py); path question.|
| 2026-10-07 23:22   | Agent-D  | T21 audit PASS; T15 audit NEEDS WORK (F15-1 demo artifact).  |
| 2026-10-07 23:25   | DeepSeek | T21 → DONE; T15 → ACTIVE (F15-1 fix); T20 path ruled in-zone.|
| 2026-10-07 23:40   | Agent-B  | T15 F15-1/2/4 fixed (demo refuses artifact); T17 null rec.  |
| 2026-10-07 23:45   | Agent-D  | T15 re-audit PASS (F15-1 regression test has teeth).         |
| 2026-10-07 23:48   | DeepSeek | T15 → DONE; T17 freeze ruled: null levels + v1.x additive.  |
