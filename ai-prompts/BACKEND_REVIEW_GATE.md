# BACKEND REVIEW GATE — Before handing AURA to Alpha

Review the completed backend against:
- `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`
- `docs/architecture/BACKEND_DONE_DEFINITION.md`
- `docs/architecture/MT5_PYTHON_BRIDGE_V1.md`
- `docs/architecture/BACKEND_FRONTEND_API_V1.md`
- `project-control/TASK_MANIFEST.yaml`

Do not modify implementation unless a separate repair task is explicitly assigned.

Return an evidence-only audit with:

| Bug | Evidence | Severity | Exact file-line | Fix required |
|---|---|---|---|---|

Check especially:
1. Python bridge really replaces candle acquisition as the active path.
2. No MQL5 EA dependency remains for market-data ingestion.
3. Python bridge is bundled/managed by the application.
4. The application can start without CMD/developer working directory assumptions.
5. Loopback API is loopback-only.
6. Closed-bar/no-lookahead rules are enforced.
7. Data-quality and stale-data gates are wired into capability/decision gating.
8. M15/H4 authority is preserved.
9. Shadow remains the only execution mode.
10. Persistence, idempotency, provenance, and recovery are auditable.
11. Frontend can consume a stable backend contract without changing backend internals.
