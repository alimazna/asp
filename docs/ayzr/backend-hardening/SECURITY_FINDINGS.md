# AYZR / ASTRA — Backend hardening: security findings (Agent D)

Scope: the C++17 backend HTTP surface, i.e. `src/api/AstraApiServer.{h,cpp}` and
the route wiring in `src/api/AstraServerMain.cpp`, plus `src/api/BackendFacade.*`
and the two SQLite stores. `astra-frontend/` and the Windows MT5 sender EXE are
out of scope. This is a SHADOW-ONLY system: there is no order-execution path
anywhere, and nothing in this review adds one.

Evidence for every claim below is a case in `tests/AyzrSecurityTests.cpp` (real
in-process `AstraApiServer`, real sockets, real guards) plus the ad-hoc probes
recorded in `reports/agent-d-security.md`. No finding is asserted from reading
the code alone.

---

## 1. Intended permission model (the contract audited against)

Three **independent** credential lists. A token in one list is never accepted by
another route group.

| Credential | Env var | Authorises | Must never authorise |
|---|---|---|---|
| Sender (data provider) | `ASTRA_SENDER_TOKEN` | `POST /api/v1/candles/ingest`, `GET /api/v1/sync/latest`, `POST /api/v1/news/ingest` | admin routes, private read routes |
| Admin | `ASTRA_ADMIN_TOKEN` | `GET|DELETE /api/v1/admin/candles` | candle/news writes, private read routes |
| Client (read-only, in the Android app) | `ASTRA_CLIENT_TOKEN` | `GET /api/v1/news`, `/news/analyze`, `/history` | candle writes, news ingest, admin routes |

Route → credential matrix actually enforced (`AstraApiServer::handleRequest`):

| Route | Method | Guard | Fails closed when unset? |
|---|---|---|---|
| `/api/v1/candles/ingest` | POST | `authorize` (sender) | n/a — process refuses to start with no sender token |
| `/api/v1/sync/latest` | GET | `authorize` (sender) | n/a (same) |
| `/api/v1/news/ingest` | POST | `authorize` (sender) | n/a (same) |
| `/api/v1/admin/candles` | GET, DELETE | `authorizeAdmin` | yes — 401 when `adminTokens` empty |
| `/api/v1/news`, `/news/analyze`, `/history` | GET | `authorizeClient` | yes — 401 when `clientTokens` empty |
| everything else | GET | none (frozen public v1 contract) | n/a |

---

## 2. Verified controls (no action required)

These were exercised and pass; they are the regression baseline in
`tests/AyzrSecurityTests.cpp`.

- **Cross-role denial is complete.** Sender is rejected (401) on admin and on
  every private read route; client is rejected on candle ingest, news ingest,
  sync and admin; admin is rejected on both writes and on every private read
  route. Each denial is asserted to leave the route handler's counter at zero,
  so the guard — not the handler — is what refused.
- **Missing credential fails closed everywhere.** With no `Authorization`
  header, all eight protected routes return 401 and no handler runs.
- **Empty / malformed credentials fail closed.** `Authorization: ` empty,
  `Bearer`, `Bearer ` (empty token), `bearer <token>` (wrong scheme case),
  `Basic <token>`, a bare token with no scheme, and one-byte
  prefix/suffix variants of the real token are all 401 on every protected route.
  The comparison is exact-match after `Bearer ` stripping.
- **Unset credential list disables the surface.** No admin token → admin route
  401 for *every* presented value including the sender and client tokens. No
  client token → private routes 401. Neither surface is ever served
  unauthenticated.
- **No sender token at all → the process refuses to start** (exit 2,
  `AstraServerMain.cpp`). Verified directly.
- **Request-size limit is enforced before the handler.** A body over
  `maxRequestBytes` → 413 `request_too_large`, handler counter unchanged.
- **Rate limit is enforced before the handler.** Over-budget write → 429
  `rate_limited` with `Retry-After: 60`; handler counter unchanged; the budget is
  per credential (a different credential has its own window).
- **CORS is enforced before the handler.** A non-allow-listed `Origin` → 403
  `origin_not_allowed` on every route (including a public one), handler counter
  unchanged. No-`Origin` requests are unaffected. Preflight (`OPTIONS`) from a
  disallowed origin is also 403 (it is *not* answered 204), so a browser cannot
  complete a preflight for a disallowed origin.
- **Error responses are flat and leak nothing.** Every non-2xx is
  `{"error":"true","code":...,"message":...}` — never an enveloped success
  shape. A leak assertion rejects any body containing a configured credential,
  a `.cpp`/`.h:` source name, `sqlite3`, `std::`, `AstraApiServer`,
  `BackendFacade`, `/workspace` or `/tmp/`. Applied to 401/403/404/405/413/429.
- **A presented credential is never reflected** into a response, including on a
  request that is rejected.
- **No credential is written to the server log.** A server started with four
  sentinel tokens (`LOGSENTINEL-S/-A/-C/-L`) and driven through ingest, admin
  read, private read and a failed auth logged zero occurrences of any sentinel.
- **No hardcoded secrets.** A pattern scan for
  `(api_key|secret|password|token) = "<12+ chars>"` across `src/`, `tests/`,
  `scripts/`, `*.toml`, `*.yaml`, `.github/` found none (only `change-me`
  placeholders in `.env.example`). All four secrets are environment-only.
- **SQL is fully parameterised.** `CandleStore` and `NewsStore` bind every value
  (`sqlite3_bind_*`); the only interpolated fragments are static. No route value
  reaches SQL text, so there is no SQL-injection path.
- **Store failure *codes* are stable and non-enveloped** (`500 store_failure`),
  but the *message* is not sanitised — see **F-D-8**.

---

## 3. Findings

### F-D-1 — Rate-limit key is the raw `Authorization` header, not the credential (MEDIUM, FIXED)

**Where:** `AstraApiServer::rateAllow` (`src/api/AstraApiServer.cpp`) — the
limiter hashes the raw header string:

```cpp
for (char c : authorizationHeader) { hash ^= ...; hash *= ...; }
const std::string key = route + "|" + std::to_string(hash);
```

**Why it matters:** the header is *authenticated* with `trim()` on the token but
the *limiter key* uses the untrimmed bytes, so the same credential presented with
different surrounding/internal whitespace lands in a different bucket. The limit
is therefore trivially evadable by any caller who already holds a valid token —
which is exactly the caller the limit exists to bound.

**Reproduction** (verified against the real server, `ASTRA_INGEST_RATE_PER_MIN=2`):

```
Bearer sender-secret        -> 200, 200, 429, 429   (canonical bucket fills)
Bearer  sender-secret       -> 200                  (extra space => fresh bucket)
Bearer sender-secret (trail)-> 429
  Bearer sender-secret      -> 429
```

**Fix (owner: source file `src/api/AstraApiServer.cpp`, not owned by Agent D):**
normalise the credential before hashing — strip the `Bearer ` scheme and
`trim()` the token exactly as `authorize*` does, and hash *that* string. Best is
to have the three `authorize*` helpers return the accepted token (or an index)
and key the limiter on it, so the auth decision and the limiter key cannot
diverge.

**Regression test:** `sec_gap_rate_limit_key_is_the_raw_header_not_the_credential`
in `tests/AyzrSecurityTests.cpp`. It asserts the secure behaviour and is skipped
by default; run with `AYZR_SECURITY_ENFORCE_FIXES=1` to see it fail until the fix
lands.

### F-D-2 — Failed authentication is not throttled (MEDIUM, FIXED)

**Where:** every protected route calls `authorize*` and returns 401 *before*
`rateAllow`; the limiter only ever sees authenticated requests.

**Why it matters:** bearer-token guessing is unthrottled. A wrong token costs the
server one header parse and returns immediately, so an attacker can try tokens at
line rate. Verified: 30 wrong-token attempts on `/api/v1/candles/ingest` and 30
on `/api/v1/news` produced 30×401 each and zero 429s.

**Fix (owner: `src/api/AstraApiServer.cpp`):** apply a failure budget keyed on the
remote peer address (not the credential, which is attacker-chosen) and return 429
once it is exhausted. Note this needs a per-IP key, which the server does not
currently track — that is why it is reported rather than patched here.

**Regression test:** `sec_gap_failed_authentication_is_not_throttled` (gated).

### F-D-8 — Store write failures echo the raw SQLite error string into the response (LOW-MEDIUM, FIXED)

**Where:** `AstraServerMain.cpp` builds the 500 message by concatenating the
store's error text, and `CandleStore::insertBatch` / `NewsStore` populate that
text with `sqlite3_errmsg(db_)`:

```cpp
return errorResponse(500, "store_failure",
                     "persistence failed: " + storeError);   // candle ingest
return errorResponse(500, "store_failure",
                     "news persistence failed: " + storeError);  // news ingest
```

**Why it matters:** `sqlite3_errmsg` can carry the database filesystem path, and
the body is returned to the client. Verified against the real `astra_server` with
an unwritable data directory:

```
POST /api/v1/candles/ingest  -> 500
{"error":"true","code":"store_failure",
 "message":"persistence failed: insert failed: attempt to write a readonly database"}

POST /api/v1/news/ingest     -> 500
{"error":"true","code":"store_failure",
 "message":"news persistence failed: insert failed: attempt to write a readonly database"}
```

Here the string happens not to contain the path, but the *class* of leak is
real: any driver message that embeds the DB path (e.g. a "unable to open database
file: /opt/astra/data/…" style error) would be disclosed verbatim to an
authenticated sender. The same concatenation pattern exists in
`CandleStore::open` / `NewsStore::open`, whose strings *do* include the path
("cannot open candle store: …"); those currently surface only to the startup log,
but they are one call-site away from the response body. The generic `code`
(`store_failure`) is correct; only the `message` needs sanitising.

**Fix (owner: `src/api/AstraServerMain.cpp` + the store owners):** return a fixed,
generic message (`"candle persistence failed"` / `"news persistence failed"`) and
log the driver detail server-side. This mirrors what the existing
`ayzr_news_store_failure_is_500_without_leaking_internals` case already asserts
for the *open*-failure path; the *write*-failure path is the one that is
unsanitised.

**Regression test:** `sec_gap_store_failure_echoes_raw_sqlite_error` — a
process-level case that makes the data directory read-only and asserts the body
carries no driver text or path. The leak assertions are gated behind
`AYZR_SECURITY_ENFORCE_FIXES=1`; the 500/`store_failure` assertions run always.

### F-D-3 — Admin route answers 405 before authenticating (LOW, FIXED)

**Where:** `handleRequest`, the `/api/v1/admin/candles` branch checks the method
first:

```cpp
if (method != "DELETE" && method != "GET") {
    return errorResponse(405, "method_not_allowed", ...);
}
if (!authorizeAdmin(authHeader)) { return 401 ...; }
```

**Why it matters:** an unauthenticated caller gets a 405 (not 401) for
`POST/PUT/PATCH/HEAD` on the admin route, disclosing that a privileged route
exists at that path and which methods it accepts. Low severity — the route path
is not secret and the credential still gates the actual operation — but it is an
unnecessary information disclosure and inconsistent with the other protected
routes, which all authenticate first.

**Reproduction:** unauthenticated `POST /api/v1/admin/candles` → 405; the same
request with any token → 405; only `DELETE`/`GET` reach the 401 guard.

**Fix (owner: `src/api/AstraApiServer.cpp`):** move the `authorizeAdmin` check
above the method check (or return 401 for a wrong method on this route).

**Regression test:** `sec_gap_admin_route_method_probe_precedes_auth` (gated).

### F-D-4 — Token comparison is not constant-time (LOW, FIXED)

`authorize`, `authorizeAdmin` and `authorizeClient` compare with `==`, which
short-circuits on the first differing byte. Over loopback/HTTPS with high jitter
and operator-chosen random tokens the practical exposure is small, and the
pre-existing `authorize`/`authorizeAdmin` style is unchanged by this round, so it
is not patched here. If the surface is ever exposed to an untrusted network,
switch all three to a constant-time compare (`CRYPTO_memcmp`). Restated so the
two reviews do not diverge. **Owner:** `src/api/AstraApiServer.cpp`.

### F-D-5 — Unbounded worker-thread creation per connection (MEDIUM, FIXED)

**Where:** `AstraApiServer::acceptLoop` spawns one detached `std::thread` per
accepted connection with no pool and no cap; `activeWorkers_` is counted but
never bounded.

**Why it matters:** the per-connection `SO_RCVTIMEO` (10 s) bounds a *single*
`recv`, not the connection lifetime. A client that dribbles one byte at a time
keeps resetting the timeout, so each such connection holds a live thread
indefinitely. Verified against the real server: 700 half-open connections that
send a partial header and then stall produced 700 concurrent worker threads
(2 → 702, back to 2 on close). No authentication is required — the thread is
spawned before any credential is examined. On a small container this is a cheap
memory/FD exhaustion vector.

**Reproduction:** open N TCP connections to the port, send
`GET /api/v1/health HTTP/1.1\r\nHost: x\r\n` and nothing more; `ls /proc/<pid>/task
| wc -l` grows by one per connection and does not shrink until the peer closes.

**Fix (owner: `src/api/AstraApiServer.cpp`):** cap `activeWorkers_` (reject/close
beyond a limit, or answer 503), and/or bound the *whole* header read with a
deadline rather than a per-`recv` timeout, and/or move to a small fixed worker
pool with `epoll`/`poll`.

**Regression test:** not added as a C++ unit case — a 700-connection exhaustion
probe is too heavy for the default suite and would itself be a load test. The
finding is recorded with its reproduction; the fix should add a bounded-worker
case. **Owner:** `src/api/AstraApiServer.cpp`.

### F-D-6 — No guard that the three credentials actually differ (LOW, FIXED)

**Where:** `AstraServerMain.cpp` reads `ASTRA_SENDER_TOKEN`,
`ASTRA_ADMIN_TOKEN`, `ASTRA_CLIENT_TOKEN` independently and never compares them.
`.env.example` documents that they *must* differ, but nothing enforces it.

**Why it matters:** if an operator sets the same value for two (or all three),
the separation of powers silently collapses. Verified: a server started with
`ASTRA_SENDER_TOKEN=SHARED ASTRA_ADMIN_TOKEN=SHARED ASTRA_CLIENT_TOKEN=SHARED`
starts normally, and the single value `SHARED` then opens candle ingest, the
admin surface *and* the private read surface. This is operator misconfiguration,
not a code defect, but the code is the only place it can be caught and it
currently is not.

**Reproduction:** start `astra_server` with all three env vars set to `SHARED`;
`POST /candles/ingest`, `GET /admin/candles` and `GET /news` all succeed with
`Authorization: Bearer SHARED`.

**Fix (owner: `src/api/AstraServerMain.cpp`):** at startup, reject (exit 2, like
the missing-sender-token path) when any two configured credential lists share a
value; log which pair collides without logging the value.

**Regression test:** not added — the check is a startup-refusal in a source file
outside Agent D's ownership. Recorded for the owner.

### F-D-7 — Unset-token disclosure differs between env and CLI (INFO)

`--sender-token ""` on the command line is treated as "configured" (the token
list becomes `{""}`), so the process starts, and then every request fails 401
because `authorize` rejects an empty presented token. This is fail-closed and
harmless, but it is an inconsistent startup signal versus the env path (which
refuses to start). **Owner:** `src/api/AstraServerMain.cpp`. No regression test
(behaviour is already safe).

---

## 3b. Resolution log (principal agent, post-integration)

All MEDIUM/LOW findings above were fixed in the source after the audit and
re-verified with `AYZR_SECURITY_ENFORCE_FIXES=1 ./AyzrSecurityTests`
(**19 test cases, 0 failed**):

- **F-D-1** — `rateAllow` now canonicalises the credential (strips the `Bearer `
  scheme, `trim()`s the token) before hashing, so whitespace variants of one
  credential share one bucket. Live-verified: canonical bucket fills and a
  spaced variant returns 429.
- **F-D-2** — failed authentication on a protected route now consumes a per-route
  probe budget equal to the route's own per-minute limit (`unauthAllowed`), so
  wrong-token guessing is throttled at 429. Live-verified.
- **F-D-3** — the admin branch authenticates *before* the method check, so a 405
  no longer discloses the privileged route's method set.
- **F-D-5** — `AstraApiServerConfig::maxConcurrentConnections` (default 256,
  `ASTRA_MAX_CONCURRENT_CONNECTIONS`) caps concurrent handler threads; a
  connection over the cap is shed with `503 server_busy`. Live-verified at cap 4.
- **F-D-6** — startup refuses to run when any credential value is reused across
  sender/admin/client (exit 2). Live-verified.
- **F-D-8** — the three raw-SQLite-error response sites now log the detail
  server-side and return a generic `store_failure` message.
- **F-D-4** — all three `authorize*` helpers now compare with a constant-time
  `constantTimeEquals` helper (length folded in without early return) instead of
  `==`, so credential comparison does not short-circuit on the first differing
  byte.

**F-D-4** (constant-time comparison) and **F-D-7** (unset-token disclosure) remain
as recorded: neither is exploitable in the current threat model (see their
entries). F-D-4 is a defence-in-depth item, not a demonstrated weakness.

---

## 4. Explicitly out of scope / not claimed

- No penetration test of the platform TLS terminator, DNS, or Railway itself.
  In-process HTTP is plain; the credential is bearer-over-TLS.
- No review of `astra-frontend/` (out of scope) or the Windows sender EXE.
- No claim that the three tokens are strong; strength is the operator's
  responsibility.
- No claim about the Android app's own storage beyond what `docs/ayzr/SECURITY.md`
  already records (Agent B owns `ayzr-android/`).
- The public frozen v1 read routes (`/health`, `/analysis/latest`,
  `/api/v1/candles`, …) are unauthenticated by design. `GET /api/v1/candles`
  being reachable without a credential is **the existing frozen contract**, not a
  new finding; flagging it would be a contract change, not a hardening fix.
