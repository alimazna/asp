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

## DEC-013 — Concrete persistence store
PER defines only the persistence contract. The runtime implementation is a
dependency-free, file-backed `FilePersistenceStore` (durable at the record
boundary, append-only streams, idempotent re-puts). No external database is
introduced.

## DEC-014 — Guardian construction
The concrete Guardian type is private to `src/guardian/Guardian.cpp`. The single
construction point is `makeGuardian()` declared in `IGuardian.h`, so callers
depend only on the interface and cannot widen Guardian authority.

## DEC-015 — API wire contract typing
`ApiField` values that are already JSON-encoded (booleans, numbers, nested
objects/arrays) must be marked `raw`. Emitting them unquoted is required for a
valid wire contract; the frontend must never receive `"true"`/`"1"` strings in
place of real JSON types.

## DEC-016 — Bridge exec-failure visibility
A supervised child that fails before `exec` must be reported as a failed launch
(`ERROR`), not `ONLINE`. On POSIX this is detected with a close-on-exec
self-pipe; on Windows a failed `CreateProcess` is already reported directly.


## DEC-017 — Live decision pipeline composition
The runtime composition root runs the decision chain once per new closed bar of
the primary operational timeframe (M15), inside `DecisionPipeline`
(`src/runtime/DecisionPipeline.{h,cpp}`). The chain is the live analogue of the
deterministic replay engine and uses the same engines, so live and replay
decisions are structurally identical. The decision is recorded in the ledger and
durable store *before* any shadow command is issued. Overlapping bridge windows
are deduplicated per timeframe so a closed bar is never published twice. Macro
context is UNKNOWN by default (no calendar feed) and is carried through as
explicit uncertainty rather than fabricated as clear. Live execution remains
impossible by construction.

## DEC-018 — Persistence numeric round-trip precision
Position and outcome doubles persisted through `PersistenceEngine` are encoded
with 17 significant digits (`std::setprecision(17)`) so a write/read round-trip
is bit-faithful and reconciliation cannot report a spurious FIELD_MISMATCH.
