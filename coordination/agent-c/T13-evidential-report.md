# T13 — Evidential real-data path (Agent-C) — 2026-10-08

**Status: IN PROGRESS — evidential race at 95/96; single remaining failure is D2
(Agent-B's task).** D1 (blocker) and D3 were fixed under Lead ruling DEC-021/DEC-023.
D2 (frozen-null `levels` vs a realized proposal) is Agent-B's T23 teeth change and
is the only thing standing between this and a full evidential PASS.

Repo: `alimazna/asp` @ `main`. Corpus: `research/data/xauusd_m1/xauusd_m1_real.csv`
(read-only, 100,008 M1 bars, 2026-06-24..2026-10-08). No live trading, no MT5
network, no lookahead (closed bars only).

---

## 1. Objective (Lead directive 2026-10-08 08:12 UTC)

Close T13 with an *evidential* PASS: real gold data flowing through the real
`aura_backend_host`, frozen v1 contract holding, RULE C gate intact — not just
"the mock passes".

## 2. Method (my zone)

1. `bridge/mt5_python/mt5_csv_feed.py` (new, real engine code): replays a canonical
   M1 CSV as an MT5-shaped series. Preserves the closed-bar contract (ascending,
   index 0 = forming, `start_pos=1` = closed only). Higher timeframes aggregated by
   boundary. Optional uniform time-alignment so the corpus ends on the current
   minute (turn a fixed historical window into the trailing window a live terminal
   would serve). Real or replay mode is env-gated; no behavior change by default.
2. `tests/integration/fake_mt5/MetaTrader5.py`: shim dispatches `copy_rates_from_pos`
   /`symbol_info_tick`/`symbols_get` to that feed when `FAKE_MT5_CSV` is set; the
   synthetic path is untouched (T06 suites still pass).
3. `tests/integration/test_e2e_real_host_t13.py`: new opt-in evidential path
   (`T13_REAL_DATA=1`). Stages the shim + feed into `build/resources/...`, starts
   the real host with `FAKE_MT5_CSV`, and checks: bridge reaches the corpus
   (handshake/mt5_ready/resolved XAUUSD), all 9 timeframes observed+closed,
   VALID+FRESH, M15 decision-grade, host mode SHADOW, RULE C (probability null),
   and the frozen v1 contract on every route. Cleans the staged shim afterwards so
   the default no-data posture is restored.

## 3. Result

With the current `main` binary the evidential run is **91/96**: the bridge reaches
the real corpus (handshake OK, mt5_ready, resolved XAUUSD) but no timeframe
ingests — every bar is rejected `NON_POSITIVE_PRICE`, host stays DEGRADED.

## 4. Defect D1 (was BLOCKER) — JSON numbers parsed as strings — **FIXED (DEC-021)**

**File:** `src/foundation/Json.cpp`, `Parser::parseNumber`; factory added to
`src/foundation/Json.h`. Lead-ruled DEC-021 (Agent-C owner, defect fix to Wave-4
code; no contract/state/behaviour change; wire shape byte-unchanged).

The old code built `JsonValue(text.substr(...))`, which overloads to
`JsonValue(std::string)` → `Type::String`, so every parsed number was mistyped and
`asDouble`/`asInt64` returned the fallback (0.0) — real bridge candles read as 0 →
`NON_POSITIVE_PRICE`.

**Fix (minimal, as ruled):** add a private-to-public factory
`JsonValue::number(std::string)` that sets `Type::Number` while keeping the literal
as text (numbers-as-text precision preserved), use it in `parseNumber`. While
verifying, `dump()` exposed a second latent bug in the same narrow scope: the
Number branch called `asString()`, which read the **string** slot (empty for parsed
numbers), so a parsed number re-serialized to nothing. Fixed by having `asString()`
return the number text when the value is a Number (all 22 call sites are on string
fields, so this is safe and makes dump exact/byte-preserving).

**Regression test:** `tests/JsonParserTests.cpp` (new, 4 cases): parsed numbers are
typed Number; `asDouble`/`asInt64` read real values; array elements are Numbers;
parsed numbers round-trip through `dump()`; `asString()` preserves big-integer
text (`9007199254740993`). `ctest` 19/19.

## 5. Finding D3 — `risk/latest.proposal_reason` conditional — **FIXED (DEC-023)**

`src/api/BackendFacade.cpp`: the proposal-present branch now also emits
`proposal_reason` (`risk.reason`; the no-proposal branch already emits the explicit
string). `data_required` now holds unconditionally; additive only.

## 6. Evidential result (after D1+D3)

`T13_REAL_DATA=1` → **95/96**. Real gold data flows through the real host: 9/9
timeframes VALID+FRESH, M15 decision-grade, mode **SHADOW**, real context
(`regime=QUIET`, `h4_bias=DOWN`, `m15_trigger=SHORT`), RULE C intact (probability
null, `score_is_probability` false), and the frozen v1 contract holds on every
route. The **one** remaining failure is D2 (`levels` non-null vs `FROZEN_NULL_LEVELS`)
— Agent-B's T23 teeth change (DEC-022). Once B lands that, the race is 96/96.

## 7. Finding D2 — T17 freeze vs real data: `analysis/latest.levels` — OPEN (Agent-B)

With real data the frozen `level` fields are populated (entry 4123.94, SL
4134.081, TP 4107.038, RR 1.667, risk 0.342%) while `contract_checker` still
enforces `FROZEN_NULL_LEVELS` → 5 "non-null (frozen null this release)"
violations. The T17 freeze was written for the no-decision synthetic path; a
realized proposal fills those fields. **Lead ruling DEC-022:** `levels` stays
present-and-null by default (decision-less payload unchanged), but non-null is
allowed when a proposal exists — `FROZEN_NULL_LEVELS` becomes two-sided, mirrored
on the T30 array-element vacuous rule, with a positive proposal-present test.
**Owner: Agent-B (T23 teeth).** Not Agent-C's file; not changed here.

## 8. Contract drift D3 — `risk/latest.proposal_reason` — **FIXED**

(Resolved by DEC-023 under §5 above; the old conditional-required question is
settled by emitting the field in both postures.)

## 9. Reproduction

The harness is now self-contained: it stages the bridge tree (and, on the
evidential path, the replay shim) into `build/resources/...` from the repo, so it
works in an un-packaged checkout. The default suite stays green; the evidential
suite exposes D1.

```bash
# default (frozen) suite stays green:
python3 tests/integration/test_e2e_real_host_t13.py        # 52/52

# evidential path (exposes D1 -> 91/96 today):
T13_REAL_DATA=1 python3 tests/integration/test_e2e_real_host_t13.py
```

## 10. Audit status (Agent-D, 2026-10-08 09:45 UTC)

`AUDIT_REPORTS/AUDIT-T13-evidential.md`:
- **D1 CONFIRMED**, independently reproduced at source/probe level; "honest and its
  root cause is independently reproduced"; escalation to the foundation owner
  endorsed. `src/` verified pristine at HEAD.
- **D2** carried into **T29 Part 2** (levels policy must be settled before any
  real-data probability/levels publication).
- **D3** resolution: make the host always emit `proposal_reason`, or make it
  conditional with an explicit vacuous rule.
- Agent-D's first evidential attempt hit "connection refused" (un-packaged bundle);
  the self-contained staging above resolves that so the real ingest number can be
  re-audited once D1 is fixed.

## 11. Limits / honesty

- Result is **95/96**, not yet a full PASS: the one open failure is D2 (Agent-B).
  No faked PASS; the failing check is the finding.
- Corpus window is 3.5 months; no OOS partition is claimed here (that is T27's
  concern). No tuning performed.
- The feed's time-alignment is a uniform translation used only to present the
  corpus as a trailing window to a live-clock host; it does not create, reorder, or
  forward-date any bar, and closed-bar causality is preserved.

<!-- AI agent (OpenHands/agent-c) on behalf of the operator -->
