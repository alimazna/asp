# IMPLEMENTATION_SCOPE.md

## Active scope: Backend rebuild

The repository is a clean rebuild. The active implementation target is every backend task in `TASK_MANIFEST.yaml`.

### Included
- foundations and control-plane contracts;
- resilience and graceful degradation;
- bundled Python MetaTrader5 bridge;
- C++ localhost bridge client and data ingestion;
- nine canonical timeframe streams;
- data validation, freshness, health, and recovery;
- timeframe state and deterministic decision chain;
- signal/score/confidence/probability boundary;
- macro/market-quality gates;
- risk proposal and portfolio risk;
- shadow execution, simulated positions, outcomes, reconciliation;
- persistence and audit;
- self-learning/research/evolution/validation/governance backend;
- operating window, checkpoint, recovery, resource budgets;
- Telegram auxiliary backend layer;
- Windows process/path/bundling/launcher support;
- stable backend API for the future ASTRA frontend;
- deterministic tests, replay fixtures, and integration tests/mocks.

### Explicitly deferred
- ASTRA desktop UI implementation;
- final visual layout/screens;
- UI animations and frontend styling;
- live trading/order placement;
- broker promotion claims;
- profitability claims.

### Market-data architecture change
The old MQL5-adapter data path is replaced for this rebuild by:

```text
MT5 Terminal -> bundled Python MetaTrader5 bridge -> loopback API -> C++ AURA ingestion
```

The old MQL5 design is preserved only in `docs/archive/` for traceability and is not an active implementation target.
