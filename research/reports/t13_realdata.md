# T13 — Real-data end-to-end replay transcript

> **PROOF-OF-CONCEPT — single window.** 3.5-month XAUUSD M1 corpus
> (2026-06-24 .. 2026-10-08 broker time). Not the publication verdict;
> no walk-forward / out-of-sample claim is made here.

- Corpus: `research/data/xauusd_m1/xauusd_m1_real.csv`
- Host: `build/aura_backend_host` (loopback :35975)
- Window: 2026-10-08 10:10:06 .. 2026-10-08 10:10:08 UTC

Path exercised: **replay feed -> real bridge -> real host ingest -> frozen
FeatureSets -> model behind the shared RULE C gate -> frozen v1 API.**
No test double stands in for AURA code; the only double is the external
MetaTrader5 package, which serves the committed corpus.

## RULE C (live)

- `signal.probability` = `null`
- `signal.probability_calibrated` = `false`
- `meta.score_is_probability` = `false`
- gate closed (probability withheld): **True**

## Transcript

### stage: replay

**`GET /api/v1/bridge/status`** — bridge handshake + resolved symbol  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "bridge_process_state": "ONLINE",
    "bridge_symbol": "XAUUSD",
    "broker": "FakeBroker Ltd",
    "handshake_ok": true,
    "host": "127.0.0.1",
    "initialized": true,
    "last_error": "",
    "last_successful_request": 1791454207000,
    "loopback_only": true,
    "managed_by_application": true,
    "mt5_ready": true,
    "mt5_ready_live": true,
    "observed": true,
    "package_available": true,
    "process_state": "ONLINE",
    "requires_manual_cmd": false,
    "resolved_symbol": "XAUUSD",
    "server": "FakeServer-Demo",
    "startup_stage": "READY",
    "transport": "http_loopback"
  },
  "schema": "1.0"
}
```

### stage: features

**`GET /api/v1/timeframes`** — per-timeframe closed-bar + quality/freshness  (HTTP 200)

```json
{
  "api": "v1",
  "data": [
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 67265,
        "is_fresh": true,
        "last_update": 1791454140000,
        "max_age_millis": 180000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791454140,
      "last_successful_update": 1791454140,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 500,
      "timeframe": "M1"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 307265,
        "is_fresh": true,
        "last_update": 1791453900000,
        "max_age_millis": 900000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791453900,
      "last_successful_update": 1791453900,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 500,
      "timeframe": "M5"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 1507265,
        "is_fresh": true,
        "last_update": 1791452700000,
        "max_age_millis": 2700000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791452700,
      "last_successful_update": 1791452700,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 500,
      "timeframe": "M15"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 2407265,
        "is_fresh": true,
        "last_update": 1791451800000,
        "max_age_millis": 5400000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791451800,
      "last_successful_update": 1791451800,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 500,
      "timeframe": "M30"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 4207265,
        "is_fresh": true,
        "last_update": 1791450000000,
        "max_age_millis": 10800000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791450000,
      "last_successful_update": 1791450000,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 500,
      "timeframe": "H1"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 22207265,
        "is_fresh": true,
        "last_update": 1791432000000,
        "max_age_millis": 43200000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791432000,
      "last_successful_update": 1791432000,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 455,
      "timeframe": "H4"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 123007265,
        "is_fresh": true,
        "last_update": 1791331200000,
        "max_age_millis": 259200000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1791331200,
      "last_successful_update": 1791331200,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 76,
      "timeframe": "D1"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 641407265,
        "is_fresh": true,
        "last_update": 1790812800000,
        "max_age_millis": 1814400000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1790812800,
      "last_successful_update": 1790812800,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 16,
      "timeframe": "W1"
    },
    {
      "capability_impact": [],
      "decision_grade": true,
      "freshness": {
        "age_millis": 2974207265,
        "is_fresh": true,
        "last_update": 1788480000000,
        "max_age_millis": 7776000000,
        "state": "FRESH"
      },
      "has_closed_bar": true,
      "last_closed_bar_open": 1788480000,
      "last_successful_update": 1788480000,
      "observed": true,
      "quality": {
        "decision_grade": true,
        "state": "VALID"
      },
      "sequence": 4,
      "timeframe": "MN1"
    }
  ],
  "schema": "1.0"
}
```

**`GET /api/v1/timeframes/M15/snapshot`** — M15 feature snapshot  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "capability_impact": [],
    "close": 4126.28,
    "freshness": {
      "age_millis": 1507265,
      "is_fresh": true,
      "last_update": 1791452700000,
      "max_age_millis": 2700000,
      "state": "FRESH"
    },
    "has_closed_bar": true,
    "high": 4126.74,
    "last_successful_update": 1791452700,
    "low": 4119.42,
    "observed": true,
    "open": 4120.66,
    "open_time": 1791452700,
    "quality": {
      "decision_grade": true,
      "state": "VALID"
    },
    "sequence": 500,
    "timeframe": "M15"
  },
  "schema": "1.0"
}
```

### stage: model

**`GET /api/v1/analysis/latest`** — context + signal + levels + meta  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "context": {
      "h4_bias": "DOWN",
      "m15_trigger": "SHORT",
      "mtf_agreement": null,
      "regime": "QUIET",
      "volatility_state": "LOW"
    },
    "levels": {
      "entry": 4126.28,
      "reward_risk": 1.666666667,
      "sl_method": null,
      "stop_loss": 4136.349286,
      "suggested_risk_pct": 0.338047088,
      "take_profit": 4109.497857,
      "tp_method": null
    },
    "meta": {
      "coverage_tier": "unknown",
      "data_freshness_sec": null,
      "degraded": false,
      "disclaimer": "Decision support only. Not financial advice.",
      "score_is_probability": false
    },
    "signal": {
      "confidence_hi": null,
      "confidence_lo": null,
      "direction": "DOWN",
      "features_contributing": [],
      "horizon": null,
      "model_version": null,
      "probability": null,
      "probability_calibrated": false,
      "score": 48.29244114
    },
    "symbol": "XAUUSD",
    "timestamp": "2026-10-08T09:45:00Z"
  },
  "schema": "1.0"
}
```

**`GET /api/v1/analysis/history`** — recent decision records  (HTTP 200)

```json
{
  "api": "v1",
  "data": [
    {
      "context": {
        "h4_bias": "UNKNOWN",
        "m15_trigger": "UNKNOWN",
        "mtf_agreement": null,
        "regime": "UNKNOWN",
        "volatility_state": "UNKNOWN"
      },
      "levels": {
        "entry": null,
        "reward_risk": null,
        "sl_method": null,
        "stop_loss": null,
        "suggested_risk_pct": null,
        "take_profit": null,
        "tp_method": null
      },
      "meta": {
        "coverage_tier": "unknown",
        "data_freshness_sec": null,
        "degraded": false,
        "disclaimer": "Decision support only. Not financial advice.",
        "score_is_probability": false
      },
      "signal": {
        "confidence_hi": null,
        "confidence_lo": null,
        "direction": "DOWN",
        "features_contributing": [],
        "horizon": null,
        "model_version": null,
        "probability": null,
        "probability_calibrated": false,
        "score": 48.29244114
      },
      "symbol": "XAUUSD",
      "timestamp": "2026-10-08T09:45:00Z"
    }
  ],
  "schema": "1.0"
}
```

**`GET /api/v1/context/latest`** — market context only  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "context": {
      "h4_bias": "DOWN",
      "m15_trigger": "SHORT",
      "mtf_agreement": null,
      "regime": "QUIET",
      "volatility_state": "LOW"
    },
    "symbol": "XAUUSD",
    "timestamp": "2026-10-08T09:45:00Z"
  },
  "schema": "1.0"
}
```

**`GET /api/v1/risk/latest`** — risk proposal + portfolio  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "aggregate_open_risk_fraction": 0.00338047088,
    "available": true,
    "open_positions": 1,
    "proposal": {
      "decision": "REDUCED",
      "decision_id": "M15-1791452700-SHORT",
      "direction": "SHORT",
      "entry_price": 4126.28,
      "position_size_lots": 0.03357210209,
      "reason": "risk bounded by guardian policy",
      "reward_risk_ratio": 1.666666667,
      "risk_amount": 33.8047088,
      "risk_fraction": 0.00338047088,
      "stop_price": 4136.349286,
      "target_price": 4109.497857,
      "valid": true
    },
    "proposal_available": true,
    "proposal_reason": "risk bounded by guardian policy",
    "risk_bounded_by_guardian": true
  },
  "schema": "1.0"
}
```

### stage: api

**`GET /api/v1/system/state`** — host mode / readiness  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "api": "v1",
    "bridge_state": "ONLINE",
    "mode": "SHADOW",
    "ready": true,
    "schema": "1.0",
    "shadow_only": true,
    "startup_stage": "READY"
  },
  "schema": "1.0"
}
```

**`GET /api/v1/health`** — liveness  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "aggregate": "ONLINE",
    "decision_grade_data": true,
    "degraded_reasons": [],
    "observed_at": 1791454208835,
    "unknown_is_not_safe": true
  },
  "schema": "1.0"
}
```

**`GET /api/v1/research/status`** — research layer posture  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "available": true,
    "experiment_count": 0,
    "experiments": [],
    "failure_count": 0,
    "failures": [],
    "mode": "SHADOW",
    "note": "research output never grants execution authority"
  },
  "schema": "1.0"
}
```

**`GET /api/v1/governance/status`** — governance posture  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "history": [],
    "live_trading_authorised": false,
    "pending": [],
    "pending_count": 0
  },
  "schema": "1.0"
}
```

**`GET /api/v1/audit/recent`** — recent audit trail  (HTTP 200)

```json
{
  "api": "v1",
  "data": {
    "active_incidents": [],
    "audit_records": [
      {
        "action": "SHADOW_COMMAND_ISSUED",
        "actor": "aura-runtime",
        "details": "shadow command issued",
        "event_id": "SHADOW_COMMAND_ISSUED-2",
        "occurred_at": 1791454207265,
        "outcome": "SUCCESS",
        "previous_hash": "FNV1A64:c263ad13387b480b",
        "record_hash": "FNV1A64:d8dd17fc7fe12de9",
        "sequence": 2,
        "service_state": "ONLINE",
        "subject": "M15-1791452700-SHORT"
      },
      {
        "action": "DECISION_PRODUCED",
        "actor": "aura-runtime",
        "details": "decision evaluated on closed bar",
        "event_id": "DECISION_PRODUCED-1",
        "occurred_at": 1791454207265,
        "outcome": "SUCCESS",
        "previous_hash": ":",
        "record_hash": "FNV1A64:c263ad13387b480b",
        "sequence": 1,
        "service_state": "ONLINE",
        "subject": "M15-1791452700-SHORT"
      }
    ],
    "audit_stream_size": 2,
    "count": 2
  },
  "schema": "1.0"
}
```

---

Transcript generated by `scripts/t13_replay_transcript.py` (Agent-C).
AI agent (OpenHands/agent-c) on behalf of the operator.
