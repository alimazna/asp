# AYZR — Backend & Database (Agent A)

Scope: the C++17 backend and its stores, with a focus on the **news-intelligence
module** (`src/news/**`, the news routes in `src/api/AstraApiServer.*` and
`src/api/NewsApi.*`). AYZR is **shadow-only**: there is no order execution, ever.

This document records (1) an evidence-based audit of what is reused vs newly
written, (2) the news module's contracts, (3) the hardening changes made, (4) the
resolved Cloudflare-database question, and (5) how to build and test.

---

## 1. Audit — reuse vs new

### Reused unchanged (do not rewrite)

| Component | Path | Role |
|---|---|---|
| Candle store | `src/persistence/CandleStore.*` | SQLite candle persistence, grid-alignment quarantine |
| Decision pipeline | `src/runtime/DecisionPipeline.*`, `AuraRuntime.*` | ASTRA decisions (read-only to news) |
| Frozen v1 read surface | `src/api/BackendFacade.*`, `BackendApiSchema.*` | public contract, untouched |
| HTTP server core | `src/api/AstraApiServer.*` | socket loop, CORS, rate limit, auth helpers |
| Loopback client | `src/foundation/HttpClient.*` | local bridge helper (loopback only) |
| Persistence engine | `src/persistence/PersistenceEngine.*` | generic record store |

The news module depends on the above **read-only**. It never mutates a decision
and never writes a candle.

### New (additive)

| File | Purpose |
|---|---|
| `src/news/NewsModels.h` | domain model + dedup/identity helpers |
| `src/news/LlmProvider.h` | provider-neutral `ILlmProvider` contract + outcome classes |
| `src/news/Llm7Provider.{h,cpp}` | OpenAI-compatible chat-completions adapter (libcurl) |
| `src/news/MockLlmProvider.h` | deterministic provider for tests/offline |
| `src/news/NewsPrompt.{h,cpp}` | prompt construction from real data only |
| `src/news/NewsAnalysisParser.{h,cpp}` | defensive parse/validate of model output |
| `src/news/NewsAnalysisService.{h,cpp}` | bounded-retry analysis orchestration |
| `src/news/NewsStore.{h,cpp}` | durable SQLite store (`news_articles`, `news_analyses`) |
| `src/foundation/OutboundHttpClient.{h,cpp}` | outbound HTTPS client (libcurl, optional) |
| `src/api/NewsApi.{h,cpp}` | v1 envelope builders for news/history |
| `tests/NewsIntelligenceTests.cpp` | unit tests (parser, store, provider, service) |
| `tests/NewsApiIntegrationTests.cpp` | end-to-end tests against the real `astra_server` |

News routes are registered in `src/api/AstraApiServer.cpp` and wired in
`src/api/AstraServerMain.cpp`.

---

## 2. News module contracts

### Identity and dedup

- An article is unique by `(source, article_id)`.
- A **story** is unique across sources by `dedup_key = newsDedupKey(headline)`:
  the first accepted article is canonical, later ones are reported
  `duplicate_content` with the canonical id.
- An analysis is unique by `article_id` and is **superseded** on re-analysis
  (`ON CONFLICT ... DO UPDATE`), so history never grows duplicate rows.

`newsDedupKey`: ASCII is lowercased and reduced to alphanumerics; **non-ASCII
(UTF-8, i.e. Arabic) bytes are preserved verbatim** (see §3).

### Analysis status gate

`NewsAnalysis.status` is the only gate on whether a proposal may be shown:

| status | meaning |
|---|---|
| `ok` | a proposal is present and evidence-supported |
| `insufficient_data` | evidence contradictory/missing; no proposal |
| `unavailable` | provider unreachable / not configured |
| `invalid_model_output` | model replied but failed schema validation |

A failure never becomes a recommendation. Levels (`entry`/`stop_loss`/
`take_profit`) are honoured **only** for `bullish`/`bearish` and only when finite
and positive; they are dropped for `no_trade`/`sideways` (they would imply an
order that does not exist). In the API an absent level is JSON `null`, never `0`.

### Provider neutrality

Business logic depends only on `ILlmProvider`. `Llm7Provider` classifies HTTP
status into `LlmOutcome` (`AuthFailure`, `RateLimited`, `Timeout`, `BadRequest`,
`ServerError`, `TransportError`). The service retries **only** retryable
outcomes, bounded by `maxAttempts`, so it cannot produce a retry storm. The API
token is read from the environment, never logged, never serialized, and never
placed in a request body.

### Bounded schema-repair turn (added)

`nemotron-3-nano:30b` has no JSON mode, and it was **verified live** to return
*valid JSON that violates the schema* on the first turn — most often folding the
top-level keys (`direction`, `astra_agreement`, …) inside a `facts` element, so
validation fails with `invalid direction: ''`. When the caller allows more than
one attempt (`maxAttempts >= 2`, the default), the service makes **one**
corrective turn:

1. replay the conversation: `system`, original `user`, the model's own invalid
   reply as an `assistant` turn, then a corrective `user` instruction
   (`buildRepairUserMessage`) carrying the parser's safe reason and a concrete
   example of the exact shape;
2. re-validate the reply with the same parser — the schema is **not** relaxed.

Measured against the real provider: an inline restatement in a single user
message does **not** recover a valid object, while the `assistant` + `user`
turn sequence does. The repair shares the existing attempt budget (it cannot
become a retry storm) and is skipped when `maxAttempts == 1`. If it still
fails, the status stays `invalid_model_output` — never a coerced recommendation.

### Live provider findings (2026-10-27)

- The LLM7 free tier for `nemotron-3-nano:30b` is reachable and returns the
  OpenAI-compatible shape the adapter expects (`choices[0].message.content`).
- The free tier **rate-limits aggressively**: repeated calls within seconds
  return HTTP 429. This is a real operational limit, not a bug — the adapter
  classifies 429 as `RateLimited` (retryable, bounded backoff) and the app keeps
  serving cached news + market data. A paid balance or a lower call rate is
  required for reliable production analysis.
- A real-token probe on 2026-10-27 confirmed `GET /v1/models` returns 67 models
  (including `nemotron-3-nano:30b`) and the token authenticates, but
  `POST /v1/chat/completions` returned **HTTP 429 `insufficient_quota`**
  ("Daily token quota exceeded"), finally with `retry_after: 69686` (~19 h). The
  account has a **$0 balance**; the free daily budget was spent by the session's
  test calls. The API degraded cleanly to `status:"unavailable"` with an Arabic
  `limitations_ar` line — no fabricated summary, facts, or direction.
- The model's first-pass schema adherence is **poor**; the repair turn is
  therefore the primary path to a valid analysis, not an edge case.

### API surface (private, client-token)

| Route | Method | Credential | Response |
|---|---|---|---|
| `/api/v1/news` | GET | client | list, newest first; `?category=`, `?gold_only=`, `?limit=` |
| `/api/v1/news/analyze?id=` | GET | client | article + latest analysis; 400 missing id, 404 unknown id |
| `/api/v1/news/quota` | GET | client | AI provider availability snapshot (secret-free) |
| `/api/v1/history` | GET | client | persisted analyses, newest first; `?limit=` |
| `/api/v1/news/ingest` | POST | **sender** | write path; per-article outcomes |

### Daily-quota monitoring (`GET /api/v1/news/quota`)

LLM7 returns **HTTP 429 for two opposite conditions**, and conflating them is a
real defect: a *temporary* burst limit clears in seconds, while a *spent daily
budget* will not clear until the provider's window resets. Treating the second as
retryable makes the backend hammer a provider that cannot answer; treating the
first as permanent makes the app look broken for a few seconds.

`LlmQuotaMonitor` (`src/news/LlmQuotaMonitor.*`) records each provider outcome
and exposes a secret-free snapshot:

- A **429 whose body carries a quota marker** (`insufficient_quota`,
  `quota_exceeded`, `daily token quota`, `exceeded your current quota`) is
  `quota_exhausted`: **not retryable**, and the service opens a cooldown and
  withholds provider calls until the reset window.
- Any **other 429** is `rate_limited`: still retryable, bounded by the existing
  attempt cap.
- The reset time is parsed from the `Retry-After` header (integer seconds) or the
  body's `retry_after` field. When neither is present the monitor still cools
  down (conservative fallback, `LLM7_QUOTA_FALLBACK_COOLDOWN_S`, default 3600)
  but reports `retry_after_seconds: null` — it **never invents a countdown**.

The response shape (`data.quota`):

```json
{"category":"quota_exhausted","retry_after_seconds":66282,
 "last_change_at":1793122517,"last_success_at":0,
 "cooling_down":true,"cooldown_until":1793188799,
 "quota_exhausted":true,"rate_limited":false}
```

The Android app calls this alongside the news feed and shows an Arabic banner
(`QuotaBanner`) that distinguishes "paused until the quota renews" from
"provider busy, retry shortly". A failed quota read leaves the banner hidden —
an unknown quota is never shown as exhausted.

### Multi-provider failover chain (`ASTRA_LLM_ORDER`)

A single provider is a single point of failure: when its free budget is spent,
every analysis stops. With `ASTRA_LLM_PROVIDER=auto` (the default) the backend
builds an ordered failover chain instead (`FailoverLlmProvider`,
`src/news/FailoverLlmProvider.*`). The order comes from `ASTRA_LLM_ORDER`
(default `gemini,zai,mistral,llm7`); a provider whose credential is empty is
**skipped**, never placed at the head as dead weight. The first provider that
answers wins, and each analysis is attributed to the model that really produced
it.

Pinning `ASTRA_LLM_PROVIDER` to one name (`gemini` | `zai` | `mistral` | `llm7`)
still selects exactly that provider, so an operator can reproduce or isolate a
single one. Pinning never silently becomes a chain — otherwise a test that
points one provider at a dead port would be rescued by the others.

`GET /api/v1/news/quota` reports the whole chain as well as the last result, so
the app can say *which* credential is down and whether analysis still works:

```json
{"category":"quota_exhausted","retry_after_seconds":3600,
 "cooling_down":true,"cooldown_until":1793188799,
 "quota_exhausted":true,"rate_limited":false,
 "chain_summary":"all_quota_exhausted",
 "providers_configured":3,"providers_unavailable":3,
 "providers_quota_exhausted":3,
 "any_provider_available":false,"all_providers_unavailable":true,
 "all_providers_quota_exhausted":true,
 "providers":[
   {"provider":"gemini","last_outcome":"quota_exhausted",
    "unavailable":true,"quota_exhausted":true}]}
```

`chain_summary` is the machine token the app keys its banner on:
`ok` | `degraded` (some providers down, at least one still answers) |
`all_quota_exhausted` | `all_unavailable` | `no_provider`. Only **hard**
outcomes (`auth_failure`, `quota_exhausted`, `bad_request`) count a provider as
down; a timeout, 5xx, transport error or temporary rate limit may clear on the
next call and must not be reported as down. Labels and counts only — never key
material.

The Gemini adapter (native `generateContent` shape) classifies a 429 the same
way as the OpenAI-compatible adapters: a body carrying an exhaustion marker
(`insufficient_quota`, `quota_exceeded`, `exceeded your current quota`, `check
your plan and billing`) is `quota_exhausted`, any other 429 stays `rate_limited`.
The reset is read from a `Retry-After` header or the `google.rpc.RetryInfo`
`retryDelay` field; when neither is present the reset is reported as unknown.

**Live evidence (2026-10-27, local HTTPS stub):** three providers pointed at a
stub returning `429 insufficient_quota` produced
`chain_summary:"all_quota_exhausted"` with all three rows `quota_exhausted:true`;
the same stub returning `401` produced `chain_summary:"all_unavailable"` with
`category:"auth_failure"`. Both verdicts were read back over real HTTP from the
running server. The transport is HTTPS-only, so the stub is served over TLS.

**Live evidence (2026-10-27):** the real token returned
`HTTP 429`, `retry-after: 66401`, and body
`{"error":{"message":"Daily token quota exceeded. Retry after 66401 seconds.","type":"insufficient_quota","param":null,"code":"quota_exceeded","retry_after":66401}}`.
The classifier matched it exactly (test fixture grounded in this probe), and the
end-to-end server returned `status:"unavailable"` with the Arabic pause message
and opened the cooldown; a second request answered in ~25 ms **without** calling
the provider.

**Credential separation is deliberate and tested:**

- The **client token** (`ASTRA_CLIENT_TOKEN`) is read-only and ships inside the
  Android app. It authorizes `GET /news`, `/news/analyze`, `/history` **only**.
- The **sender token** authorizes the news *write* path (`/news/ingest`) and
  candle ingestion. A leaked client token therefore cannot write anything.
- The **admin token** authorizes the purge routes and nothing on the news surface.
- With no `ASTRA_CLIENT_TOKEN` configured the news routes are **disabled (401)**,
  never served unauthenticated.

Every news route is rate-limited and returns the v1 error envelope on rejection.

---

## 3. Hardening changes made (with evidence)

The module already existed and its tests passed. Review found three real defects,
each now fixed and covered by a regression test.

### 3.1 `newsDedupKey` collapsed every Arabic headline to one key — **fixed**

`src/news/NewsStore.cpp` built the dedup key with `std::isalnum`, which is `false`
for every byte `>= 0x80`. Arabic UTF-8 is entirely high bytes, so **every Arabic
headline produced the empty key `""`**. Because `dedup_key` has a UNIQUE index,
the first Arabic article became canonical and every later, *different* Arabic
story was silently dropped as `duplicate_content`. Arabic is the app's primary
language, so this was severe.

Fix: keep non-ASCII bytes verbatim; only case-fold/reduce ASCII. Verified by
`news_dedup_key_preserves_arabic` (unit) and
`news_arabic_stories_are_not_collapsed` (integration: two distinct Arabic stories
now report `stored:2, duplicates:0`).

### 3.2 `NewsAnalysisService::analyze` dereferenced a null provider — **fixed**

`src/news/NewsAnalysisService.cpp` stamped `provider_->providerId()` **before**
the `if (provider_ == nullptr)` guard, so the documented null-provider path
segfaulted. Fix: run the null check first. Covered by
`news_service_null_provider_is_unavailable`.

### 3.3 Unbounded model output could be persisted — **fixed**

`src/news/NewsAnalysisParser.cpp` accepted arbitrarily large strings/arrays from
the model. Added validation bounds (64 KiB per text field, 256 items per array):
an oversized field is **rejected** (`invalid_model_output`), never truncated,
because truncation would silently alter the model's meaning. Covered by
`news_parser_rejects_oversized_fields`.

### 3.4 Empty identity rejected explicitly — **added**

`NewsStore::insertArticles` now rejects an empty `article_id` or empty `dedupKey`
with a named `rejected` result instead of letting the UNIQUE constraint mask it.
Covered by `news_store_rejects_empty_identity`.

No change was made to `CandleStore`, the decision pipeline, or any API contract.

---

## 4. Database — the Cloudflare question, resolved

### 4.1 What the evidence shows

A repository-wide search for Cloudflare database products (D1, Workers KV,
Hyperdrive, R2, Durable Objects) found **no backend reference to any of them**:

```bash
grep -rniE "cloudflare|hyperdrive|\bD1\b|wrangler|workers\.dev|durable object" \
  --include=*.md --include=*.cpp --include=*.h --include=*.toml --include=*.json \
  --include=*.yml --include=*.yaml --include=*.txt --include=*.sh . \
  | grep -v '^./astra-frontend/' | grep -v '^./.git/'
```

Every hit is either (a) the literal timeframe string `D1`, (b) Cloudflare
**Pages** as a browser origin / static host, or (c) the `CLOUDFLARE_API_TOKEN`
used to deploy the (out-of-scope) frontend. There is **no `wrangler.toml`, no
D1 binding, no Hyperdrive config, and no Cloudflare database client** anywhere in
the backend.

### 4.2 What the backend actually uses

- **Store:** SQLite via `sqlite3` (`CandleStore` for candles, `NewsStore` for
  news), WAL journaling + busy timeout, one transaction per batch.
- **Location:** `ASTRA_DATA_DIR` (default `./data`); production is a Railway
  volume mounted at `/opt/astra/data` (`railway.toml` →
  `[deploy.volumes.astra-data]`). Files: `candles.sqlite`, `news.sqlite`.
- **Access:** only the C++ backend touches the database. The Android app reads
  exclusively through the backend API.

Conclusion: **SQLite on the Railway volume is the current and intended store.**
The "Cloudflare database" in the target-architecture description is not backed by
any artifact in this repository.

### 4.3 Decision: do not migrate

Per the rule *"do not assume Cloudflare D1/Workers/Hyperdrive without evidence"*
and *"reuse existing working adapters"*, no migration is performed. Replacing a
working, tested store would be a destructive change with no functional benefit
and would risk the existing data-integrity guarantees.

### 4.4 Non-destructive migration path (if a Cloudflare DB is later mandated)

Ordered least- to most-disruptive; each is **additive behind the existing
interfaces**, never an in-place destructive migration, and only after a verified
backup of the Railway volume:

1. **Keep SQLite as the write store; serve reads through the backend.** No change
   needed — the app already never touches the DB directly.
2. **Cloudflare Hyperdrive + Postgres.** Hyperdrive is a connection pooler, not a
   store; this needs a Postgres instance and a new store adapter.
3. **Cloudflare D1** (SQLite-compatible, HTTP API). Closest match to the current
   schema: implement a `D1Store` behind the same interface as `CandleStore`/
   `NewsStore`, **dual-write** until verified, then cut over behind a feature flag.

A Cloudflare DB cannot be chosen or provisioned without a product decision and
credentials from the owner; none are required for the current deployment.

---

## 5. Build and test

```bash
# configure + build (needs libsqlite3-dev; libcurl optional for LLM7)
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"

# run the news tests directly
cd build-linux
./NewsIntelligenceTests
./NewsApiIntegrationTests

# full suite (no ctest in this image)
for t in *Tests; do ./"$t" || echo "FAIL $t"; done
```

`ASTRA_LLM_PROVIDER=mock` drives the deterministic in-process provider so the
integration tests need no network and no external quota. The LLM7 adapter is
exercised through an injected fake transport at the network boundary.

**Verified totals (this work):** 29 test binaries, 231 test cases, 0 failed.
`NewsIntelligenceTests` 29 cases / 0 failed; `NewsApiIntegrationTests` 6 cases /
0 failed.
