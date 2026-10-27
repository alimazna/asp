# AYZR — Security

Owner: Agent E (Testing & Security). Scope: the C++17 backend (`src/`), the AYZR
news module, and the new Android app (`ayzr-android/`). `astra-frontend/` and
`windows-sender/` are out of scope.

This is a SHADOW-ONLY market-intelligence system: there is no order execution
path anywhere. The security goals are therefore (a) protect the stored market
data and the analysis surface, (b) never let a read credential write, (c) never
let a client-side (app-embedded) credential reach a privileged surface, and (d)
never leak secrets or internals in a response.

Every claim below is backed by a case in `tests/AyzrFailureInjectionTests.cpp`
or by a command recorded in `docs/ayzr/reports/agent-e-testing.md`.

## Authentication boundaries

There are three **independent** credential lists. They are compared with exact
string equality after `Bearer ` stripping; a token in one list is never accepted
by another route group.

| Credential | Env var | Authorises | Never authorises |
|------------|---------|-----------|------------------|
| Sender (data provider) | `ASTRA_SENDER_TOKEN` | `POST /api/v1/candles/ingest`, `GET /api/v1/sync/latest`, `POST /api/v1/news/ingest` | admin routes, private read routes |
| Admin | `ASTRA_ADMIN_TOKEN` | `GET|DELETE /api/v1/admin/candles` | candle/news writes, private read routes |
| Client (read-only, in the Android app) | `ASTRA_CLIENT_TOKEN` | `GET /api/v1/news`, `/news/analyze`, `/history` | candle writes, news ingest, admin routes |

Guarantees verified end to end (real server, real HTTP):

- **A read credential cannot write.** `client-token` on
  `/api/v1/candles/ingest` and on `/api/v1/news/ingest` → 401.
- **A write credential cannot read the private surface.** `sender-token` on
  `/api/v1/news` and `/api/v1/history` → 401.
- **A leaked sender credential cannot reach the destructive surface.**
  `sender-token` on `GET|DELETE /api/v1/admin/candles` → 401. This is the whole
  point of the separate admin list: a compromised ingestion credential must not
  be able to inspect or purge stored data.
- **Missing configuration fails closed.** With no `ASTRA_CLIENT_TOKEN`, the
  private routes are disabled (401) rather than served unauthenticated. With no
  `ASTRA_SENDER_TOKEN`, the process refuses to start (exit 2).
- **`news/ingest` is a write** and is guarded by the sender credential, not the
  client token — see F-E-3.

Rate limiting is **per credential** (an FNV-1a hash of the presented header is
the key; the token itself is never stored) and per route group. A denied request
returns `429` with `Retry-After: 60`.

## The Android app holds no privileged secret

`ayzr-android/lib/` was reviewed in full:

- `lib/core/app_config.dart` defines the base URL and a **read-only client
  token** only. The token is supplied at runtime (`--dart-define=AYZR_CLIENT_TOKEN`
  or the in-app settings screen) and defaults to empty; there is no compiled-in
  credential.
- `lib/data/api_client.dart` calls only `/api/v1/health/v1`,
  `/api/v1/analysis/latest`, `/api/v1/news`, `/api/v1/news/analyze`,
  `/api/v1/history`. Ingestion, sync and admin routes are absent by
  construction.
- `lib/state/settings_store.dart` persists the token in `shared_preferences` on
  the device and never logs it. `settings_screen.dart` obscures the input field.
- The only token-shaped literals in the tree are test fixtures in
  `ayzr-android/test/` (e.g. `'secret-read-token'`); no production literal
  exists.

The Android app therefore holds only the read-only client credential. It cannot
write candles, publish news, or purge data even if the credential is extracted
from the device — verified server-side by the 401s above.

## Secret handling

- **No hardcoded secrets.** A pattern scan for `api_key`/`secret`/`password`
  assignments of ≥ 12 characters across `src/`, `tests/`, `scripts/`, `*.toml`,
  `*.yaml` found none.
- **Secrets are environment-only.** `ASTRA_SENDER_TOKEN`, `ASTRA_ADMIN_TOKEN`,
  `ASTRA_CLIENT_TOKEN`, `LLM7_API_TOKEN` are read from the environment
  (`AstraServerMain.cpp`). `.env.example` carries only placeholders and states
  that the three ASTRA tokens must differ.
- **Credentials never appear in logs or responses.** A server started with
  `sender-secret`/`admin-secret`/`client-secret` logged zero occurrences of any
  of them. The LLM7 bearer token is sent only as an `Authorization` header
  (`Llm7Provider::complete`), never in the request body, and the error string
  built from a provider failure is a short HTTP-status slice — verified to
  contain no token.
- **`GET /api/v1/admin/candles` is the quarantine inspection view.** It returns
  rows the guarded read APIs withhold, so it is admin-only; it is never on an
  unauthenticated route.
- **The destructive admin route is single-row by construction.** `DELETE
  /api/v1/admin/candles` removes exactly one fully-qualified
  `(source_id, symbol, timeframe, open_time)` row; there is no bulk delete, so a
  mistake cannot wipe the store.

## Input validation

All bodies are parsed by a strict, dependency-free JSON parser that reports
malformed input rather than coercing it. Validated before persistence:

- **Identity:** `source_id`, `symbol`, `timeframe` required; unknown timeframe →
  `400 invalid_timeframe` before any store work, so an unusable row is never
  written.
- **OHLC:** all four of open/high/low/close required; each must be positive; the
  ordering invariant `low ≤ min(open,close) ≤ max(open,close) ≤ high` must hold;
  volumes and spread must be non-negative.
- **Timestamps:** non-positive open time → rejected; open time in the future
  (beyond a 5-minute skew, evaluated in the broker clock frame) → rejected;
  intraday open time off the timeframe grid → rejected.
- **Batch bounds:** `ASTRA_MAX_CANDLES_PER_BATCH` (default 500) refuses an
  oversized batch with `413` before parsing; the news route caps at 200 articles.
- **Request size:** bodies over `maxRequestBytes` (1 MiB) → `413`.
- **Query parameters:** unknown `tf` → `400`; out-of-range `limit` is clamped;
  a missing `id` on `/news/analyze` → `400`, an unknown id → `404`.
- **HTTP framing:** a missing header terminator, a body shorter than its
  declared `Content-Length`, or a target not starting with `/` are answered with
  `400` and a stable code. A per-connection `SO_RCVTIMEO` (10 s) bounds how long
  a stalled client can hold a worker.

## Error responses must not leak internals

Every non-2xx body is the flat object `{"error":"true","code":...,"message":...}`
— never an enveloped success shape. Messages are short, human-readable and
generic (`"no candle data available for M15"`, `"a valid sender bearer token is
required"`). A dedicated assertion (`checkNoInternalLeak`) rejects any body
containing: the data directory path, any of the three configured tokens, a C++
source filename, `sqlite3`, `std::`, `AstraApiServer`, or `/workspace`. It is
applied to 401/403/404/405/429/500/400 responses and to the news read responses.

SQLite error strings (which can contain a filesystem path) are returned only to
the *server log*, never to a client: a news-store failure yields the generic
`500 store_failure` / `"news persistence failed: news store is not open"`.

## CORS

CORS is an exact-match allow-list (`ASTRA_ALLOWED_ORIGINS`). A browser request
from a non-allow-listed origin → `403 origin_not_allowed` before any handler
runs. Non-browser requests (no `Origin`) are unaffected. Loopback origins are
deliberately not in the production allow-list; do not add them (`AGENTS.md`).

## Transport and availability

- `astra_server` binds `0.0.0.0:$PORT` and is intended to sit behind the
  platform TLS terminator (Railway). In-process the HTTP is plain; there is no
  in-process TLS. Treat the credential as bearer-over-TLS.
- The liveness probe (`/api/v1/livez`) is unauthenticated and consults no engine
  or bridge state; readiness (`/api/v1/readyz`) reports `503` until real closed
  candles arrive. Neither exposes data.
- Public read routes (`/api/v1/health`, `/analysis/latest`, `/timeframes`, …)
  are unauthenticated by design (the frozen v1 contract) and carry only derived
  analysis, never credentials.

## Findings

### F-E-3 — `news/ingest` credential moved from client to sender (RESOLVED)

During this session the working tree changed `POST /api/v1/news/ingest` from
client-token to sender-token authorisation (`src/api/AstraApiServer.cpp`). This
is the correct boundary: news ingestion is a write, and the client token is
embedded in the Android app, so it must never authorise a write. Verified after
the change: `client-secret` and `admin-secret` → 401, `sender-secret` → 200
(`ayzr_news_ingest_write_requires_sender_credential`). No action required; noted
so the contract change is on the record. Note this is a **breaking change** for
any existing news publisher using the client token — publishers must use the
sender token.

### F-E-4 — Stale-feed masking in the Python bridge (OPEN, see TESTING.md F-E-1)

`bridge/mt5_python/mt5_client.py` can report a feed stale by up to 18 h as
`FRESH`/`VALID`. This is a data-integrity (honesty) issue rather than a secret
disclosure, but it defeats a guard the security model relies on: freshness is
what gates whether a reading is decision-grade. Reproduced deterministically
(see `docs/ayzr/TESTING.md`, Finding F-E-1). Not resolved here: the fix belongs
to the bridge owner and needs a design decision (do not simply widen the clamp).

### F-E-5 — Test isolation on a hard-coded port (OPEN, environmental)

`PythonBridgeContractTests` binds the fixed port 8799. A concurrent listener
makes the bridge fail to bind and the test report false failures. Use an
ephemeral port. Test-only; no product impact.

### F-P-1 — Bearer-token comparison is not constant-time (OPEN, low severity)

`authorize`, `authorizeAdmin` and `authorizeClient`
(`src/api/AstraApiServer.cpp`) compare the presented token with `==`, which
short-circuits on the first differing byte. In principle this leaks token length
and prefix through timing. In practice the exposure is small here: the
comparison runs over loopback/HTTPS with high network jitter, the tokens are
operator-chosen random strings, and the three credential classes are separate.
Consistent with the pre-existing `authorize`/`authorizeAdmin` style, so the new
`authorizeClient` matches it rather than diverging. If this surface is ever
exposed to an untrusted network, switch all three to a constant-time compare
(e.g. `CRYPTO_memcmp`). Not changed in this pass to avoid an inconsistent fix.

## Not in scope / explicitly not claimed

- No penetration test of the platform TLS terminator, DNS, or the Railway
  platform itself.
- No review of `astra-frontend/` (out of scope) or of the Windows sender EXE.
- No claim that the three tokens are strong; strength is the operator's
  responsibility (`.env.example` only requires that they differ).
