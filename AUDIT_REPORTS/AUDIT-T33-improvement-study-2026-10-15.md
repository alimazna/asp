# AUDIT-T33 — Improvement Study and Recommendations

Date: 2026-10-15
Scope: deployed ASTRA stack (Railway backend `astra-server` @ `3fa9763`,
Cloudflare Pages `astra-dia.pages.dev`, `windows-sender/`)
Method: read the actual source, tests, CI and deployment config; ran the live
endpoints. Every finding below cites the file or endpoint it came from.

This is a study, not a change set. No behaviour was modified for this report.

---

## 0. What is already solid (do not rebuild)

- **Data-honesty is enforced in three independent layers.** Ingest rejects
  off-grid candles (`AstraRuntime::parseCandle` → `openTimeAligned`); reads
  quarantine any contaminated `(symbol, timeframe)` series
  (`CandleStore::recent/syncPoints/syncPointsForSource/latestOpenTimeAnySource`);
  the frontend refuses non-aligned batches (`astra-frontend/assets/lib.js`
  `candleBatchIntegrity`). Genuine data passes all three.
- **Idempotent ingestion.** `candles` has
  `PRIMARY KEY (source_id, symbol, timeframe, open_time)` and WAL
  (`src/persistence/CandleStore.cpp`). A replay returns `already_stored` and no
  second logical row. Verified end-to-end against the real binary.
- **Auth boundary is correct.** Ingestion and sync require a bearer token;
  unauthenticated calls return `401`. CORS returns `204` for
  `https://astra-dia.pages.dev` and `403` for any other origin. The frontend
  bundle contains only a public HTTPS base URL.
- **No trading surface.** No order-execution route exists anywhere in
  `src/api`. SHADOW-only is intact.

---

## 1. Highest-impact finding: the live decision path is single-timeframe

**Evidence.** `AuraRuntime::runDecisionCycle` builds history from one
timeframe only:

```
dataBus_.recent(closedBar.timeframe, pipeline_->config().historyBars);
```

`DecisionPipeline::onClosedBar` then computes features from that one history,
and `DecisionPipelineConfig::operationalTimeframe` defaults to `M15`. The nine
frame engines (`FeatureEngine`, `StructureEngine`, … `ProbabilityEngine`) are
all present and linked in the deployed binary (verified with `nm`), but they
consume a single-timeframe snapshot.

Meanwhile the full nine-timeframe engine
(`AnalyticalFeatureEngine::computeAll`, which builds `perTimeframe` for all nine
and a `cross` block with `m15Available`/`h4Available`/`h4D1Agreement`/
`h4StructuralAuthority`) is used **only** by the offline research dumper
`src/analysis/features/emit_feature_set.cpp` (`aura_feature_dump`).

**Consequence.** The multi-timeframe analysis the frontend's "Analysis" view
describes (nine frames, H4 = structural authority, M15 = trigger) is not what
the live decision uses. The live decision is M15-only. The `/timeframes` view
reports all nine, which makes the product look broader than the decision
actually is.

**Recommendation (highest value).**
Wire the cross-timeframe feature set into the live path, keeping the frozen
contract:

1. In `AuraRuntime`, maintain a per-timeframe closed-bar map in the data bus
   (the ingestion already stores every timeframe; `recent()` is available).
2. On an operational-timeframe (M15) close, call
   `AnalyticalFeatureEngine::computeAll(byTimeframe, asOf)` and feed its `cross`
   block (H4 authority, D1 agreement, MTF conflict share) into the existing
   gate `EligibilityEngine` / `RegimeEngine`.
3. Keep `cross.m15Available && cross.h4Available` as the hard core gate — it is
   already the honest blocker ("core streams incomplete").
4. Add a regression test asserting a decision is *blocked* when H4 is absent
   even though M15 is present, and *allowed* only when both are valid.

Effort: medium. Risk: medium (touches the decision path — gate it behind the
existing `DEGRADED` semantics so a missing frame never fabricates a value).

---

## 2. Business logic quality: opportunity generation and success rate

Two distinct quantities are exposed and both are honest today:

- **Probability** (`/analysis/latest` → `signal.probability`) — pre-trade
  estimate, explicitly flagged `probability_calibrated true/false`. The raw
  `score` is exposed additively and labelled "NOT a probability" when
  uncalibrated. Good.
- **Win rate** (`/shadow/outcomes`) — computed from `OutcomeEngine` records
  (`realized_r`, `bars_held`, `outcome_class`). Empty → "no decided outcome
  yet — not 0%". Good.

**Gaps / suggestions.**

1. **Calibration provenance is not surfaced.** `probability/latest` returns
   `available:false` with a reason, but when it *is* available there is no field
   naming the calibration method or sample size. Add
   `calibration_method`, `calibration_samples`, `calibration_window` to the
   probability payload so a consumer can judge whether the number is earned.
2. **No confidence interval discipline on the frontend.** `confidence_lo/hi`
   exist in the API but are rendered as raw numbers. Recommend rendering the
   probability *with* its interval and refusing to show a point estimate when
   the interval is wide.
3. **Outcome labelling.** `outcome_class` (win/loss/breakeven) should carry the
   cost model (spread/commission) it was computed under; otherwise "win rate" is
   ambiguous. Add `cost_model_version` to each outcome.
4. **Opportunity scarcity is not explained.** When no plan is produced the
   reason is generic ("no directional signal"). Recommend distinguishing: (a)
   data missing/blocked, (b) macro gate suppressed, (c) eligibility rejected,
   (d) score below threshold, (e) already in position. `DecisionPipeline` already
   pushes per-stage `issues` — expose them.

---

## 3. Observability and readiness

**Evidence.** `/api/v1/health` returns
`{"aggregate":"STARTING","decision_grade_data":false,...}` and `railway.toml`
sets `healthcheckPath = "/api/v1/health"`. There is no separate readiness
endpoint (`grep readiness src/` → only a Windows startup comment).

**Risk.** `/health`'s `aggregate` reflects *data* state, not *process* liveness.
A correctly running server with no MT5 data reports `STARTING`/`DEGRADED`, which
conflates "the process is up" with "the data is fresh". If Railway ever keys
restarts off this, a healthy-but-dataless service looks unhealthy.

**Recommendation.**
- Add `GET /api/v1/readyz` → `200 {"ready":true}` whenever the HTTP server is
  serving (liveness), independent of data.
- Keep `/health` as the *data* view but rename the semantic in docs: `aggregate`
  is data readiness, `process` is liveness.
- Add a `version`/`commit` field to `/health` so the running revision is visible
  without `railway status`.

---

## 4. Ingestion hardening

**Evidence.** Body size is capped (`413 request_too_large`,
`AstraApiServer.cpp`), CORS and bearer auth are enforced, but `429` exists only
as a status string — no rate limiter is implemented.

**Recommendation.**
- Per-token rate limit (token bucket) on `/candles/ingest` and `/sync/latest`.
  A leaked/rotated token currently allows unbounded writes.
- Cap candles-per-batch (the sender batches; enforce a max on the server too),
  currently only total body size is bounded.
- Log rejected candles with a stable reason code and the source id (never the
  token) so ingestion failures are auditable.

---

## 5. Persistence and scale

**Evidence.** SQLite + WAL on the Railway volume `/opt/astra/data`. Single
writer. Schema is minimal and correct for one service.

**Recommendation.**
- Document the migration trigger: if more than one replica is ever needed, WAL
  on a shared volume is not safe across instances — move to Postgres then. Until
  then SQLite is the right free-tier choice.
- Add a startup integrity check (`PRAGMA integrity_check`) and a visible
  `store_path` / `store_writable` in `/health` so a bad volume mount fails loud.
- Purge/quarantine tooling: the one hybrid M15 row (`1792068300`) is withheld by
  the read guard but still occupies storage. Provide an authenticated admin
  route to *delete* a specific quarantined row after review (currently there is
  no safe way to remove it). Do not auto-delete.

---

## 6. Windows sender

**Evidence.** `windows-sender/astra_sender/` has `collector`, `engine`, `gui`,
`ledger`, `protocol`, `config`; 20 tests pass. Reconcile-from-server
(`engine.py:178`) and gap detection (`engine.py:288`, `>1800s`) exist.

**Recommendation.**
- **Signed releases.** The `AstraSender.exe` release asset is unsigned; Windows
  SmartScreen will warn on an unknown publisher. Add a code-signing step (even a
  self-signed cert documented for the owner) or ship a hash + instructions.
- **First-run backfill UX.** Startup defaults to the latest closed candle.
  Surface the explicit backfill mode in the GUI with a clear progress and
  "idempotent, rate-limited" note, so the owner never triggers months of history
  by accident.
- **Token storage.** Confirm the sender token is stored in the OS credential
  store or a restricted file, never in the ledger DB (`ledger.py`). Add a test
  asserting the token never appears in persisted state.
- **Symbol discovery.** Support MT5 suffixes (`XAUUSD.a`, `GOLD`) via explicit
  `symbol_resolve` and show the resolved symbol in the GUI; add a test with a
  suffixed symbol fixture.

---

## 7. Frontend

**Evidence.** Static SPA (`astra-frontend/`), `node build.js` → `dist/`,
secret-scanned, deployed via `scripts/deploy-pages.sh` and a CI workflow
(`deploy-static-pages.yml`, triggered on `deploy/astra-stack` + `astra-frontend/**`).

**Recommendation.**
- **Integration tests.** `tests/lib.test.js` covers pure helpers only. Add a
  small mocked-fetch test per view asserting the *degraded/empty/unavailable*
  render paths, so honesty regressions are caught in CI.
- **No HTTP cache headers on `config.js`.** Ensure `config.js` is served
  `no-store` so a redeploy that changes `BACKEND_BASE` is not cached stale;
  hashed assets can be immutable.
- **Accessibility.** Add `aria-live` on the status/callout regions so a
  degraded/error state is announced, not just coloured.
- **CI secret dependency.** The Pages workflow needs `CLOUDFLARE_API_TOKEN` and
  `CLOUDFLARE_ACCOUNT_ID` repo secrets. If unset, CI silently cannot deploy and
  the manual `deploy-pages.sh` is the only path. Add a preflight job that fails
  loudly when the secrets are absent.

---

## 8. Process / repo hygiene

- `research/` is 478,037 lines (80% of the repo). Recommend confirming it is
  excluded from every deploy path (it is for Railway — the Dockerfile copies
  only `CMakeLists.txt` + `src/`) and consider Git LFS / archive for large
  generated artifacts.
- `railway.toml` triggers a deprecation warning (Config as Code). Migrate to
  `.railway/railway.ts` (`railway config migrate`) when convenient; not urgent.
- Add a documented "data honesty" ADR tying the three layers together, since
  they live in three languages (C++ ingest, C++ store, JS frontend).

---

## 9. Prioritised roadmap

| # | Item | Value | Effort | Risk |
|---|---|---|---|---|
| 1 | Wire 9-frame cross analysis into the live decision path (§1) | High | Med | Med |
| 2 | `/readyz` liveness + `version` in `/health` (§3) | High | Low | Low |
| 3 | Rate limit ingestion/sync; batch cap (§4) | High | Low | Low |
| 4 | Expose decision-block reasons + calibration provenance (§2) | Med | Low | Low |
| 5 | Frontend degraded-path integration tests (§7) | Med | Low | Low |
| 6 | Sender: code signing + token-storage test + symbol suffix test (§6) | Med | Med | Low |
| 7 | Admin route to purge a reviewed quarantined row (§5) | Med | Low | Med |
| 8 | store integrity check in `/health` (§5) | Low | Low | Low |
| 9 | Cache headers + a11y + CI preflight (§7) | Low | Low | Low |

Items 2–5 are the cheapest wins and reduce operational risk without touching the
decision maths. Item 1 is the single change that makes the product match its own
description ("nine-timeframe analysis") and should be done behind tests.

---

## 10. Not in scope / explicitly NOT recommended

- Do not add live order execution. SHADOW-only is a project invariant.
- Do not synthesize or backfill fake candles to make the UI "look alive".
- Do not switch to Postgres while the service is single-replica on the free
  tier; the migration cost has no payoff yet.
- Do not delete the hybrid M15 row silently; withhold + review + explicit purge.
