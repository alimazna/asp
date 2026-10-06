# BACKEND_FRONTEND_API_V1

## Purpose
Provide a stable contract for Alpha to build the ASTRA desktop application without depending on backend internals.

## Contract categories
1. Runtime state
2. Data health
3. Timeframe snapshots
4. Decision records
5. Risk proposals
6. Shadow positions/outcomes
7. Research status
8. Governance/approval state
9. Audit/incident state

## Minimum frontend-safe endpoints/contracts
The implementation may expose these through a local application API, in-process façade, or approved transport. The transport itself is a backend concern.

```text
GET  /api/v1/system/state
GET  /api/v1/health
GET  /api/v1/timeframes
GET  /api/v1/timeframes/{tf}/snapshot
GET  /api/v1/signals/latest
GET  /api/v1/risk/latest
GET  /api/v1/shadow/positions
GET  /api/v1/shadow/outcomes
GET  /api/v1/research/status
GET  /api/v1/governance/status
GET  /api/v1/audit/recent
GET  /api/v1/bridge/status
POST /api/v1/command
```

The concrete transport is a loopback-only HTTP/JSON server
(`aura::LoopbackApiServer`) on `127.0.0.1:8790`; the Python bridge is a separate
internal component on `127.0.0.1:8791` that the frontend never contacts.

Human control actions use explicit versioned command contracts and backend-side
authorization/policy checks. The command surface is an allow-list: only
`notify` and `request_approval` are accepted; every execution-like command
(including `live.execute`) is rejected with `403 command_not_permitted`. Every
command must carry an `actor`. Live execution remains prohibited.

## State model
Frontend must distinguish service states from system operating modes.

Service:
`STARTING, ONLINE, DEGRADED, OFFLINE, RECOVERING, PAUSED, BLOCKED, ERROR`

System:
`STARTING, RECOVERY, NORMAL, DEGRADED, SHADOW, PAUSED, MANUAL, EMERGENCY, HALTED`

## Timeframe display contract
Frontend should be able to show:
- M1/M5/M15/M30/H1/H4/D1/W1/MN1;
- freshness/quality;
- last successful update;
- exact failed timeframe;
- capability impact.

## Decision display contract
A decision should expose:
```text
decision_id
symbol
trigger_time
trigger_timeframe
closed_bar_id
data_state
structure_state
regime_state
eligibility_state
signal
score
confidence
probability (N/A until calibrated)
risk_proposal
system_mode
strategy_version
configuration_version
```

## Frontend invariants
- never fabricate data;
- never infer a missing value as zero/safe;
- never call the Python bridge directly;
- never write persistence records directly;
- never bypass backend policy;
- never place live orders.
