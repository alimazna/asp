# AYZR — Shared Team Memory (source of truth)

Persistent, cross-agent record. Every accepted architectural decision lands here.
Secrets never appear here. Private agent notes live in `memory/agents/<id>.md`.

## 0. Project identity & absolute rules

- Repo `alimazna/asp`. Market: **XAUUSD only**. Mode: **SHADOW ONLY** — no order
  execution, ever.
- Operational candle source: **BiQuote**. Historical research: Dukascopy.
- **ASTRA CORE is frozen.** Its math, decision rules, entry/exit conditions,
  Entry/SL/TP, and output semantics must not change. New engines may be built
  *around* it; the Central Orchestrator may organise its presentation but may not
  rewrite its computation.
- Never invent prices/candles/news/indicators/probabilities. Unavailable ⇒
  `N/A`/`Unavailable`, never a plausible default.
- No live LLM calls in tests. `ASTRA_LLM_PROVIDER=mock`.

## 1. ASTRA CORE fingerprint (Phase 0 baseline)

Frozen oracle digest — must stay identical after every phase:

```
oracle_sha256 = aee61fa9225b4fc6a3ba6dd7a2d82640227ec07179cd592e42b93e526889c480
cases = 15
```

Verify with:
```
cd research/ayzr_quant_lab && python3 tests/test_astra_protection.py
```
Baseline result (2026-11-02): `PASS — ASTRA CORE unchanged`.
Baseline C++ suite: **44/44 binaries pass**.

## 2. Repository map (verified)

| Area | Path | Notes |
|---|---|---|
| Decision core | `src/runtime/DecisionPipeline.*`, `src/scoring`, `src/signals`, `src/regime`, `src/structure`, `src/features` | ASTRA CORE. Frozen. |
| AYZR engines | `src/ayzr/HorizonEngine.*`, `TradeLifecycle.*` | SHORT/MEDIUM/LONG wrappers over `SignalEngine`. |
| AYZR JSON | `src/ayzr/AyzrJson.*` | Serialises engines/trades for clients. |
| API | `src/api/AstraApiServer.cpp` | Route table + auth. |
| News/AI | `src/news/**` | LLM7 adapter, parser, mock provider, quota monitor. |
| Store | `src/persistence/CandleStore.*` | SQLite, idempotent on (source,symbol,tf,open). |
| Tests | `tests/*.cpp` | 40 sources → 44 binaries. |
| Quant lab | `research/ayzr_quant_lab/` | ASTRA port, oracle, protection gate. |
| Android | `ayzr-android/` | Flutter, Arabic RTL. |
| Web | `tools/ayzr_observatory/` | Local observatory, loopback-only. |

Excluded: `astra-frontend/`, `frontend/` (old UI), `windows-sender/` (separate EXE).

## 3. Accepted engineering decisions

- **D-01** New ecosystem code lives under `src/ayzr/ecosystem/` and
  `src/ayzr/engines/`, namespace `aura::ayzr::eco`. It never edits ASTRA files.
- **D-02** All engines emit one unified `EngineResult` (see `EngineContract.h`).
  Unknown fields are `null`/`N/A`; no fabricated numbers.
- **D-03** Central Orchestrator never recomputes engine math; it classifies,
  deduplicates, detects conflict, and decides PUBLISH / PUBLISH_WITH_CAUTION /
  WATCH / WITHHOLD. No majority voting.
- **D-04** Session intelligence parses IANA TZif from `/usr/share/zoneinfo`.
  No hard-coded session hours. Gold is OTC: labels are *reference* sessions, not
  a central exchange bell.
- **D-05** Decision Ledger is append-only (SQLite table `ayzr_decision_ledger`),
  one row per event, with correlation id and reason codes.
- **D-06** Confidence/probability is only emitted when a calibration file exists.
  ASTRA `score` is never presented as a probability.

## 4. Interfaces between layers

```
BiQuote → DataIntelligence → MarketIntelligence ─┐
                                                 ├→ Engines → Orchestrator → Ledger
ASTRA CORE (frozen) ─────────────────────────────┘                │
                                                                  ▼
                                                       Android / Web (read-only)
```

## 5. Open issues

- BiQuote field availability (Bid/Ask, spread, tick) — audit in
  `docs/ayzr/reports/biquote-audit.md`; Microstructure engine reports
  `Unavailable` until verified.
- Live LLM7 blocked on free-tier 429 + token rotation (F-E-6 in SECURITY.md).
