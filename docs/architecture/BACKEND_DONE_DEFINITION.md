# BACKEND_DONE_DEFINITION.md

Backend handoff to Alpha is allowed only when all applicable gates below are PASS or an explicit human-approved exception is documented.

## A. Architecture
- [ ] V4 master is implemented as active control reference.
- [ ] Python MetaTrader5 bridge is the active candle acquisition path.
- [ ] MQL5/EA candle acquisition is not required by the runtime.
- [ ] C++ remains the runtime/decision core.
- [ ] M15/H4 authority is preserved.
- [ ] SHADOW remains the only execution mode.

## B. Bridge
- [ ] Bundled Python runtime/dependencies exist in the product layout.
- [ ] AURA can start the bridge automatically.
- [ ] Loopback-only transport is enforced.
- [ ] Handshake/version/schema validation works.
- [ ] XAUUSD symbol resolution works against the target broker naming style.
- [ ] Nine timeframes can be requested.
- [ ] Closed-bar semantics are enforced.
- [ ] Errors are explicit and structured.
- [ ] Health/freshness is observable.

## C. Data integrity
- [ ] OHLC invariants are enforced.
- [ ] Timestamp monotonicity is checked.
- [ ] Duplicates/out-of-order/incomplete/stale states are detected.
- [ ] Quality propagates into gating.
- [ ] No future information can enter decision snapshots.

## D. Runtime/resilience
- [ ] Windows path resolution does not depend on CWD.
- [ ] Double-click startup does not require CMD.
- [ ] Bridge failure degrades only dependent capabilities.
- [ ] Restart/recovery is bounded and auditable.
- [ ] Shutdown is graceful.

## E. Decision/shadow
- [ ] Deterministic IDs are implemented.
- [ ] Score/confidence/probability remain separated.
- [ ] Risk is before shadow execution.
- [ ] Shadow lifecycle is persisted.
- [ ] Reconciliation is implemented.

## F. Persistence/audit
- [ ] Critical state survives restart.
- [ ] Audit is append-only.
- [ ] Configuration/strategy/protocol versions are captured.
- [ ] Idempotency is tested.

## G. Research/evolution/governance
- [ ] Research is sandboxed from trusted evaluator/control plane.
- [ ] Holdout/evaluator boundaries are protected.
- [ ] Human gate exists for promotion.
- [ ] Known-good/rollback concepts are persisted.

## H. Packaging
- [ ] Product package has deterministic relative paths.
- [ ] Missing runtime dependency errors are actionable.
- [ ] No manual Python start is required.
- [ ] No manual CMD start is required.

## I. Evidence
- [ ] Unit tests pass.
- [ ] Integration/mocked bridge tests pass.
- [ ] Windows build evidence exists.
- [ ] Real MT5 integration evidence exists when test environment permits.
- [ ] Test log is complete.
- [ ] Backend review gate is PASS.
