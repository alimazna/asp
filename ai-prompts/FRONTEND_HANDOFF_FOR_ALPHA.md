# FRONTEND HANDOFF FOR ALPHA — Execute only after backend approval

Project: AURA
Product brand: ASTRA
Asset: XAUUSD only

Backend is considered frozen for frontend integration only after the backend review gate is approved.

## Frontend objective
Build the desktop control center branded **ASTRA** on top of the stable backend API. Do not rewrite backend logic, data ingestion, bridge lifecycle, persistence, risk, research, governance, or shadow execution.

## Visual source
Use `docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg` as the visual reference and `docs/brand/ASTRA_VISUAL_IDENTITY.md` as the design constraints.

The reference establishes:
- geometric triangular A-symbol;
- ASTRA wordmark;
- deep navy/dark surfaces;
- cool light-gray backgrounds;
- restrained silver/gray accents;
- clean aerospace/institutional technology feel;
- refined spacing and minimal visual clutter.

Do not invent a different logo or brand identity.

## Required product behavior
The desktop application must:
- start by double-click;
- not require CMD;
- launch/supervise the backend bridge through the approved backend host contract;
- clearly show STARTING / ONLINE / DEGRADED / OFFLINE / RECOVERING / PAUSED / SHADOW states;
- distinguish system mode from subsystem health;
- never fabricate market data when the bridge is unavailable;
- make data freshness and failed timeframes visible;
- preserve the primary M15 setup / H4 structure relationship;
- show SHADOW decisions as simulated lifecycle events, not live trades.

## Backend boundary
Treat the backend as the source of truth. Consume versioned contracts. Do not reach into backend private classes or filesystem internals.
