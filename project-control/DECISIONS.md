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

## DEC-019 — Frontend transport is loopback-only HTTP/JSON
The ASTRA frontend reaches the backend through a loopback-only HTTP/JSON
server, `aura::LoopbackApiServer` (`src/api/LoopbackApiServer.{h,cpp}`),
binding `127.0.0.1` on port `8790` (the Python bridge uses `8791`). The server
exposes exactly the `BackendFacade` route table, owns no state, and reaches no
runtime internals. A non-loopback bind is refused. This is the approved "local
application API / approved transport" of `BACKEND_FRONTEND_API_V1.md` and the
V4 local-boundary model. The host starts and stops the server; the operator
never launches it manually. Read routes are GET-only; commands use
`POST /api/v1/command` with the same allow-listed, actor-attributed,
policy-checked contract as the in-process facade.

## DEC-020 — Frontend-contract D-gaps closed in the backend
The D1–D9 discrepancies recorded by the frontend integration review are closed
in the backend: freshness/last-successful-update/capability-impact (D3),
decision symbol/time/states/versions (D4), per-decision risk proposal (D5),
append-only audit stream (D6), research/failure/governance history (D7), shadow
exit detail (D8), and bridge/broker identity (D9) are all emitted. Values that
are genuinely not yet known are emitted as explicit `null`/`UNKNOWN` and are
never fabricated. This preserves the "UNKNOWN is not SAFE / STALE is not FRESH"
invariant on the wire.

## DEC-021 — Parsed JSON numbers are Number-typed (D1 foundation fix)
`JsonValue` keeps numbers as **text** to preserve integer precision (see Json.h),
and this is correct. The defect is only in the parser: `Parser::parseNumber`
built the value through `JsonValue(std::string)`, so every parsed number came
back `Type::String` and `asDouble`/`asInt64` returned their fallback. The fix
adds a private `JsonValue::number(std::string)` factory (sets `Type::Number`
while retaining the text) and uses it in `parseNumber`. Scope: **bugfix only** —
no type semantics change, no wire-contract retyping, no serializer change. This
is the C++ side of DEC-013 (parsed numbers must be usable as numbers); it is
required for the real-data end-to-end path (T13 evidential) and for any consumer
that reads a numeric field back from a parsed envelope. Owner: Agent-C.

## DEC-022 — `levels` invariant is conditional on an available proposal (D2)
`levels` (analysis) is **conditional**: present and `null` when no live decision
proposal exists, present and populated when a proposal is available. The T17
freeze encoded only the no-decision posture, and the T23 checker
(`FROZEN_NULL_LEVELS`) enforced it unconditionally, which rejects the real-data
host when a proposal exists. Ruling: preserve the frozen-null
**default/no-decision** meaning (additive-only: a decision-less payload is
unchanged) and make the check two-sided — null-by-default, populated when a
proposal is available. This mirrors the T30 array-element vacuous rule. Owner:
Agent-B (D2 checker + T23 teeth, two-sided).

## DEC-023 — `risk/latest.proposal_reason` required in both postures (D3)
`risk/latest.data_required` lists `proposal_reason`, but the real host emitted it
only in the no-proposal posture. Ruling: emit `proposal_reason` in **both**
postures — a string when a proposal exists, explicit `null` when not — so the
requirement holds unconditionally. Additive only. Owner: Agent-C.
