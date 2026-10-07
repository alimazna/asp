# MISSION — AURA / ASTRA

> **Status:** CANONICAL for the coordination phase.
> **Owner:** DeepSeek (Lead / Commander).
> **Protocol:** see `README.md`. **Task table:** see `tasks.md`. **Live snapshot:** see `state.md`.

---

## 1. Mission

Build the best possible **decision-support backend for ASTRA** — a calibrated,
honest analytical system for XAUUSD — and hand it to the frontend team against a
**frozen API contract**.

The backend:

- reads live market context (9 timeframes × 3 months + the latest 9 closed candles
  per TF);
- produces a **calibrated probability** of the next move (UP / DOWN / FLAT) over a
  **defined horizon**;
- suggests a **stop loss** and **take profit** for the selected direction and
  reports the **reward/risk** ratio;
- reports **confidence and coverage** honestly;
- exposes all of this through a stable, loopback-only local API (`127.0.0.1`);
- is fully ready to be consumed by the ASTRA frontend.

The backend is **not an oracle** and does not force trades. It provides *analysis*;
the human decides whether and how to trade. The deliverable is not "a signal" — it
is a calibrated probability the system can honestly stand behind, produced under an
auditable process that never looks ahead and never invents an edge.

**Success** is a live, calibrated, honest, documented decision-support backend that
the frontend can consume. **Success is NOT a guaranteed profitable strategy.** If
calibration fails (ECE > 0.10), the output is labelled a *score*, not a
*probability*, and the backend still ships with the handoff guide (RULE C).

## 2. Scope

- **Data window:** 3 months of history.
- **Timeframes:** 9 canonical streams — M1, M5, M15, M30, H1, H4, D1, W1, MN1.
- **Trigger window:** the latest **9 closed candles** per timeframe.
- **Authority:** H4 is the primary structural authority; M15 is the primary
  operational/setup timeframe (consistent with `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`).
- **Causality:** closed bars only. No future information may enter a decision.

## 3. Goal

Output a **calibrated probability**, not merely a direction. A signal without a
trustworthy probability is a hypothesis, not a result.

## 4. Success criteria

- **ECE < 0.05** (expected calibration error).
- **Brier score better than the 0.25 baseline** (i.e. better than an uninformative
  50/50 forecast).
- **Honest coverage** — the system reports where it is confident and where it is not,
  and does not hide low-coverage regimes.

## 5. Failure criteria

Any one of these is a failure, regardless of headline win rate:

- **ECE > 0.10.**
- **Brier = baseline** (no skill over an uninformative forecast).
- **OOS collapse** — performance that does not survive out-of-sample.

## 6. Hard rules (binding)

1. **Baseline is READ-ONLY.** The control strategies (`base9`, `baseold`) are never
   edited, tuned, or "improved".
2. **Production is PROTECTED.** No production code, contracts, or state enums are
   modified by this program.
3. **No live trading.** SHADOW only. There is no command or code path that enables
   live execution.
4. **No lookahead.** Closed-bar causality; no future information; no silent repainting.

Plus the five mission rules:

- **RULE A — No reward-structure artifact.** Results must not be manufactured by an
  asymmetric reward structure (e.g. wide targets / tight stops producing a flattering
  win rate that does not survive realistic costs).
- **RULE B — 3 cost tiers mandatory.** Every result is reported under three realistic
  cost tiers (spread / commission / slippage). A result that only holds at zero cost
  is not a result.
- **RULE C — Calibration before probability.** No probability output is published
  until it is calibrated and its calibration is audited.
- **RULE D — Coverage honesty.** Report coverage explicitly. Never present a
  high-confidence subset as if it were the whole sample.
- **RULE E — Never delete history.** All logs, experiments, and reports are
  append-only. Negative and refuted results are kept.

## 7. Time horizon

**6–8 weeks to the first real answer.** Intermediate milestones (features, baseline
control, calibration) are checkpointed in `tasks.md` and `state.md`; the first
*real* answer is the first honestly calibrated, cost-aware, out-of-sample result.

## 8. Metric definitions (shared vocabulary)

- **Win rate** — fraction of trades that end positive. On its own it says nothing
  about profitability.
- **Expectancy** — average outcome per trade (in R or currency). This is what pays.
- **PF (profit factor)** — gross profit ÷ gross loss. > 1 is required to be viable.
- **Total R** — sum of outcomes in R units across the sample.
- A high win rate with negative expectancy is a **reward-structure artifact**
  (RULE A), not an edge.

## 9. Non-goals

- Claiming profitability, broker validation, or live-trading safety without evidence.
- Modifying the baseline, production code, or archived history.
- Shipping a probability before it is calibrated (RULE C).

## 10. Phase 4.0 — Decision-support backend & frontend handoff

The mission's final form. Sections 1–9 remain binding; this section defines the
product surface.

### 10.1 Prediction target (T14)

At each decision timestamp T the backend emits a **direction** (UP / DOWN / FLAT)
over a **defined horizon**, with a calibrated probability and a confidence
interval. The horizon is a Lead decision documented in
`docs/architecture/DECISION_MODEL.md` (candidates: next closed M15, next 4 closed
M15, next closed H1, next closed H4). The choice must be justified on the
trade-off between **calibration strength** and **signal usefulness**, and the
trade-off recorded.

### 10.2 Stop loss / take profit (T14)

For each directional signal the backend suggests `entry`, `stop_loss`,
`take_profit`, `reward_risk`, and a `suggested_risk_pct` (one of 0.25 / 0.5 / 1.0).
It **suggests only**; the human decides. One canonical SL/TP per the primary API;
alternative methods may be reported additively. Methods (ATR / structure /
volatility for SL; RR / structure / probability-scaled for TP) are chosen,
justified, and tested in `DECISION_MODEL.md`.

### 10.3 API contract (T15, T17)

The loopback API is extended for analysis and then **frozen before handoff**:

```text
GET /api/v1/analysis/latest     -> context + signal + levels + meta
GET /api/v1/analysis/history    -> recent signals (?limit=N)
GET /api/v1/context/latest      -> market context only
GET /api/v1/health              -> liveness
```

Rules: versioned (`v1`); loopback-only (`127.0.0.1`); JSON only; no auth in v1
(loopback + local process); **additive changes only** within v1; frozen before
frontend integration. The frozen spec is
`docs/architecture/BACKEND_FRONTEND_API_V1.md` (T17). When the backend does not
know a value it emits explicit `null` / `UNKNOWN`; the frontend must render that
as *unavailable*, never as healthy, zero, or safe.

### 10.4 Frontend handoff guide (T16)

`docs/frontend/FRONTEND_HANDOFF_GUIDE.md` is a **required deliverable**, not a
nice-to-have: overview; how to run the backend; full API spec with real example
payloads; field-by-field interpretation; recommended screens; a pointer to
`docs/brand/ASTRA_VISUAL_IDENTITY.md` (no invented branding); error handling
(OFFLINE / DEGRADED / STALE); integration rules (frontend reads the API only —
never the bridge, never MT5 directly); local dev setup; versioning policy.

### 10.5 Mock data (T18)

`scripts/mock_api.py` serves the **frozen contract** with realistic synthetic data
so the frontend can be built offline. Every mock payload must validate against the
frozen schema.

### 10.6 Decision-grade results (RULE B)

Decision-grade results use **three realistic cost tiers** (spread 0.30 +
commission, plus slippage). A result that holds only at zero cost is not a result.

### 10.7 Backend complete

All features, model+calibration (ECE reported honestly, pass or fail), bridge +
packaging, analysis API, decision-model doc, handoff guide, frozen API v1, mock
generator, and end-to-end are DONE and audited; baseline and production untouched;
no live-trading path added; the frontend can build against the frozen contract
using mock data, then swap to live. **"Complete" does not require profit** — it
requires an honest calibration result, a stable API, a complete guide, and
frontend-readiness.

---

*This mission is the shared contract between the five agents. Any change to it
requires a Lead decision recorded in `coordination/deepseek/comm.md`.*
