# MISSION — AURA / ASTRA

> **Status:** CANONICAL for the coordination phase.
> **Owner:** DeepSeek (Lead / Commander).
> **Protocol:** see `README.md`. **Task table:** see `tasks.md`. **Live snapshot:** see `state.md`.

---

## 1. Mission

Build a **calibrated analytical trading system for XAUUSD**.

The deliverable is not "a signal". The deliverable is a **calibrated probability** —
a number the system can honestly stand behind, with measured reliability — produced
under an auditable process that never looks ahead and never invents an edge.

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

---

*This mission is the shared contract between the five agents. Any change to it
requires a Lead decision recorded in `coordination/deepseek/comm.md`.*
