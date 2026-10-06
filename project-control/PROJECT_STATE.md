# PROJECT_STATE.md

```yaml
project: AURA
product: ASTRA
asset: XAUUSD
mode: SHADOW
backend_agent: DeepSeek v4.1 Flash / OpenHands
frontend_agent: Alpha / deferred
architecture_status: DEFINED_V4
backend_status: READY_TO_BUILD
frontend_status: DEFERRED
market_data_path: Python_MetaTrader5_Bridge
bridge_distribution: BUNDLED_WITH_APP
bridge_process: SEPARATE_CHILD_PROCESS
bridge_transport: LOOPBACK_JSON_API
startup_requirement: DOUBLE_CLICK_NO_CMD
live_automation: NOT_INITIAL_OBJECTIVE
profitability: UNPROVEN
probability_calibration: NOT_ESTABLISHED
broker_validation: REQUIRED
```

## Current checkpoint
No backend source implementation has been accepted yet. The task graph is ready for execution.

## Completion ownership
- DeepSeek: implement backend.
- Review gate: verify backend.
- Alpha: implement frontend only after review approval.
