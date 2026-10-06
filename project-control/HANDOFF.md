# HANDOFF.md

## Backend -> Frontend handoff contract

Frontend handoff is allowed only after:
1. all required backend tasks are implemented or explicitly documented as non-blocking accepted exceptions;
2. backend review gate is PASS;
3. Windows startup test confirms no CMD requirement;
4. bridge handshake and market-data integration tests have evidence;
5. `BACKEND_FRONTEND_API_V1.md` is frozen for the frontend release;
6. project state is updated to `BACKEND_APPROVED_FOR_FRONTEND`.

## Alpha must receive
- the approved repository state;
- `docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md`;
- `docs/architecture/BACKEND_FRONTEND_API_V1.md`;
- `docs/brand/ASTRA_VISUAL_IDENTITY.md`;
- `docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg`;
- the final backend test report and known limitations.

## Alpha must not
- change backend contracts without a new architecture decision;
- add market-data providers;
- implement live order execution;
- bypass the bridge/data validation boundary;
- use the frontend as a source of truth.
