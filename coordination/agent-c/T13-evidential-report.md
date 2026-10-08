# T13 — Evidential real-data path (Agent-C) — 2026-10-08

**Status: BLOCKED — NOT a PASS.** The real-data pipeline is wired and reproducible,
but it stops at a backend defect before any decision-grade data flows. Reported,
not silently fixed (my zone is `bridge/`/`tests`; the defect is in frozen
production `src/foundation/`).

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

## 4. Defect D1 (BLOCKER) — JSON numbers parsed as strings

**File:** `src/foundation/Json.cpp`, `Parser::parseNumber` (line ~241).
**Zone:** foundation / production `src/` — NOT Agent-C. Reported, not fixed.

```cpp
out = JsonValue(text.substr(start, pos - start));   // <-- overloads to
                                                    //     JsonValue(std::string)
```

`JsonValue(std::string)` sets `type_ = Type::String` (Json.h:25), so every parsed
number is typed as a **string**. `asDouble`/`asInt64` return the fallback when
`type_ != Type::Number`:

```cpp
double JsonValue::asDouble(double fallback) const noexcept {
    if (type_ != Type::Number) return fallback;   // -> 0.0 for every field
```

**Proof (zUnit-style probe against the real parser):**

```
$ g++ -std=c++17 -I src /tmp/jsontest.cpp src/foundation/Json.cpp -o /tmp/jsontest && /tmp/jsontest
open isNumber=0 isString=1 asDouble=-1
time asInt64=-1
```

**Consequence:** `PythonBridgeClient` reads every candle
(`PythonBridgeClient.cpp:168-175`), every `timeframe`, `price`, and numeric field
as **0** → `DataValidator` rejects all bars `NON_POSITIVE_PRICE` (a downstream
symptom, not the root cause). This is latent only because all committed fixtures
were generated from `mock_api.py`, and every writer (`dumpInto`) goes through
`asString()` — so the bug is invisible on the synthetic path and on all current
tests. It surfaces the moment a real bridge payload is parsed.

**Suggested fix (foundation owner):** set the Number type when constructing from a
numeric literal, e.g. a `JsonValue::number(std::string)` factory used by
`parseNumber`. I applied this as a **temporary local patch only** to prove the
chain; it is reverted — `src/` is byte-identical to `main`.

**Verified-after-fix (temporary patch, then reverted):** 8/9 timeframes VALID+FRESH
(all but M1), host mode **SHADOW**, `analysis/latest` carries real context
(`regime=QUIET`, `h4_bias=DOWN`, `m15_trigger=SHORT`, score 48.85), RULE C intact
(probability null, `score_is_probability` false). Harness 94/96 — the 2 remaining
failures are findings D2/D3 below, not ingestion.

**M1/AEST clock:** the corpus's newest bar (10:30) runs ~2h20 ahead of wall clock;
after uniform time-alignment all timeframes ingest. Without alignment the host
honestly rejects the still-forming bars `FUTURE_DATED` (correct behavior).

## 5. Finding D2 — T17 freeze vs real data: `analysis/latest.levels`

With real data the frozen `level` fields are populated (entry 4123.94, SL
4134.081, TP 4107.038, RR 1.667, risk 0.342%) while `contract_checker` still
enforces `FROZEN_NULL_LEVELS` → 5 "non-null (frozen null this release)"
violations. The T17 freeze was written for the no-decision synthetic path; a
realized proposal fills those fields. **Ruling needed (Lead/T17 owner):** relax the
frozen-null invariant to "null unless a live proposal exists", or freeze
`levels` differently. Not fixed without the ruling.

## 6. Finding D3 — contract drift: `risk/latest.proposal_reason`

Schema `data_required` for `GET /api/v1/risk/latest` lists `proposal_reason`
(T30 promotion), but the real host omits it when a proposal
exists (`proposal_available:true` + populated `proposal`). The mock emits it only
in the no-proposal posture. Open question: is `proposal_reason` required only when
`proposal` is null, or always? If conditional, `data_required` needs a
conditional/vacuous rule (mirroring the T30 array-element vacuous logic).
Not fixed — schema is Lead-owned.

## 7. Reproduction

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

## 8. Audit status (Agent-D, 2026-10-08 09:45 UTC)

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

## 9. Limits / honesty

- Result is **not** a PASS. It is a real-data pipeline that reproduces a blocker.
- Corpus window is 3.5 months; no OOS partition is claimed here (that is T27's
  concern). No tuning performed.
- The feed's time-alignment is a uniform translation used only to present the
  corpus as a trailing window to a live-clock host; it does not create, reorder, or
  forward-date any bar, and closed-bar causality is preserved.

<!-- AI agent (OpenHands/agent-c) on behalf of the operator -->
