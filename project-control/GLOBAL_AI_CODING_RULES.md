# GLOBAL_AI_CODING_RULES.md

## Non-negotiable rules
1. One task = one prompt = one output path = one source file.
2. Read required dependencies before implementation.
3. Never invent a missing dependency or architecture decision.
4. `BLOCKED` is a valid outcome.
5. Do not modify unrelated files.
6. Preserve deterministic behavior.
7. No future information, no lookahead, no silent repainting.
8. Data before signal; structure before entry; regime before eligibility; signal before risk; risk before shadow execution.
9. Unknown/stale/invalid data must not be treated as safe or fresh.
10. Version configuration, strategy, schema, protocol, and important decisions.
11. Keep an append-only audit/history boundary.
12. Keep the evaluator independent from candidate/research code.
13. SHADOW is the initial execution objective. No live broker orders.
14. Python MT5 bridge is the active market-data acquisition path; do not create an MQL5 EA data path.
15. The Python bridge is bundled and supervised by the app; end users must not need CMD.
16. Loopback-only bridge endpoint; never expose it publicly.
17. The frontend must not become the backend source of truth.
18. Do not claim profitability, calibrated probability, broker validation, or production safety without evidence.

## Engineering quality
Prefer small interfaces, explicit ownership, deterministic serialization, idempotent boundaries, clear error records, and testable pure logic.

## Evidence discipline
A successful compile is not proof of integration. Integration requires compile + link + contract compatibility + dependency direction + state/ownership/idempotency/versioning/fault behavior.
