# DECISIONS.md

## DEC-001 — Technical identity
AURA is the technical project identity. ASTRA is the product/desktop brand.

## DEC-002 — Market-data acquisition
Candle acquisition for the new rebuild uses Python + the official MetaTrader5 package through the user's installed MT5 Terminal.

## DEC-003 — No MQL5 candle-ingestion path
MQL5/EA adapters are not the active candle-ingestion implementation for the new build. Historical MQL5 material is retained only as archive/reference.

## DEC-004 — Bundled bridge
The Python MT5 bridge is shipped as part of the application distribution and managed by AURA. The end user should not have to run Python manually.

## DEC-005 — Separate process
The bridge is a separate child process, not Python code embedded into the C++ executable process.

## DEC-006 — Localhost boundary
The C++ runtime communicates with the bridge over a loopback-only JSON API. The bridge must not listen on public network interfaces.

## DEC-007 — Application startup
The packaged desktop product must work by double-click and must not depend on CMD working directory, PATH setup, or manual bridge startup.

## DEC-008 — Backend-first build
DeepSeek completes the backend first. Backend is reviewed and accepted before Alpha starts the frontend.

## DEC-009 — Visual identity
ASTRA uses the supplied visual reference as the brand source: geometric A-symbol, ASTRA wordmark, dark navy/deep blue, cool gray, silver/white visual language, refined spacing.

## DEC-010 — SHADOW only
No live broker order placement is part of the initial backend build.

## DEC-011 — Timeframe authority
Nine timeframe streams remain canonical. M15 is the primary operational/setup timeframe; H4 is the primary structural authority.

## DEC-012 — Closed-bar causality
Decision-grade candle input is closed-bar only. No lookahead, no future information, and no silent repainting.
