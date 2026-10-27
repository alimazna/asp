# AYZR — MT5 Sender Integration Contract (receiving API)

> Scope note: the Windows MT5 Sender EXE is a **separate project**. This document
> describes only the *receiving* interface the backend already provides, so the
> separate sender can integrate against it later. Nothing here implements or
> modifies that EXE.

## 1. What already exists (reused, not rebuilt)

The backend already exposes a complete, authenticated candle-ingestion surface.
This work **reuses** it unchanged; no new ingest route was added.

| Method | Path | Auth | Handler |
|--------|------|------|---------|
| POST | `/api/v1/candles/ingest` | sender bearer token | `AstraServerMain` ingest lambda |
| GET | `/api/v1/sync/latest` | sender bearer token | `store.syncPointsForSource` / `syncPoints` |

Validation performed by the existing `parseCandle` (`src/api/AstraServerMain.cpp`):

- `time` must be numeric (or numeric string) and a positive epoch timestamp;
- `open/high/low/close` are **required** and must be finite and positive;
- OHLC ordering is enforced: `high >= max(open,close)` and `low <= min(open,close)`;
- `tick_volume`, `real_volume`, `spread` must be non-negative when present;
- the intraday timeframe grid check rejects a candle whose `open_time` is not
  aligned to its timeframe (D1/W1/MN1 exempt — whole-hour broker offsets);
- a candle whose open time is more than 5 minutes in the future (measured in the
  sender's broker clock) is rejected.

Persistence identity is `(source_id, symbol, timeframe, open_time)`, enforced by
the SQLite primary key. Replaying a batch is therefore safe: the store returns
`already_stored` for a row it already holds and never duplicates it.

## 2. Request

`POST /api/v1/candles/ingest`
`Authorization: Bearer <ASTRA_SENDER_TOKEN>`
`Content-Type: application/json`

```json
{
  "source_id": "sender-pc-1",
  "symbol": "XAUUSD",
  "timeframe": "M15",
  "clock_offset_seconds": 10800,
  "candles": [
    {
      "time": 1792045800,
      "open": 2653.10,
      "high": 2655.20,
      "low": 2651.80,
      "close": 2654.05,
      "tick_volume": 210,
      "spread": 12,
      "real_volume": 0
    }
  ]
}
```

Fields:

- `source_id` (string, required): stable sender identity. Also scopes `/sync/latest`.
- `symbol` (string, required): `XAUUSD` only.
- `timeframe` (string, required): one of `M1 M5 M15 M30 H1 H4 D1 W1 MN1`.
- `clock_offset_seconds` (int, optional, additive): `(broker time − UTC)` in
  seconds. Clamped to `|offset| <= 18h`; an implausible value is treated as a
  stale feed and ignored, never trusted blindly.
- `candles` (array, required, bounded): closed candles only. The sender must not
  send the currently-forming bar.

Per-candle fields: `time` (epoch seconds, required), `open/high/low/close`
(required), `tick_volume`, `spread`, `real_volume` (optional; never fabricated —
omit rather than send 0 for a field the source does not provide).

## 3. Response

`200 OK` with the standard envelope:

```json
{
  "api": "astra",
  "schema": 1,
  "data": {
    "source_id": "sender-pc-1",
    "symbol": "XAUUSD",
    "timeframe": "M15",
    "received": 1792045900,
    "accepted":   [{"identity": "symbol=XAUUSD&timeframe=M15&open_time=1792045800", "state": "stored_new"}],
    "duplicates": [{"identity": "...", "state": "already_stored"}],
    "rejected":   [{"state": "rejected", "reason": "candle OHLC ordering violated"}],
    "ingested": 1
  }
}
```

`ingested` counts only rows **persisted** this call. A row already present is a
`duplicate` (`already_stored`) and does not increment `ingested` — this is the
persistence acknowledgment, distinct from mere HTTP receipt.

## 4. Status codes

| Code | Meaning |
|------|---------|
| 200 | Request handled; inspect `accepted`/`duplicates`/`rejected` |
| 400 | Malformed JSON or missing top-level fields |
| 401 | Missing/invalid sender bearer token |
| 413 | Request body exceeds `ASTRA_MAX_REQUEST_BYTES` |
| 429 | Rate limit exceeded (`Retry-After: 60`) |
| 500 | Persistence failure (transaction rolled back) |

Error body: `{"api":"astra","schema":1,"error":{"code":"...","message":"..."}}`.
No stack traces or internal details are returned.

## 5. Sync / reconciliation

`GET /api/v1/sync/latest?source_id=<id>` returns, per `(symbol, timeframe)`, the
authoritative `last_open_time_utc` and `count` **for that source**. A sender must
scope by its own `source_id`; another sender's watermark is not delivery proof.

## 6. Retry and idempotency

- Safe to retry any batch: identity is the primary key, replays yield
  `already_stored`.
- Retry only on 5xx / 429 / transport errors, with bounded exponential backoff.
- Treat 400 as terminal (fix the payload); do not retry.
- Recommended batch size: <= 1000 candles; the server caps the body size.

## 7. Environment variables (server side)

| Variable | Required | Purpose |
|----------|----------|---------|
| `ASTRA_SENDER_TOKEN` | yes | bearer token accepted for ingest/sync |
| `ASTRA_ADMIN_TOKEN` | no | separate credential for admin routes |
| `ASTRA_MAX_REQUEST_BYTES` | no | body cap (default 1 MiB) |
| `ASTRA_INGEST_REQUESTS_PER_MINUTE` | no | per-credential rate limit |
| `ASTRA_DATA_DIR` | no | SQLite location (Railway volume) |

## 8. Local verification (mock sender)

`tests/NewsApiIntegrationTests.cpp` and `tests/AstraApiServerTests.cpp` drive the
real server with mock batches. A standalone mock sender is provided at
`tools/ayzr_agents/mock_sender.py` for manual end-to-end checks against a local
instance:

```bash
ASTRA_SENDER_TOKEN=dev-token ./build-linux/astra_server --data-dir /tmp/astra-demo --port 8790 &
python3 tools/ayzr_agents/mock_sender.py --port 8790 --token dev-token
```

The script sends a valid batch, replays it (expecting `already_stored`), and
sends an invalid OHLC row (expecting `rejected`). It never contacts production.
