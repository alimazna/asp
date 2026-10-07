# ASTRA — Frontend Handoff Guide (Phase 4.0 / API v1)

> **Status:** DRAFT — Lead deliverable (T18). The analysis surface below is the
> **target contract**; it is frozen by T17 and served by T16. The existing
> backend contract is documented in `docs/frontend/ASTRA_FRONTEND_HANDOFF.md`.
> **Audience:** the frontend team building the ASTRA desktop app.
> **Rule:** where the backend does not know a value it emits `null` / `UNKNOWN`.
> The frontend renders that as **unavailable** — never as healthy, zero, or safe.

---

## A. Overview

- **ASTRA** — the desktop product / visual identity (what the frontend builds).
- **AURA** — the backend (this repository).
- **What the backend does** — reads live market context for `XAUUSD` (9
  timeframes × 3 months + the latest 9 closed candles per TF), produces a
  **calibrated probability** of the next move over a defined horizon, suggests a
  **stop loss / take profit**, reports **reward/risk**, **confidence**, and
  **coverage**, and exposes it all over a loopback-only JSON API.
- **What the frontend does** — reads the API and presents analysis. It **never**
  forces a trade; the human decides.
- **Mode** — `SHADOW` only. Live trading is never authorised.

## B. Running the backend

1. Double-click `ASTRA.exe` (Windows). No CMD window, no user-installed Python.
2. Wait for readiness — poll `GET /api/v1/health` until `status = "ok"`.
3. Confirm freshness via `meta.data_freshness_sec`.

**Common startup errors**

| symptom | cause | fix |
|---|---|---|
| health never `ok` | MT5 not running / not logged in | start MT5, log in, relaunch |
| `symbol_not_found` | broker names gold differently | set the broker symbol alias in config |
| bridge `stale` | no new bars arriving | check the MT5 terminal / market hours |
| port in use | another instance running | close the other instance |

## C. API contract — full spec

Base: `http://127.0.0.1:8790/api/v1/` — **loopback only**, JSON only, no auth in
v1 (local process only). Additive changes only within v1.

### `GET /api/v1/analysis/latest`

```json
{
  "timestamp": "2026-10-08T14:30:00Z",
  "symbol": "XAUUSD",
  "context": {
    "regime": "RANGE",
    "h4_bias": "UP",
    "m15_trigger": "LONG",
    "mtf_agreement": 0.72,
    "volatility_state": "NORMAL"
  },
  "signal": {
    "direction": "UP",
    "horizon": "next_4xM15",
    "probability": 0.63,
    "probability_calibrated": true,
    "confidence_lo": 0.57,
    "confidence_hi": 0.69,
    "model_version": "v1.0",
    "features_contributing": [
      {"name": "h4_bias_up", "weight": 0.12}
    ]
  },
  "levels": {
    "entry": 2650.30,
    "stop_loss": 2646.10,
    "take_profit": 2658.70,
    "reward_risk": 2.05,
    "suggested_risk_pct": 0.5,
    "sl_method": "atr_1.5x",
    "tp_method": "rr_2x"
  },
  "meta": {
    "coverage_tier": "high",
    "data_freshness_sec": 3,
    "degraded": false,
    "score_is_probability": true,
    "disclaimer": "Decision support only. Not financial advice."
  }
}
```

### `GET /api/v1/analysis/history?limit=N`
Array of the same object, most-recent-first. `limit` default 50, max 500.

### `GET /api/v1/context/latest`
The `context` object plus `timestamp` / `symbol`.

### `GET /api/v1/health`
`{"status":"ok|degraded|offline","bridge":"ok|stale|offline","version":"v1","uptime_sec":N}`

### Error schema (all endpoints)

```json
{"error": {"code": "symbol_not_found", "message": "human-readable", "retryable": false}}
```

## D. Field-by-field interpretation

| field | means | frontend shows | optional in v1 |
|---|---|---|---|
| `signal.probability` | calibrated `P(UP)` | percentage + interval | no |
| `signal.probability_calibrated` | `false` ⇒ value is a **score** | "score" label, not "probability" | no |
| `signal.confidence_lo/hi` | uncertainty band | as a range, never a point | yes |
| `context.regime` | market state | regime chip | no |
| `context.mtf_agreement` | 0–1 multi-TF agreement | meter | yes |
| `levels.*` | suggested levels | **suggestions**, clearly labelled | yes |
| `levels.reward_risk` | derived RR | number | yes |
| `meta.coverage_tier` | `high/medium/low` | coverage badge (RULE D) | no |
| `meta.degraded` | backend degraded | banner; suppress confident styling | no |
| `meta.score_is_probability` | calibration verdict | drives the probability-vs-score label | no |

**Guaranteed in v1:** `timestamp`, `symbol`, `signal.direction`,
`signal.probability`, `signal.probability_calibrated`, `meta.coverage_tier`,
`meta.degraded`. Everything else may be `null` and must degrade gracefully.

## E. Recommended screens

1. **Live dashboard** — context (regime, H4 bias, M15 trigger, agreement) +
   signal (direction, probability **or** score, interval) + levels (entry/SL/TP/RR).
2. **History** — recent signals from `/analysis/history`, filterable.
3. **Health / status** — `/health`, bridge state, freshness, degraded banner.

## F. Visual language

Follow `docs/brand/ASTRA_VISUAL_IDENTITY.md`. Colors, typography, spacing come
from there. **Do not invent new branding.**

## G. Error handling

| state | trigger | show |
|---|---|---|
| `OFFLINE` | health offline / unreachable | "Backend offline", disable actions |
| `DEGRADED` | `meta.degraded = true` | warning banner, mute confidence styling |
| `STALE` | `data_freshness_sec` above threshold | "Data stale — values may be old" |
| `UNKNOWN`/`null` | field absent | "unavailable" — never 0, never "safe" |

**Never fabricate data.** A missing value is shown as missing.

## H. Integration rules

- The frontend reads the **API only**. Never the bridge, never MT5, never the
  broker.
- The frontend never writes persistence and never bypasses backend policy.
- The frontend never blocks the backend; poll, don't hammer.
- The backend is the **source of truth**.

## I. Local dev setup

- Backend API: `http://127.0.0.1:8790/api/v1/` (loopback). Bridge is internal
  (`127.0.0.1:8791`) and the frontend must not contact it.
- Offline development: run `scripts/mock_api.py` (T19) — it serves the **frozen
  contract** with realistic synthetic data, so the frontend can be built and
  tested without MT5 or live data. Swap the base URL to the live backend when
  ready; no code change beyond the base URL.
- Mock payloads validate against the frozen schema (T17).

## J. Versioning policy

- `v1` is **frozen** when handed off (T17). It is tagged and recorded in
  `docs/architecture/BACKEND_FRONTEND_API_V1.md`.
- Additive changes only within `v1`, through a documented process.
- Breaking changes require `v2`; `v1` keeps serving until clients migrate.
