# ASTRA ingestion contract — what the backend must implement

**Verified against:** the real `astra_server` binary, 2026-10-28.
Every request/response below was produced by running the server and calling it,
not read from a document. Where a documented claim disagreed with the binary,
the binary won and the disagreement is noted.

This is the spec to hand to whoever builds the backend. It is the minimum a
receiver must do for the ASTRA Collector to deliver candles and to be able to
tell the truth about whether it succeeded.

---

## 0. The one thing that is easy to get wrong

The collector stores a **base URL** and appends fixed relative paths to it.

```
base_url + "/health"
base_url + "/candles"
base_url + "/candles/ingest"
base_url + "/sync/latest"
```

There is **no `/api/v1` in the collector's path constants**. The base URL must
therefore include the version prefix:

```
https://<railway-host>/api/v1        <- correct
https://<railway-host>               <- every request 404s
```

Verified: with `base_url = http://127.0.0.1:12099` every call returns
`404 {"code":"not_found"}`; with `http://127.0.0.1:12099/api/v1` all four
routes answer correctly.

The app should normalise this on save (accept a bare host and append
`/api/v1`), because a 404 here is silent data loss, not an obvious error.

---

## 1. Transport and framing

* HTTPS in production. Plain HTTP is only for local testing.
* Request and response bodies are JSON, `Content-Type: application/json`.
* Every successful response is wrapped in an envelope:

```json
{"api": "v1", "schema": "1.0", "data": { ... }}
```

* Every error is:

```json
{"error": "true", "code": "<machine_code>", "message": "<human text>"}
```

`code` is what the collector acts on. It must be stable. `message` is shown to
the user, so it must not contain a token.

---

## 2. Authentication

One credential, sent on every request except the health probe:

```
Authorization: Bearer <sender-token>
```

* The token is a **sender** credential, never an admin credential. The
  collector must not be able to delete data or change configuration.
* A missing or wrong token returns **401** with `code: "unauthorized"`. The
  collector maps 401/403 to `AUTH_FAILED` and stops retrying until the user
  fixes it — so do not return 401 for a transient problem.
* **Rate limiting must be per-credential, not per-IP.** Verified: the current
  server keys its rate window on the token hash. A per-IP limit is wrong here
  because every Windows sender behind one office NAT shares an IP, and the
  frontend and the sender may also share one.

---

## 3. Routes

### 3.1 `GET /health` — liveness, unauthenticated

The collector calls this to show "Server: ONLINE/OFFLINE". It must answer
without a token and without touching the database.

Verified response (`GET /api/v1/health`):

```json
{"api":"v1","schema":"1.0","data":{"aggregate":"STARTING",
 "decision_grade_data":false,"degraded_reasons":[],"observed_at":1793190689000,
 "unknown_is_not_safe":true,"version":"v1","schema":"1.0","strategy_version":"..."}}
```

**Critical:** the path is `/health`, not `/healthz` and not `/api/v1/health`
once the base already carries `/api/v1`. Bare `/health` on the current server
is a **404** — see section 0.

A 200 here means "reachable", nothing more. Do not return 200 with a body that
claims healthy data when the pipeline has no data; the collector displays the
`aggregate` field verbatim and the owner trusts it.

### 3.2 `POST /candles/ingest` — the only write path

Request:

```json
{
  "schema_version": "1.0",
  "source_id": "owner-pc-1",
  "symbol": "XAUUSD",
  "timeframe": "M15",
  "clock_offset_seconds": 10800,
  "candles": [
    {"uid":"...","symbol":"XAUUSD","timeframe":"M15","time":1793183400,
     "open":2650.1,"high":2652.4,"low":2649.05,"close":2651.75,
     "tick_volume":1200,"spread":25,"real_volume":0,"source":"MT5"}
  ]
}
```

Field rules, all verified against the binary:

| Field | Required | Notes |
|---|---|---|
| `source_id` | yes | 400 `missing_fields` if absent |
| `symbol` | yes | 400 `missing_fields` if absent |
| `timeframe` | yes | must be one of the nine; else 400 `invalid_timeframe` |
| `clock_offset_seconds` | **effectively yes** | see below |
| `candles` | yes | must be an array; else 400 `missing_candles` |
| `candles[].time` | yes | **open time in epoch seconds**. Not `open_time_utc` — that name is silently ignored and every candle is rejected |
| `candles[].open/high/low/close` | yes | all four, else `candle requires open/high/low/close` |
| `candles[].tick_volume/spread/real_volume` | no | default 0 |
| `candles[].uid` | no, but send it | echoed back in the ack; without it the ack is matched by symbol/timeframe/open_time |

**`clock_offset_seconds` is not optional in practice.** MT5 stamps bars in
broker server time. The server's "is this candle in the future" guard runs in
the clock frame it is told to use. Omit the offset and every bar from a UTC+2
or UTC+3 broker is rejected as future-dated — which is the common case for gold
brokers. The collector measures `broker_server_time - utc` and sends it.

Batch limit: **500 candles per request** (`ASTRA_MAX_CANDLES_PER_BATCH`).
Exceeding it is 413 `batch_too_large`, and the whole batch is refused, so the
sender must paginate. The collector batches per stream.

Per-candle validation the server performs (reject the candle, do not fail the
request):

* open time not in the future (300 s slack, in the broker frame)
* open time > 0
* all four prices present and > 0
* `high >= max(open,close)` and `low <= min(open,close)` and `high >= low`
* volumes and spread non-negative
* open time aligned to the timeframe grid

A rejected candle must appear in `rejected[]` **with its identity**, not only
as a free-text reason. One malformed candle must not cause the others in the
batch to be reported as failed.

Response:

```json
{"api":"v1","schema":"1.0","data":{
  "source_id":"probe-sender","symbol":"XAUUSD","timeframe":"M15",
  "received":1793190786,
  "accepted":[{"identity":"symbol=XAUUSD&timeframe=M15&open_time=1793183400",
               "state":"stored_new"}],
  "duplicates":[{"identity":"...","state":"already_stored"}],
  "rejected":[{"identity":"...","state":"rejected","reason":"..."}],
  "ingested":1}}
```

**The ack is the delivery guarantee.** The collector confirms a candle only if
it appears in `accepted[]` with `state: stored_new`, or in `duplicates[]` with
`state: already_stored`. Anything it sent that appears in neither is
`unconfirmed` and is retried with the same identity. A 200 alone proves nothing
and must never be treated as success.

Note: the response is **per batch**, not per candle, in the sense that
`symbol`/`timeframe` are top-level. If a backend accepts mixed timeframes in
one request, the ack must still identify each candle's timeframe.

### 3.3 `GET /sync/latest` — reconciliation, authenticated

Used for two things: proving a credential works without writing anything, and
letting a sender rebuild its local ledger from the server's authoritative state.

With `?source_id=<id>` the answer is scoped to that sender and **may be treated
as delivery proof**:

```json
{"api":"v1","schema":"1.0","data":[
  {"symbol":"XAUUSD","timeframe":"M15","last_open_time_utc":1793183400,"count":1}]}
```

Without `source_id` the answer is an aggregate across all senders and **must
not** be used as proof for any one sender. Keep both behaviours; the collector
uses the scoped form.

### 3.4 `GET /candles` — read path for the frontend and reconciliation

Query: `?tf=M15&limit=500` (`timeframe` must be accepted as an alias for `tf` —
the collector and the bridge both spell it `timeframe`, and silently defaulting
to M15 gives a wrong answer with no error).

```json
{"api":"v1","schema":"1.0","data":{
  "bars":[{"time":1793183400,"open":2650.1,"high":2652.4,"low":2649.05,
           "close":2651.75,"tick_volume":1200,"spread":25,"real_volume":0}],
  "timeframe":"M15","symbol":"XAUUSD","count":1,"closed_only":true,
  "newest_closed_time":1793183400}}
```

Two behaviours the collector depends on and that must be preserved:

1. **An empty stream returns HTTP 503 `dependency_unavailable`**, not 200 with
   an empty array. The collector reads that 503 as the authoritative answer
   "the server holds nothing for this stream" and re-sends. Returning 200 with
   `[]` would also work, but the two must not be confused with "unreachable",
   because the collector repairs only on an *answer*.
2. **`limit` is clamped and the page is oldest-first.** The current server
   clamps to 500 and returns `ORDER BY open_time ASC`. This means a stream
   longer than the page is truncated, and a sender cannot distinguish "the
   server has these 500" from "the server has only these 500".

> **Recommendation for the new backend.** Point 2 is a real limitation: the
> collector can only repair a *totally* lost stream, never a partial one,
> because a truncated oldest-first page looks like a complete inventory. Adding
> a cursor — `?since=<epoch>` or an opaque `next_cursor` — would let a sender
> walk the full inventory and detect partial loss. Until then, partial loss is
> undetectable by design and should be documented as such.

---

## 4. Idempotency — the server's half of duplicate prevention

The database must enforce a uniqueness constraint on:

```
(source_id, symbol, timeframe, open_time)
```

Verified: `candles.sqlite` has `PRIMARY KEY (source_id, symbol, timeframe,
open_time)`. Two identical POSTs produced **one row** and the second returned
`already_stored`.

Rules:

* A repeat of the same candle must not create a second row and must not trigger
  duplicate downstream analysis.
* The response must say `already_stored` so the sender can stop retrying.
* Use a transaction so persistence and the ack cannot disagree.

Do **not** promise exactly-once network delivery. A response can be lost after
the server committed. The correct guarantee is at-least-once retry from the
sender plus server-side idempotency, so each candle is persisted and processed
once *logically*.

---

## 5. Error codes the collector acts on

| HTTP | code | Collector behaviour |
|---|---|---|
| 401/403 | `unauthorized` | `AUTH_FAILED`; stop and ask the user to fix the token |
| 429 | `rate_limited` | back off and retry; `Retry-After` is honoured |
| 503 | `dependency_unavailable` | on `/candles` only: authoritative empty stream |
| 400 | `malformed_json`, `missing_fields`, `missing_candles`, `invalid_timeframe` | permanent; surface to the user, do not retry forever |
| 413 | `batch_too_large` | split the batch and resend |
| 500 | `store_failure` | transient; retry with backoff |
| — | timeout / connection lost | **uncertain**; retry with the same identity |

A dropped connection after the request was sent is the classic uncertain
delivery. The collector treats it as pending and retries with the same uid,
which is safe only because of section 4.

---

## 6. CORS

Only needed for the browser frontend, never for the sender (a Python client
sends no `Origin`).

Verified: the current server **denies every origin by default** and returns
403 `origin_not_allowed`. That is the correct default. The new backend must:

* keep an explicit allowlist, configured by env (`ASTRA_ALLOWED_ORIGINS`),
* include the production Cloudflare Pages origin,
* include preview origins only if deliberately approved,
* never use `*` while the API is authenticated,
* answer `OPTIONS` preflight with `Access-Control-Allow-Headers: Authorization,
  Content-Type` and `Access-Control-Allow-Methods: GET, POST, OPTIONS`.

---

## 7. Configuration the backend must read

| Variable | Meaning | Default |
|---|---|---|
| `PORT` | bind port, assigned by the platform | 8080 |
| `ASTRA_HOST` | bind address | `0.0.0.0` |
| `ASTRA_DATA_DIR` | durable data directory | `./data` |
| `ASTRA_SENDER_TOKEN` | comma-separated accepted sender tokens | none — refuse to start |
| `ASTRA_ADMIN_TOKEN` | separate credential for destructive routes | none |
| `ASTRA_ALLOWED_ORIGINS` | comma-separated browser origins | none (deny all) |
| `ASTRA_INGEST_RATE_PER_MIN` | per-credential ingest limit | 240 |
| `ASTRA_SYNC_RATE_PER_MIN` | per-credential sync limit | 240 |
| `ASTRA_MAX_CANDLES_PER_BATCH` | batch cap | 500 |

Verified: the server **exits** with a fatal message when no sender token is
configured. Keep that — starting unauthenticated is worse than not starting.

---

## 8. Durable storage

`ASTRA_DATA_DIR` must point at a **persistent volume**, not the container
filesystem. Verified: the server writes `candles.sqlite` plus a WAL. On Railway
this needs a mounted volume; an ephemeral filesystem loses every candle on each
deploy. The WAL means the data directory must be a real filesystem that
supports `-shm`/`-wal` files — some network filesystems do not, so this is
worth testing before trusting it.

---

## 9. What the collector sends, in order, on each pass

1. `GET /health` — set Server status. Never fails the pass.
2. `GET /candles` per timeframe — learn what the server holds. A 503 means
   "holds nothing" and triggers a re-send for that stream.
3. Read the latest **closed** candle per timeframe from MT5.
4. Compare against the local ledger; queue only unacknowledged candles.
5. `POST /candles/ingest` in batches of ≤500, one stream per request.
6. Record an acknowledgment **only** on `stored_new` / `already_stored`.

Everything after step 2 is bounded by the ledger, so a restart does not resend
what the server already confirmed.

---

## 10. Acceptance tests for the new backend

Run these before declaring the backend ready. Each one maps to a way the
collector has already been broken once.

1. Bare host in the base URL → the app must still work (section 0).
2. Correct candle → `stored_new`, and a row appears in the store.
3. Same candle again → `already_stored`, still one row.
4. No token / wrong token → 401, and no row written.
5. `open_time_utc` instead of `time` → every candle rejected, and the ack names
   them (do not silently accept).
6. A UTC+3 candle with `clock_offset_seconds` → accepted. Without it → rejected
   as future-dated. (This is the broker-time trap.)
7. OHLC violation → that candle rejected, others in the batch unaffected.
8. 501 candles → 413; 500 → accepted.
9. M30 with no data → `/candles` answers 503 `dependency_unavailable`.
10. Two senders, same symbol/timeframe/open_time, different `source_id` → two
    rows (the identity includes the source).
11. Origin not in the allowlist → 403 `origin_not_allowed`.
12. Rate limit exceeded → 429 with `Retry-After`.
13. No `ASTRA_SENDER_TOKEN` configured → process refuses to start.
14. Restart the process → previously ingested candles are still there.
