# AYZR — Testing

Owner: Agent E (Testing & Security). Scope: the C++17 backend in `src/` and the
AYZR-specific additions. `astra-frontend/` and `windows-sender/` are out of scope
for this document.

This document records how the backend is built and exercised, the exact totals
observed on the verification date, and the failure-injection surface that proves
the data-integrity and authentication guards actually hold.

## Build

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j
```

Requirements: a C++17 compiler, CMake ≥ 3.16, Ninja (or Make), `libsqlite3-dev`.
`libcurl` is optional: when present the LLM7 outbound client is compiled
(`-DASTRA_HAVE_CURL=1`); when absent the news pipeline degrades to a deterministic
`unavailable` state instead of failing to build.

Test discovery is automatic: every `tests/**/*.cpp` becomes its own executable
and its own CTest case (`CMakeLists.txt`, `GLOB_RECURSE AURA_TEST_SOURCES`). A
new `TEST_CASE(...)` needs no build-graph edit.

## Running the suite

```bash
cd build-linux && ctest --output-on-failure          # 30 CTest cases
# or, per binary, for the harness totals:
cd build-linux && for t in *Tests; do ./"$t"; done
```

Each C++ test binary prints one `[PASS]`/`[FAIL]` line per case and a summary
line (`N test case(s), F failed, A assertion failure(s)`). A non-zero exit means
at least one case failed. `SingleInstanceTests` uses its own printer
(`SingleInstance tests: all passed`) and reports 10 assertions.

### Python suites

```bash
python3 -m unittest discover -s tests/models -p "test_*.py"   # 308 tests
for f in tests/integration/test_*.py; do python3 "$f"; done
```

`tests/models/` is a `unittest` suite. `tests/integration/*.py` are standalone
scripts that print their own `N/M checks passed`; two of them
(`test_contract_t16.py`, `test_e2e_real_host_t13.py`) SKIP unless a `build/`
directory with `aura_backend_host` exists.

## Totals (verified 2026-10-27, AYZR working tree)

| Suite | Result |
|-------|--------|
| CTest cases | **40/40 passed, 0 failed** |
| C++ harness test cases (40 binaries) | 0 failed (per-binary `RESULT: PASS`) |
| `SingleInstanceTests` assertions | 10/10 (own printer) |
| `tests/models` (unittest) | **308 passed, 0 failed** |
| Flutter (AYZR Android) | **86 passed, 0 failed** (`flutter test`) |
| Flutter analyze | **0 issues** (`flutter analyze`) |

Two AYZR suites were added or extended in this pass and are included in the
40/40 above:

- `GeminiProviderTests` (new, 9 cases) — locks the Gemini 429 refinement: a
  body carrying an exhaustion marker is `quota_exhausted`, a bare rate limit
  stays `rate_limited`, `RetryInfo.retryDelay` / `Retry-After` are parsed, and
  the credential never appears in the URL.
- `FailoverLlmProviderTests` (extended, +6 health cases) — the whole-chain
  verdict: one exhausted provider is `degraded`, all exhausted is
  `all_quota_exhausted`, all auth-failure is `all_unavailable` (and is **not**
  mislabelled as quota), a transient failure stays available, and an empty
  chain reports `no_provider`.
- `AyzrApiContractTests` (extended, +1 case) — `GET /api/v1/news/quota` still
  carries every pre-existing key and now also the chain-health keys.

### End-to-end (real server, local HTTPS stub)

The provider chain was exercised over real HTTP against the running
`astra_server` with all three providers pointed at a TLS stub:

- stub returns `429 insufficient_quota` → `chain_summary:"all_quota_exhausted"`,
  all rows `quota_exhausted:true`, `all_providers_quota_exhausted:true`.
- stub returns `401` → `chain_summary:"all_unavailable"`,
  `category:"auth_failure"`, all rows `last_outcome:"auth_failure"`.

The transport is HTTPS-only by design, so the stub is served over TLS with its
CA trusted by the host. The three-provider chain, `/quota` verdicts and the
`/analyze` `unavailable` path were all read back from the live server.

Additional suites at the same revision: `tests/integration/*.py` — 6 scripts
PASS, 2 SKIP (no `build/`), 1 pre-existing FAIL (see Findings); mock-sender E2E
(`tools/ayzr_agents/mock_sender.py`) — PASS (ingest=2, duplicates=2, rejected=1,
sync=1).

Per-binary C++ harness counts at the same revision:

```
AnalysisApiTests 10            IngestContractTests 2
AnalyticalFeatureBoundsTests 8 JsonParserTests 4
AnalyticalFeatureLeakageTests 8 LoopbackConcurrencyTests 1
AnalyticalFeatureRuleATests 7  NewsApiIntegrationTests 6
AnalyticalFeatureTests 9       NewsIntelligenceAgentCTests 34
AstraApiServerTests 9          NewsIntelligenceTests 32
AyzrFailureInjectionTests 18   PackageSmokeTests 6
BackendApiContractTests 11     PersistenceRestartTests 8
BridgeRecoveryTests 3          ProbabilityApiTests 18
CandleStoreTests 5             PythonBridgeContractTests 4
CandlesApiTests 7              ShadowLifecycleTests 4
ClosedBarCausalityTests 6      StartupEarlyBindTests 1
DataQualityPropagationTests 4  StartupWithoutCmdTests 5
DecisionIdentityTests 4        SingleInstanceTests (10 assertions)
DecisionPipelineIntegrationTests 7
FrontendContractD1D9Tests 13
```

## Failure-injection and security suite

`tests/AyzrFailureInjectionTests.cpp` (18 cases, Agent E) drives the **real**
`astra_server` binary over real loopback HTTP against a real SQLite file. The
only test double is a stalling TCP listener that injects an API timeout at the
network boundary, so the real outbound client, provider classification and retry
policy all execute.

| Case | Injects / asserts |
|------|-------------------|
| `ayzr_candle_ingest_requires_sender_credential_only` | Candle write accepts only the sender token; admin/client/no token → 401 |
| `ayzr_admin_surface_rejects_sender_and_client_credentials` | Destructive admin route rejects sender + client tokens; admin token reaches purge and inspection |
| `ayzr_news_read_surface_requires_client_credential` | News/history/analyze require the client token; sender + admin → 401 |
| `ayzr_news_ingest_write_requires_sender_credential` | News ingest is a write: client (app-embedded) token → 401, sender token → 200 |
| `ayzr_news_surface_disabled_without_client_token` | No configured client token disables the routes (401), never unauthenticated |
| `ayzr_rate_limit_returns_429_with_retry_after` | Per-credential fixed-window limit on read and write; 429 + `Retry-After` |
| `ayzr_error_responses_use_flat_contract_without_leaks` | 404/405/403 are flat `{error,code,message}`, no internal detail |
| `ayzr_duplicate_candle_batch_is_idempotent` | Replayed batch stored once, then `already_stored`; no second row |
| `ayzr_invalid_ohlc_is_rejected_and_never_stored` | high<low, non-positive price, missing OHLC, negative volume → per-candle `rejected`; store stays empty (503) |
| `ayzr_invalid_and_misaligned_timestamps_are_rejected` | Non-positive time, future time, off-grid open time → `rejected` |
| `ayzr_empty_batch_empty_feed_and_bad_timeframe` | Empty batch is a no-op; empty feed is 503 not a fake empty chart; `M7` → 400; missing array/identity → 400 |
| `ayzr_malformed_json_is_400_and_leaks_nothing` | Malformed JSON / wrong shapes → 400 with stable code and no leak |
| `ayzr_malformed_http_framing_is_bounded_and_400` | Missing header terminator, truncated body, bad target → 400; socket read timeout bounds a stalled client |
| `ayzr_candle_store_open_failure_refuses_to_start` | Unopenable candle store is fatal (non-zero exit, no bound port) |
| `ayzr_news_store_failure_is_500_without_leaking_internals` | News store failure → 500 `store_failure`; reads degrade to honest empty, never fabricated |
| `ayzr_llm_network_interruption_yields_unavailable` | Unreachable provider → `unavailable`, `no_trade`, null levels, no credential echoed |
| `ayzr_llm_api_timeout_yields_unavailable_within_bound` | Provider that never answers → bounded read timeout, honest `unavailable`, bounded wall-clock |
| `ayzr_stale_and_conflicting_evidence_are_not_decision_grade` | STALE/UNKNOWN not decision-grade; conflicting evidence stays `no_trade` with both sides and no levels |

The suite has teeth: mutating the news-ingest auth check to also accept the
client token made `ayzr_news_ingest_write_requires_sender_credential` FAIL
(1 failed, 1 assertion). The mutation was reverted and the file verified
byte-identical to its pre-mutation state.

## Data-integrity guards (context)

Two independent layers withhold non-MT5 M15 data (`AGENTS.md`):

- **Ingest** rejects any intraday candle whose `open_time` is not on its
  timeframe grid. Covered by `IngestContractTests` and
  `ayzr_invalid_and_misaligned_timestamps_are_rejected`.
- **Reads** quarantine a whole `(symbol, timeframe)` series when any row is
  misaligned. Covered by `CandleStoreTests`.

The broker clock offset (`clock_offset_seconds`) is clamped to |offset| ≤ 18h on
both the sender and the server. `IngestContractTests` proves an implausible
offset is ignored rather than trusted.

## Findings

### F-E-1 — Stale-feed masking when staleness ≤ the clock-offset clamp (PRE-EXISTING, unresolved)

`bridge/mt5_python/mt5_client.py::_capture_clock_offset` derives the broker
offset from the newest **forming M1 bar** and accepts it when
`|offset| ≤ _MAX_CLOCK_OFFSET_SECONDS` (18h). Because that forming bar is the
*same* stale bar the freshness check is meant to judge, a feed that is stale by
up to 18 hours yields an "offset" equal to the staleness, which is then
subtracted from the age. The result is a stale feed reporting `freshness: FRESH`,
`quality: VALID` and `age_seconds ≈ one bar interval`.

Reproduced with the committed `tests/integration/test_bridge_t06.py` harness
(`FAKE_MT5_LAST_TIME = now − N`):

| Injected staleness | Reported offset | Reported freshness | `quality` |
|---|---|---|---|
| 0–17 h | −N (equals the staleness) | `FRESH` | `VALID` |
| 19 h, 24 h | 0 (clamped) | feed rejected `MARKET_DATA_STALE` | `UNKNOWN` |

Consequence: `test_bridge_t06.py::scenario_stale` (10 × M15 = 2.5 h stale) fails
— the feed is accepted as FRESH instead of rejected `MARKET_DATA_STALE`
(24/25 checks). The failure is in the committed repository state, independent of
the AYZR working tree (`bridge/` has no local modifications).

Severity: this is exactly the class of failure the data-honesty rules forbid
(serving a stale reading as a real, decision-grade one). It is **not** exploited
by the AYZR Android read path (which calls the backend API, not the bridge), but
it affects the desktop backend that reads the bridge.

Suggested resolution (not applied — outside Agent E's write scope, needs a
decision): bound the clock-offset search to a plausible timezone band (≤ ~15 h)
**and** cross-check the offset against a second, independent clock reference
(e.g. the newest closed bar across a longer timeframe, or an absolute broker
time), so a stale feed cannot define its own offset. Do not widen the clamp.

### F-E-2 — A live server on a fixed port poisons `PythonBridgeContractTests` (environmental)

`PythonBridgeContractTests::real_bridge_handshake_and_error_contract` starts the
Python bridge on the hard-coded port **8799**. When any other process already
listens on 8799, the bridge fails to bind, the handshake succeeds against the
wrong service, and 8 assertions fail. Observed once during this session while a
concurrent process held 8799; the test passes when the port is free.

Resolution for future work (not applied): allocate an ephemeral port, as the
newer `CandlesApiTests` and `AyzrFailureInjectionTests` cases already do. Not a
product defect — a test-isolation weakness.
