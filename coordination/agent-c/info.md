# Agent-C - Important Info
> Living document. Updated, not appended.

## Role
Backend & Live Integration. Owns the MT5 bridge, Python bundling, Windows packaging, and the probability API. Keeps production protected and live trading disabled.

## Owned files
- src/api/
- bridge/
- packaging/
- coordination/agent-c/

## Forbidden files
- The baseline (base9 / baseold) - READ-ONLY.
- src/foundation/ (frozen contracts), src/models/, src/analysis/features/.
- coordination/<other-agent>/ folders.

## Features / deliverables
- T06 MT5 bridge hardening — **DONE** (Agent-D PASS 25/25 + 12/12). Staleness
  (MARKET_DATA_STALE), INSUFFICIENT_HISTORY, bootstrap-error surfacing.
- T07 Python bundling — **DONE** (Agent-D PASS 17/17 → 18/18 after scope
  assertion). packaging/bundle_manifest.json, scripts/bundle.py, parity test.
- T08 Windows packaging — HELD (Lead). T09 Probability API — **REVIEW** (Agent-D).
- T13 End-to-end integration — IDLE, gated on T01–T09.

## Owned files (src/api/)
- ProbabilityApi.h / ProbabilityApi.cpp (T09) — probability surface, RULE C gate.
- BackendFacade.h / BackendFacade.cpp (T09 additive) — route /api/v1/probability/latest.
- tests/ProbabilityApiTests.cpp (T09) — 10 cases.

## Key findings
- Bridge verified empirically on 127.0.0.1:8791 (no real MT5/broker here).
- T07 is layout parity + stager, NOT a runtime-complete bundle: no interpreter
  binary is staged under resources/python/ (binary payload, out of repo scope).
  Asserted in test_bundling_t07.py so the limit cannot silently drift.
- Contradictions reported, not fixed (all touch protected/declared files):
  C-1 PackagingConfig.h pythonRelativePath vs PathResolver resources/python.
  C-2 pandas>=2.0 required but never imported.
  C-3 numpy pin drift (>=1.24 / >=1.23 / >=1.24 across three files).
  Escalated to @human by the Lead; not mine to resolve.

## Open questions
- C-1/C-2/C-3 reconciliation decision (human). T08/T09 blocked on it.
- Real MT5 candle retrieval still unexercised (no broker terminal in container).

## Useful commands
(empty)
