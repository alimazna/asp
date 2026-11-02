# Agent D — Security review report (backend-hardening round)

Branch `harden/agent-d`, base commit `80dbd24`. Worktree `/workspace/wt/agent-d`.
SHADOW ONLY: no order execution, no deployment, no push, no live LLM7 call
(`ASTRA_LLM_PROVIDER=mock` throughout).

## Files changed

| Path | Change |
|---|---|
| `tests/AyzrSecurityTests.cpp` | **new** — 19-case in-process security suite (real `AstraApiServer` over real loopback sockets) |
| `docs/ayzr/backend-hardening/SECURITY_FINDINGS.md` | **new** — findings F-D-1…F-D-8 with severity, reproduction, fix and regression test |
| `docs/ayzr/SECURITY.md` | **append-only** — Agent D addendum (findings summary + regression-test pointer) |

No source file under `src/` was edited: every fix identified belongs to a file
owned by another agent (see findings). `astra-frontend/`, `ayzr-android/` and the
Windows sender EXE were not touched.

## Commands run

```bash
# build
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j4
#   -> EXIT 0; curl 8.14.1 and SQLite3 3.46.1 found

# new suite (default: secure expectations only)
cd build-linux && ASTRA_LLM_PROVIDER=mock ./AyzrSecurityTests
# new suite (enforce open findings -> regression tests for F-D-1/2/3/8 must fail)
cd build-linux && AYZR_SECURITY_ENFORCE_FIXES=1 ASTRA_LLM_PROVIDER=mock ./AyzrSecurityTests

# full suite
cd build-linux && for t in *Tests; do ./"$t"; done   # ASTRA_LLM_PROVIDER=mock exported
```

Ad-hoc verification probes (Python `http.client`/`socket`, not committed):

- auth matrix across all 8 protected routes × {none, sender, admin, client};
- raw-header variants (`Bearer `, `Bearer`, `bearer x`, `Basic x`, leading/
  trailing spaces, prefix/suffix);
- unset admin/client token; no sender token (process start refusal);
- oversized `Content-Length`; CORS `Origin` allow-list; `OPTIONS` preflight;
- 30-attempt wrong-token sweeps on the write and read surfaces;
- 700 half-open connections + `ls /proc/<pid>/task`;
- 40 slowloris connections dribbling headers for 25 s;
- whitespace-varied rate-limit eviction at `ASTRA_INGEST_RATE_PER_MIN=2`;
- read-only data directory to force a store write failure;
- sentinel-token log scan (`LOGSENTINEL-S/-A/-C/-L`).

## Exact test results

New suite `AyzrSecurityTests` (19 cases):

```
19 test case(s), 0 failed, 0 assertion failure(s)
RESULT: PASS
```

All 19 cases pass: `sec_sender_credential_authorises_only_ingest_and_sync`,
`sec_client_credential_cannot_write_or_reach_admin`,
`sec_admin_credential_cannot_write_or_read_private`,
`sec_missing_credentials_fail_closed_on_every_protected_route`,
`sec_empty_and_malformed_authorization_fail_closed`,
`sec_admin_surface_fails_closed_when_admin_token_unset`,
`sec_private_surface_fails_closed_when_client_token_unset`,
`sec_credentials_are_not_interchangeable`,
`sec_unknown_path_and_wrong_method_do_not_reach_handlers`,
`sec_oversized_body_is_refused_before_the_handler`,
`sec_rate_limit_refuses_before_the_handler`,
`sec_disallowed_origin_is_refused_before_the_handler`,
`sec_error_bodies_are_flat_and_leak_no_internals`,
`sec_presented_credential_is_never_reflected`,
`sec_malformed_framing_is_400_and_leaks_nothing`,
`sec_gap_rate_limit_key_is_the_raw_header_not_the_credential` (gated),
`sec_gap_failed_authentication_is_not_throttled` (gated),
`sec_gap_store_failure_echoes_raw_sqlite_error` (gated leak asserts),
`sec_gap_admin_route_method_probe_precedes_auth` (gated).

Gated run (`AYZR_SECURITY_ENFORCE_FIXES=1`) — the regression tests for the open
findings correctly detect them:

```
19 test case(s), 4 failed, 5 assertion failure(s)
RESULT: FAIL
[FAIL] sec_gap_rate_limit_key_is_the_raw_header_not_the_credential
[FAIL] sec_gap_failed_authentication_is_not_throttled
[FAIL] sec_gap_store_failure_echoes_raw_sqlite_error
[FAIL] sec_gap_admin_route_method_probe_precedes_auth
```

Full suite — **31/31 binaries passed, 0 failed** (`PASSED=31 FAILED=0`).
30 binaries report a numeric total (273 test cases, 0 assertion failures);
`SingleInstanceTests` uses a different harness and reports
`Single-instance tests: all passed` (10 `[PASS]` lines). Per binary:

```
AnalysisApiTests 10 | AnalyticalFeatureBoundsTests 8 | AnalyticalFeatureLeakageTests 8
AnalyticalFeatureRuleATests 7 | AnalyticalFeatureTests 9 | AstraApiServerTests 9
AyzrFailureInjectionTests 18 | AyzrSecurityTests 19 | BackendApiContractTests 11
BridgeRecoveryTests 3 | CandleStoreTests 5 | CandlesApiTests 7
ClosedBarCausalityTests 6 | DataQualityPropagationTests 4 | DecisionIdentityTests 4
DecisionPipelineIntegrationTests 7 | FrontendContractD1D9Tests 13 | IngestContractTests 2
JsonParserTests 4 | LoopbackConcurrencyTests 1 | NewsApiIntegrationTests 6
NewsIntelligenceAgentCTests 34 | NewsIntelligenceTests 32 | PackageSmokeTests 6
PersistenceRestartTests 8 | ProbabilityApiTests 18 | PythonBridgeContractTests 4
ShadowLifecycleTests 4 | SingleInstanceTests (all passed) | StartupEarlyBindTests 1
StartupWithoutCmdTests 5
```

No pre-existing test regressed.

## Findings summary

Verified-and-holding controls (no action): complete cross-role denial with the
handler never reached; fail-closed on missing/empty/malformed/prefix credentials;
unset token list disables its surface; no-sender-token refuses startup; 413/429/
403 enforced before the handler; flat error bodies with a no-leak assertion;
credential never reflected; no credential in logs; no hardcoded secrets; all SQL
parameterised.

Open findings (all reported, none patched — the fix belongs to a source file
owned by another agent):

| ID | Severity | Summary | Owner file |
|----|----------|---------|-----------|
| F-D-1 | MEDIUM | Rate-limit key is the raw `Authorization` header → whitespace variants of one credential evade the limit. | `src/api/AstraApiServer.cpp` |
| F-D-2 | MEDIUM | Failed authentication is not throttled → unbounded token brute force. | `src/api/AstraApiServer.cpp` |
| F-D-5 | MEDIUM | One detached thread per connection, no cap, no whole-header deadline → dribbled headers park threads (700 conns → 700 threads, unauthenticated). | `src/api/AstraApiServer.cpp` |
| F-D-8 | LOW-MED | Store **write** failure echoes raw `sqlite3_errmsg` into the 500 body. | `src/api/AstraServerMain.cpp` + stores |
| F-D-3 | LOW | Admin route returns 405 before authenticating → route/method disclosure. | `src/api/AstraApiServer.cpp` |
| F-D-6 | LOW | No startup guard that the three credential values differ. | `src/api/AstraServerMain.cpp` |
| F-D-4 | LOW | Token comparison is not constant-time (carried from F-P-1). | `src/api/AstraApiServer.cpp` |
| F-D-7 | INFO | `--sender-token ""` treated as configured; env path refuses startup. | `src/api/AstraServerMain.cpp` |

## Unresolved issues

1. **Fixes not implemented.** All eight findings require edits to
   `src/api/AstraApiServer.{h,cpp}` and `src/api/AstraServerMain.cpp`, which are
   outside Agent D's ownership (Agent D owns only the test file and the two docs).
   Reported with exact file, line context, reproduction and suggested fix.
2. **F-D-5, F-D-6, F-D-7 have no always-on regression test.** A 700-connection
   exhaustion probe would itself be a load test; F-D-6/F-D-7 are startup
   refusals in a non-owned file. Their reproductions are recorded in
   `SECURITY_FINDINGS.md`.
3. **F-D-1/F-D-2/F-D-3/F-D-8 regression tests are gated** behind
   `AYZR_SECURITY_ENFORCE_FIXES=1`, because they assert behaviour that is
   currently insecure. They fail (as intended) when enforced; they must be
   un-gated as each fix lands.
4. **Timing analysis of F-D-4 was not performed.** The constant-time claim is
   reasoned from the `==` comparison, not measured; no timing harness was built.
5. **Load behaviour of F-D-5 was measured only in the harness environment**
   (loopback, 700 connections, single 4-core box). The production memory/FD
   ceiling was not measured.
6. `GET /api/v1/candles` (and the rest of the frozen v1 read surface) is
   unauthenticated by design; this is the existing contract, not a finding, and
   was deliberately not changed.

## Data honesty

No data was written, relabelled or deleted. The real XAUUSD M1 export was not
used in this review (no analysis was run); nothing here invents a price, candle,
news item, source, indicator, analysis result, trade level or probability.
