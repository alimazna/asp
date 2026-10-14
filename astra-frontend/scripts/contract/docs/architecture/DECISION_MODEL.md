# DECISION_MODEL — Prediction horizon & SL/TP methods (T15)

> **Status:** DRAFT — Lead design; empirical validation by Agent-B pending.
> **Owner:** Lead + Agent-B. **Reviewer:** Agent-D.
> **Binding rules:** RULE A (no reward-structure artifact), RULE B (3 cost
> tiers), RULE C (calibration before probability), RULE D (coverage honesty).

This document defines **what the backend predicts** and **how it suggests
levels**. It is the contract behind `GET /api/v1/analysis/latest`. Every numeric
choice here must be justified and then validated empirically; where the evidence
contradicts the design, the design changes and the change is recorded here.

---

## 1. Prediction target

At each decision timestamp `T` (the close of the most recent fully-closed bar on
the decision timeframe), the backend emits:

| field | meaning |
|---|---|
| `direction` | `UP` / `DOWN` / `FLAT` |
| `horizon` | the forward window (see §1.2) |
| `probability` | calibrated `P(direction = UP | context)` |
| `confidence_lo` / `confidence_hi` | interval around `probability` |

### 1.1 Label definition

`FLAT` exists so the model is not forced to invent a direction on a quiet bar.
Label at `T` for a horizon of `H` bars:

- `UP`   if `close(T+H) − close(T) >  θ`
- `DOWN` if `close(T) − close(T+H) >  θ`
- `FLAT` otherwise

`θ` is a **cost-aware dead-band**, not a free parameter: it must be at least the
round-trip cost of the decision timeframe (RULE B — spread 0.30 + commission,
plus a slippage allowance). A dead-band below cost produces a direction signal
that cannot pay for itself; that is a reward-structure artifact (RULE A).

### 1.2 Horizon — decision

Candidate horizons and the trade-off:

| horizon | calibration | usefulness | notes |
|---|---|---|---|
| next closed **M15** | noisiest | highest (fast feedback) | micro-structure dominated |
| next **4 closed M15** (~1h) | better | high | matches H1 authority |
| next closed **H1** | good | medium | structural, slower to update |
| next closed **H4** | strongest | lowest | ~1 signal per 4h |

**Chosen primary horizon: next 4 closed M15 bars (~1 hour), with `FLAT` via a
cost-aware dead-band.** Rationale: M15 is the operational timeframe (the M15
trigger already exists in the feature layer), and 4 bars gives the label enough
travel to clear realistic costs while keeping the signal useful for a
decision-support product that updates on the M15 clock. **The final choice is
contingent on empirical calibration strength** — if Agent-B's validation shows
the H1 label calibrates materially better, we switch to H1 and record it here.
We will not publish a probability until the chosen horizon clears ECE < 0.05
(RULE C).

> **Validation caveat (T15, 2026-10-07 23:05 UTC).** On the current synthetic
> data the **H=1** (next-bar) label calibrates *perfectly* (ECE 0.0000). That is
> a **generator artifact** — the synthetic generator drives price and features
> from the same latent state — **not model skill**, and it must not be presented
> as a horizon recommendation. H=4 (ECE 0.026) is the only usable synthetic
> signal. **No horizon can be recommended until real data exists** (E05). The
> design above stands on trading logic, not on the synthetic calibration.


---

## 2. Stop-loss methods

Candidates, all computed on the decision timeframe from closed bars only:

| method | formula | strength | weakness |
|---|---|---|---|
| `atr_1.5x` | `entry ∓ 1.5 × ATR(14)` | adapts to volatility | ignores structure |
| `structure` | below recent swing low / above swing high | respects market structure | can be very wide in trends |
| `vol_k` | `entry ∓ k × σ(N bars)` | simple, stationary | assumes normality |

**Chosen primary: `atr_1.5x`.** It is the most defensible default: it scales
automatically with the regime, it is deterministic, and it has a single,
testable parameter. Structure-based SL is reported additively where a clear
swing exists. The multiplier is **not** tuned on 2025.

## 3. Take-profit methods

| method | formula | strength | weakness |
|---|---|---|---|
| `rr_2x` | `entry ± 2.0 × SL distance` | keeps RR explicit | ignores resistance |
| `structure` | next resistance / support | realistic target | sparse in quiet regimes |
| `prob_scaled` | smaller TP when `p` is high | raises hit rate | can mask negative expectancy |

**Chosen primary: `rr_2x`** (RR = 2.0 fixed). **`prob_scaled` is explicitly
rejected as the primary**: shrinking the target as probability rises is the exact
mechanism RULE A warns about — it inflates hit rate without improving expectancy.
It may be reported as an additive alternative for the human, clearly labelled,
but it is never the canonical level.

`reward_risk` in the API is derived, not asserted: `|TP − entry| / |entry − SL|`.

## 4. Suggested risk

The backend suggests one of **0.25 / 0.5 / 1.0 %** account risk, derived from the
calibrated probability tier and coverage. It **suggests only**; the human decides.
No position sizing, no order, and no live execution is ever produced (L3).

## 5. Cost model (RULE B)

Every decision-grade number is reported under **three cost tiers**:

1. **zero** — reference only, never decision-grade;
2. **spread 0.30 + commission** — the realistic floor;
3. **spread 0.30 + commission + slippage** — the conservative tier.

The dead-band `θ` (§1.1) and the RR target are validated against tier 2/3, not
tier 1.

## 6. Validation plan (Agent-B)

1. Build labels for each candidate horizon; report class balance and coverage.
2. Calibrate (Platt / isotonic); report **ECE and Brier per horizon and per cost
   tier**, out-of-sample, with the walk-forward split (dev 2021–22, val 2023–24,
   OOS 2025). No tuning on 2025.
3. Pick the horizon with the strongest honest calibration that still clears the
   cost dead-band; if none clears ECE < 0.05, label the output a **score**.
4. Recompute SL/TP hit statistics under the three cost tiers; confirm the RR
   target is not a reward-structure artifact (RULE A).
5. Record every result, including negative ones (RULE E).

## 7. Open questions

- **Q-horizon:** does the 4×M15 label calibrate as well as H1? (Agent-B)
- **Q-theta:** is a cost-aware dead-band enough, or do we need a volatility-scaled
  one? (Agent-B)
- **Q-data:** no real XAUUSD data is in the repo yet — all of the above is
  validated on synthetic data until real data lands, and will be labelled as such.
