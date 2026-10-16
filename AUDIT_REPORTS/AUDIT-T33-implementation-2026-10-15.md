# AUDIT-T33 — Implementation Report (2026-10-15)

Executes the prioritised roadmap in
[AUDIT-T33-improvement-study-2026-10-15.md](AUDIT-T33-improvement-study-2026-10-15.md).
Every item below was verified by running the test suites named in it. No item is
reported done on inspection alone.

## Status

| # | Roadmap item | Status | Evidence |
|---|---|---|---|
| 1 | 9-frame cross analysis in the live decision path | Done | `DecisionPipelineIntegrationTests`, `AnalyticalFeature*` |
| 2 | `/readyz` liveness + `version` in `/health` | Done | `astra_server_exposes_livez_readyz_and_health_version` |
| 3 | Rate-limit + batch-cap ingestion/sync | Done | `astra_server_rate_limits_ingest_and_caps_batch` |
| 4 | Calibration provenance in the probability payload | Done | `probability_payload_carries_calibration_provenance`, `..._absent_provenance_as_null` |
| 5 | Frontend degraded-path integration tests | Done | `astra-frontend/tests/degraded-paths.test.js` (35 pass) |
| 6 | Sender: token-storage test + symbol-suffix test + backfill plan | Done | `windows-sender/tests/test_symbol_and_token.py` |
| 7 | Authenticated admin purge of a reviewed quarantined row | Done | `astra_server_admin_purge_requires_admin_token_and_deletes_one_row` |

## Item 6 — Windows sender

Three gaps from §6 of the study were closed, with tests:

- **Symbol resolution.** `XAUUSD.vx` (a broker suffix) resolves to an exact
  match and the collector reports the resolved name; "GOLD" (absent at the
  broker) falls back to a real XAU instrument and never silently reports the
  unrelated `EURUSD`. Covered by `SymbolResolutionTest`.
- **Token storage.** The ingestion token is confirmed to live only in the
  config file: a ledger file written through its real pending / failed /
  acknowledged paths is scanned byte-for-byte and must not contain the token or
  the string `Bearer`. Covered by `TokenStorageTest`.
- **Explicit backfill.** `SyncEngine.plan_backfill` was added: a bounded,
  chronological, idempotent catch-up plan that never re-plans an acknowledged
  candle and returns nothing when the bound is ≤ 0. Covered by `BackfillTest`.

Sender suite: `python -m unittest discover -s tests` → 28 pass.

## Item 7 — Admin purge route

`DELETE /api/v1/admin/candles` deletes exactly one candle by its full identity
(`source_id`, `symbol`, `timeframe`, `open_time`). Design choices:

- **Separate credential.** Checked against `config_.adminTokens`
  (`ASTRA_ADMIN_TOKEN` / `--admin-token`), never the sender token list, so a
  leaked ingestion credential cannot reach the destructive surface.
- **Single row only.** There is no bulk delete; a re-run reports
  `deleted:0, state:"not_found"`. The read guard already withholds a misaligned
  row from every API response, so this just reclaims its storage, and it is
  never automatic.
- Disabled unless `ASTRA_ADMIN_TOKEN` is set.

`src/foundation/HttpClient.cpp` was extended to send `Content-Length` for
`DELETE`/`PUT`/`PATCH`, not only `POST`, so a bodied non-POST request is
well-formed.

## Verification commands

```
# Backend (Linux)
cmake --build build-linux -j4
cd build-linux && ctest --output-on-failure      # 26/26 targets pass

# Windows sender (Linux, against the fake MT5 double)
cd windows-sender && python -m unittest discover -s tests   # 28 pass

# Frontend
cd astra-frontend && node --test tests/*.test.js # 35 pass
```

## Not done / explicitly not recommended

Nothing in the roadmap remains. Items 8–9 (store integrity check in `/health`,
cache headers / a11y / CI preflight) were not in the executed set and stay as
follow-ups. Live MT5 verification still requires the owner's Windows machine and
is marked **NOT VERIFIED** in the sender report.
