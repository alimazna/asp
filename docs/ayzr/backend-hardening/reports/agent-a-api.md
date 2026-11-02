# Agent A — Backend architecture & API reliability

Branch `harden/agent-a`, base commit `80dbd24`, worktree `/workspace/wt/agent-a`.
Scope: `src/api/NewsApi.*`, `src/api/AstraApiServer.*` (news + client-token
routes only), `src/api/AstraServerMain.cpp` (news wiring only),
`tests/NewsApiIntegrationTests.cpp`, `tests/AyzrApiContractTests.cpp` (new).

Shadow-only system: no order execution, no live LLM7 calls (all runs use
`ASTRA_LLM_PROVIDER=mock`), no deploy, no push, no data deletion.

---

## 1. Files changed (exact paths)

| File | Change |
|------|--------|
| `src/api/NewsApi.cpp` | Data-honesty fix: `published_at` / `published_at_iso` and `candle_as_of` emit JSON `null` when the value is unknown (0), instead of `0` / `1970-01-01T00:00:00Z`. |
| `tests/AyzrApiContractTests.cpp` | **New.** 14 cases pinning status codes, the flat error-envelope shape, the success-envelope keys, the ingest-ack vocabularies and the two data-honesty guards. |

No other file was modified. `git status --short` shows exactly these two paths.
`CandleStore`, the decision pipeline, `BackendFacade`, `BackendApiSchema` and the
frozen public contract were **not** touched.

---

## 2. Commands run

```bash
# build
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j4

# C++ tests (all binaries)
cd build-linux && for t in *Tests; do ./"$t" || echo "FAIL $t"; done

# the new suite in isolation
cd build-linux && ./AyzrApiContractTests

# live contract probes (mock provider, loopback, throwaway data dir)
ASTRA_LLM_PROVIDER=mock ASTRA_CLIENT_TOKEN=client-tok ./astra_server \
  --host 127.0.0.1 --port 18891 --data-dir /tmp/probe2 \
  --sender-token sender-tok --admin-token admin-tok
curl -s ... http://127.0.0.1:18891/api/v1/news  # etc.
```

`cmake --build` exited 0 with no compiler diagnostics from the changed/new
files. A clean build reports 37 pre-existing `-Wmissing-field-initializers`
warnings in `AnalysisApi.cpp` / `BackendFacade.cpp` (aggregate `ApiResponse`
initializers that omit the `headers` member) — present at the base commit, in
files outside my scope, and unchanged by this work.

---

## 3. Exact test results

Full suite, final run on the working tree (31 test binaries, one per `tests/*.cpp`):

```
AnalysisApiTests                     10 cases, 0 failed   PASS
AnalyticalFeatureBoundsTests          8 cases, 0 failed   PASS
AnalyticalFeatureLeakageTests         8 cases, 0 failed   PASS
AnalyticalFeatureRuleATests           7 cases, 0 failed   PASS
AnalyticalFeatureTests                9 cases, 0 failed   PASS
AstraApiServerTests                   9 cases, 0 failed   PASS
AyzrApiContractTests                 14 cases, 0 failed   PASS   (new)
AyzrFailureInjectionTests            18 cases, 0 failed   PASS
BackendApiContractTests              11 cases, 0 failed   PASS
BridgeRecoveryTests                   3 cases, 0 failed   PASS
CandleStoreTests                      5 cases, 0 failed   PASS
CandlesApiTests                       7 cases, 0 failed   PASS
ClosedBarCausalityTests               6 cases, 0 failed   PASS
DataQualityPropagationTests           4 cases, 0 failed   PASS
DecisionIdentityTests                 4 cases, 0 failed   PASS
DecisionPipelineIntegrationTests      7 cases, 0 failed   PASS
FrontendContractD1D9Tests            13 cases, 0 failed   PASS
IngestContractTests                   2 cases, 0 failed   PASS
JsonParserTests                       4 cases, 0 failed   PASS
LoopbackConcurrencyTests              1 case,  0 failed   PASS
NewsApiIntegrationTests               6 cases, 0 failed   PASS
NewsIntelligenceAgentCTests          34 cases, 0 failed   PASS
NewsIntelligenceTests                32 cases, 0 failed   PASS
PackageSmokeTests                     6 cases, 0 failed   PASS
PersistenceRestartTests               8 cases, 0 failed   PASS
ProbabilityApiTests                  18 cases, 0 failed   PASS
PythonBridgeContractTests             4 cases, 0 failed   PASS
ShadowLifecycleTests                  4 cases, 0 failed   PASS
StartupEarlyBindTests                 1 case,  0 failed   PASS
StartupWithoutCmdTests                5 cases, 0 failed   PASS
```

**Totals: 31 test binaries, all exit 0; 30 harness binaries → 268 cases,
0 failed, 0 assertion failures.** `SingleInstanceTests` (the 31st binary) uses a
different harness and prints its own `Single-instance tests: all passed`; it is
not part of the 268 case count.

Batch stability: the full `for t in *Tests` loop was run 7 times on the changed
tree; 6 passes were clean. In one pass `AyzrFailureInjectionTests` failed on
`ayzr_malformed_http_framing_is_bounded_and_400` (4 assertions) — a raw-socket
framing case that does not touch `NewsApi` or the news routes. It passed in
isolation 3/3 and in all 6 other passes. I classify it as a pre-existing
timing-sensitive flake in Agent E's suite, **not** a regression from this work;
it is listed under unresolved issues rather than claimed fixed.

### Mutation proof the new guards have teeth

Reverting only the `NewsApi.cpp` fix (restoring `intField` for both fields)
made exactly the two intended cases fail:

```
[FAIL] contract_missing_publish_time_is_null_not_epoch_zero
[FAIL] contract_analysis_without_candle_evidence_reports_null_version
14 test case(s), 2 failed, 6 assertion failure(s)   RESULT: FAIL
```

Restoring the fix returns the suite to 14/14 PASS.

---

## 4. API contract audit — findings

Method: read `NewsApi.*`, `AstraApiServer.{h,cpp}`, the news wiring in
`AstraServerMain.cpp`, `BackendApiSchema.*`, `BackendFacade.cpp`, the Android
client (`ayzr-android/lib/**`) and the MT5 sender contract doc; then drove a real
`astra_server` (mock provider) over loopback HTTP.

### 4.1 Auth boundaries — correct, and now pinned

Three independent credential lists, compared by exact string equality after
`Bearer ` stripping:

- **sender** (`ASTRA_SENDER_TOKEN`): `POST /candles/ingest`, `GET /sync/latest`,
  `POST /news/ingest`.
- **admin** (`ASTRA_ADMIN_TOKEN`): `GET|DELETE /admin/candles` only.
- **client** (`ASTRA_CLIENT_TOKEN`): `GET /news`, `/news/analyze`, `/history`.

Verified on the live server and pinned in the new tests: a client token cannot
write candles or news (401); a sender token cannot read the private news surface
(401); sender and client tokens cannot reach the admin surface (401). With
`ASTRA_CLIENT_TOKEN` unset the private routes fail closed (401), not open. No
finding here — the boundary is sound.

### 4.2 Error envelope — consistent

Every non-2xx is the flat object `{"error":"true","code":...,"message":...}`
(never the `{api,schema,data}` success envelope). `error` is the **string**
`"true"`, not a JSON boolean — the Android client's `_errorMessage` and the
frozen schema both rely on that. Pinned in
`contract_error_envelope_shape_is_flat_and_stable`.

### 4.3 Status codes — consistent with the documented contract

400 (`malformed_request` / `malformed_json` / `missing_id` / `missing_articles`
/ `missing_fields`), 401 `unauthorized`, 403 `origin_not_allowed`, 404
`not_found`, 405 `method_not_allowed`, 413 `request_too_large` /
`batch_too_large`, 429 `rate_limited` (with `Retry-After: 60`), 500
`store_failure`, 503 `dependency_unavailable`. All pinned.

### 4.4 Data-honesty finding — **fixed** (this was the one real defect)

`NewsApi` emitted a missing timestamp as `0`, which `isoUtcSeconds(0)` renders
as `"1970-01-01T00:00:00Z"`. Observed live: a sender article without
`published_at` returned

```json
"published_at":0,"published_at_iso":"1970-01-01T00:00:00Z"
```

and the Android detail screen renders `published_at_iso` directly
(`Fmt.isoToLocal`), so the app would show a fabricated 1970 date. The same class
of bug existed for `candle_as_of` (an analysis that observed no candle reported
a data version of epoch 0). Both now emit JSON `null`, matching the convention
the rest of the backend already uses (e.g. `AnalysisApi` emits
`"timestamp":null` and an `AnalysisApiTests` case asserts the body never
contains `1970-01-01T00:00:00Z`).

This is a backward-compatible change for honest readers: `null` is what the
Android model already accepts (`publishedAtUtc: _i(...)` → `null`;
`publishedAtIso` only when the value `is String`; `candleAsOfUtc: _i(...)` →
`null`). It is a wire change for anyone who depended on the fabricated `0`, so
it is called out here explicitly.

### 4.5 Cross-component finding — **reported, not changed** (out of my scope)

The backend wraps list payloads in a named key:

- `GET /api/v1/news` → `data: {"news": [...]}`
- `GET /api/v1/history` → `data: {"history": [...]}`

but the Android client (`ayzr-android/lib/data/api_client.dart`) expects `data`
to be a **bare array**:

```dart
final r = await _getData('/api/v1/news', ...);          // returns decoded['data']
return r.map((d) => (d as List) ...);                   // casts data to List
```

The client's own tests never catch this because they stub the HTTP layer with
bare arrays (`enveloped(newsList['news'])` in `widget_test.dart`), so the real
backend shape is never exercised. Result: against the real server, `news()` and
`history()` would fail the `d as List` cast and surface as a `server` failure
(never as data). `news/analyze` is unaffected — its `data` is an object.

I did **not** change this: the fix could go either way (unwrap the named key in
the backend, or read `data['news']` in the client), the client lives in Agent B's
scope (`ayzr-android/**`, which I am instructed not to modify), and changing the
backend shape would itself be a contract change needing a decision. I recorded
the backend's current shape in the new pin with a NOTE comment and flag it here
for the principal to arbitrate.

### 4.6 Other observations (no change made)

- **Timeouts.** `SO_RCVTIMEO` is applied to each accepted connection
  (`readTimeoutMillis`, default 10 s); a truncated body that never arrives ends
  the read loop and is handled (400), not left open. Correct.
- **Request size.** `Content-Length` over `maxRequestBytes` (1 MiB) → 413. The
  documented env var `ASTRA_MAX_REQUEST_BYTES` is **not** wired in
  `AstraServerMain` (only the default is used). Not a defect; a doc/wiring gap
  noted for completeness.
- **Rate limiting.** Per-credential (FNV-1a of the header, token never stored),
  per route group, `Retry-After: 60` on 429. Verified.
- **`candles` route default.** `GET /candles` defaults `tf=M15` and ignores
  `symbol`; a missing timeframe on the facade route is a 400. Consistent with
  `AGENTS.md`.
- **Error hygiene.** No error body contained a path, a credential or an
  implementation detail (covered by Agent E's `checkNoInternalLeak`; the new
  suite additionally asserts errors are never enveloped).

---

## 5. New test file — what it pins

`tests/AyzrApiContractTests.cpp`, 14 cases:

1. `contract_news_read_routes_require_client_credential` — 401 for
   no/sender/admin/arbitrary token on all three read routes; 200 for the client.
2. `contract_write_routes_reject_client_and_admin_credentials` — client/admin
   refused on candle ingest, news ingest and sync; sender accepted.
3. `contract_admin_surface_requires_admin_credential`.
4. `contract_news_routes_fail_closed_without_client_token`.
5. `contract_error_envelope_shape_is_flat_and_stable` — 404/405/400 shapes.
6. `contract_news_route_validation_status_codes` — 400 `missing_id`,
   404 `not_found`, 400 `missing_articles`, 400 `missing_fields`, 405s.
7. `contract_rate_limit_returns_429_with_retry_after` — status + raw
   `Retry-After` header.
8. `contract_success_envelope_keys_are_stable` — `{api,schema,data}` on every
   route.
9. `contract_candle_ingest_ack_shape_is_stable` — MT5 sender counters + state
   vocabulary + idempotent replay + scoped sync watermark.
10. `contract_news_ingest_ack_shape_is_stable`.
11. `contract_news_list_and_detail_shape_is_stable` — every key the Android
    models read (with the 4.5 divergence noted).
12. `contract_missing_publish_time_is_null_not_epoch_zero`.
13. `contract_analysis_without_candle_evidence_reports_null_version`.
14. `contract_cors_preflight_and_origin_allowlist` — 204 preflight, 403 for a
    disallowed origin, reflection for an allowed one.

It reuses the established pattern: spawn the real `astra_server`
(`ProcessSupervisor`) with `ASTRA_LLM_PROVIDER=mock`, wait for
`/api/v1/health`, drive it over loopback HTTP with the shared `HttpClient`, and
skip cleanly when the binary is not built. The raw-socket header probe is
POSIX-only and skips on Windows.

---

## 6. Unresolved issues / not claimed as done

1. **`data.news` / `data.history` vs the client's bare-array expectation
   (§4.5)** — real integration break; needs a decision and a client or backend
   change. Reported, not fixed (out of my file scope).
2. **`ASTRA_MAX_REQUEST_BYTES` is documented but not read by
   `AstraServerMain`** — the 1 MiB default applies; the env var has no effect.
   Wiring it is a one-line change but touches server config outside the news
   routes, so I left it and recorded it.
3. **One transient failure of `ayzr_malformed_http_framing_is_bounded_and_400`**
   in a single early full-suite run (not reproducible in isolation or in 4 later
   batches). Pre-existing timing sensitivity in Agent E's raw-socket test;
   untouched by this work.
4. **No live LLM7 verification** — deliberately avoided to protect the daily
   quota; all analysis paths here run on the mock provider.
5. **`candle_as_of` semantics** — the news handler still sets
   `candle_as_of` from `store.latestOpenTimeAnySource("XAUUSD","M15")`; when no
   M15 candle exists it is 0 and now serializes as `null`. Whether the analysis
   should instead record the newest candle across *all* timeframes is a product
   decision I did not make.

No claim of production deployment, push, or data change: none was performed.
