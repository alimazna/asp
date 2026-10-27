# Agent C — AI News Intelligence — report

Scope: `src/news/NewsAnalysisService.*`, `src/news/NewsPrompt.*`,
`src/news/NewsAnalysisParser.*`, the LLM7 adapter they depend on, and tests for
them. Shadow-only, read-only with respect to ASTRA: the service reads a prompt
context and returns a validated analysis; it never modifies a decision and never
executes a trade. No secrets were hardcoded. `astra-frontend/` and
`windows-sender/` were not read or touched.

## 1. Files created / changed (exact paths)

Created:
- `tests/NewsIntelligenceAgentCTests.cpp` — 34 cases (new dedicated file; see §5
  for why it is separate from `tests/NewsIntelligenceTests.cpp`).

Changed:
- `src/news/NewsAnalysisService.cpp` (md5 `3bd1371ac13c636c565838eb0b2ad873`)
  - **Defect fix (owned file):** the service built its `LlmRequest` with the
    struct default `timeoutMillis = 30000`. `Llm7Provider::complete` prefers a
    non-zero `request.timeoutMillis` over `config_.readTimeoutMillis`
    (`src/news/Llm7Provider.cpp:56`), so the documented
    `LLM7_READ_TIMEOUT_MS=45000` (`.env.example:46`) was silently shadowed and
    every live analysis was capped at 30 s. The service now sets
    `request.timeoutMillis = 0` ("no per-request override"), so the configured
    read timeout applies.
  - Null-provider guard moved ahead of the `provider_->providerId()` /
    `provider_->modelId()` dereferences (dereferencing before the `nullptr`
    check is UB). Note: Agent A concurrently applied the same guard ordering; the
    file is shared and the two edits converged.

Not changed by me (verified only): `src/news/NewsAnalysisParser.cpp`,
`src/news/NewsPrompt.cpp`. Both had already been hardened by Agent A while I was
working — the parser gained `kMaxTextBytes`/`kMaxArrayItems` bounds and the level
reads already used `isNumber()` before `asDouble()`. I pinned that behaviour with
tests rather than editing the files, to avoid conflicting edits.

## 2. Commands run

```bash
# build + targeted runs
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j
build-linux/NewsIntelligenceAgentCTests
build-linux/NewsIntelligenceTests
build-linux/NewsApiIntegrationTests
# full suite (per-binary pass/fail + case totals)
cd build-linux && for t in $(find . -maxdepth 1 -type f -name '*Tests' -executable); do ./"$t"; done

# live LLM7 contract probes (public endpoints; no secret used)
curl -sS https://api.llm7.io/v1/models
curl -sS https://api.llm7.io/openapi.json
curl -sS -X POST https://api.llm7.io/v1/chat/completions -d '{"model":"nemotron-3-nano:30b",...}'
curl -sS -X POST https://api.llm7.io/v1/chat/completions -d '{...,"response_format":{"type":"json_object"}}'

# end-to-end through the real server + real libcurl adapter
ASTRA_LLM_PROVIDER=llm7 LLM7_API_TOKEN=<placeholder> build-linux/astra_server --port 8803 ...
curl -X POST .../api/v1/news/ingest  (sender token)
curl .../api/v1/news/analyze?id=...  (client token)
```

## 3. LLM7 integration — verified against the official surface

Verified live on 2026-10-27 (public endpoints, no credential used):

| Claim | Evidence |
|-------|----------|
| Base URL `https://api.llm7.io/v1` | `POST https://api.llm7.io/v1/chat/completions` → HTTP 200, OpenAI-compatible `chat.completion` |
| Model `nemotron-3-nano:30b` exists | present in `GET /v1/models` (67 models) |
| OpenAI-compatible `/chat/completions` | 200 body has `choices[0].message.content` (+ `reasoning`), which is exactly what the adapter extracts |
| **Model does NOT support JSON mode** | `response_format:{"type":"json_object"}` → **HTTP 400** `unsupported_model_feature` "Model 'nemotron-3-nano:30b' does not support JSON mode." → defensive parsing is mandatory, and the adapter correctly never sends `response_format` |

The adapter's defaults (`Llm7Config::baseUrl`, `::model`), the request body shape,
the bearer header, the absence of any JSON-mode field, and the status→outcome
classification are all pinned by tests (§4). A live end-to-end call through the
real `astra_server` + real libcurl adapter reached `api.llm7.io` and produced a
classified, honest `unavailable` result with `provider_id=llm7`,
`model_id=nemotron-3-nano:30b`, `direction=no_trade` and null levels.

Honest caveat: the anonymous LLM7 quota became exhausted during probing
(`429 ... quota_exceeded`), so I could not capture a *successful* live model
completion on the final attempt. The successful-content path is covered
deterministically at the HTTP boundary (scripted transport) instead. No paid
credential exists in this environment and none was invented.

## 4. Failure-injection coverage (`tests/NewsIntelligenceAgentCTests.cpp`, 34 cases)

All six requested failure classes are covered, plus the LLM7 contract:

- **Auth failure** — 401/403 → `AuthFailure`, not retryable; service returns
  `unavailable` after exactly 1 attempt; missing token fails closed without
  contacting the transport; the error never echoes the credential.
- **Rate limit** — 429 → `RateLimited`, retryable; retried then succeeds.
- **Timeout** — 408/504 → `Timeout`; transport timeout → `TransportError`; the
  service exhausts exactly `maxAttempts` (bounded, no retry storm) and mixed
  retryable failures stay capped.
- **Malformed output** — non-JSON, missing/empty `choices[]`, empty content, and
  a `{ not valid json` model reply → `invalid_model_output`, no recommendation,
  levels dropped.
- **Missing news body** — prompt marks `Body: UNAVAILABLE`; a valid analysis is
  still produced and the prompt sent to the provider flagged the gap.
- **Missing candles** — prompt marks market data `UNAVAILABLE`; no invented data.
- **Conflicting evidence** — supporting/opposing arrays preserved, direction
  `no_trade`, levels dropped, disagreement note kept.
- Read-only contract, parser type-safety (`NaN`, `1e400` overflow), first-brace
  honest-failure, and the timeout-precedence regression are also covered.

## 5. Exact test results

Targeted (run in `build-linux/`):

| Binary | Result |
|--------|--------|
| `NewsIntelligenceAgentCTests` (mine) | **34 test case(s), 0 failed, 0 assertion failure(s) — RESULT: PASS** |
| `NewsIntelligenceTests` | 29 test case(s), 0 failed — PASS |
| `NewsApiIntegrationTests` | 6 test case(s), 0 failed — PASS |

Full suite (after `cmake --build build-linux -j`):

**30 binaries, 248 test cases, 0 failed, 0 binaries failing.**

(The suite grew during my session because Agents A/E added
`tests/AyzrFailureInjectionTests.cpp` and extended `NewsIntelligenceTests.cpp` /
`NewsApiIntegrationTests.cpp` concurrently; my dedicated file avoids collision
with those shared files.)

## 6. Notes / residual risk

- The timeout defect was found by a live end-to-end measurement (30.27 s wall
  time with `LLM7_READ_TIMEOUT_MS=45000`), then fixed and pinned by regression
  tests. A local slow-endpoint experiment could not be used as evidence because
  the outbound client (hardened concurrently by Agent A) now refuses cleartext
  `http://` with `CURLOPT_PROTOCOLS_STR="https"` — a deliberate, good policy.
- The parser takes the *first* balanced `{...}`; prose containing braces before
  the real object yields an honest `invalid_model_output` rather than a
  mis-parse. Pinned by a test.
- No deployment, push, or production-data change was performed. Nothing was
  committed (the working tree is shared and integration is the principal's call).
