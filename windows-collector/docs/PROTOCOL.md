# Collector protocol

The contract between the ASTRA Collector and whatever receives its candles.
A receiving side must implement this to interoperate.

All requests are HTTPS. Every request carries `Authorization: Bearer <token>`.
A receiver that returns 401 or 403 is reported to the user as an authentication
failure and **no candle is recorded as delivered**.

## Candle identity

```
uid = f"{symbol}:{timeframe}:{open_time_utc}@{sha256(f'{symbol}|{timeframe}|{open_time_utc}')[:16]}"
```

Both sides compute this from `symbol`, `timeframe`, and `open_time_utc`. It is
never derived from prices. Use it as the uniqueness key.

## Timeframes

`M1 M5 M15 M30 H1 H4 D1 W1 MN1`

## 1. `GET /health`

Liveness. No authentication required.

```json
{ "data": { "status": "ok" } }
```

## 2. `GET /candles?symbol=<symbol>&timeframe=<tf>&limit=<n>`

What the receiver currently holds for one stream. Used for gap detection.

```json
{
  "data": {
    "candles": [ { "uid": "...", "open_time_utc": 1792432800, "...": "..." } ],
    "open_times": [ 1792379700, 1792378800 ]
  }
}
```

`open_times` is preferred and should be the complete, sorted list of open times
the receiver holds for that stream. If it is absent, the collector reads the
`candles` array instead. Both are scoped to the requested symbol and timeframe.

If the receiver holds nothing for the stream, return `200` with an empty list.
Do not return `404` for a stream that is merely empty.

## 3. `POST /candles/ingest`

Deliver a batch of candles from one stream.

Request:

```json
{
  "symbol": "XAUUSD",
  "timeframe": "M15",
  "candles": [
    {
      "uid": "XAUUSD:M15:1792432800@2066ed6de246f92a",
      "symbol": "XAUUSD",
      "timeframe": "M15",
      "open_time_utc": 1792432800,
      "open": 2000.0, "high": 2001.0, "low": 1999.0, "close": 2000.5,
      "tick_volume": 120, "spread": 20, "real_volume": 0,
      "source": "BrokerName", "schema_version": 1
    }
  ]
}
```

Response:

```json
{
  "data": {
    "received": 1792432800,
    "accepted":   [ { "uid": "...", "state": "stored" } ],
    "duplicates": [ { "uid": "..." } ],
    "rejected":   [ { "uid": "...", "reason": "invalid ohlc" } ]
  }
}
```

The collector records a candle as acknowledged **only** when the receiver names
its exact `uid` in `accepted` or `duplicates`. An HTTP 200 with an empty or
unnamed response confirms nothing, and the candle stays pending for retry.

`accepted` means newly persisted. `duplicates` means already persisted — this is
what makes a lost-response retry safe. Return one or the other for every candle
in the request, and put anything you refused in `rejected` with a reason.

## Idempotency

Enforce a uniqueness constraint on `uid`. A repeated request for the same candle
must not create a second logical record and must not trigger duplicate
downstream analysis. Return it under `duplicates`.

This is what makes the delivery guarantee work: the sender retries at least
once, and the receiver deduplicates, so each candle is persisted and processed
exactly once logically.

## Limits the sender respects

- Batches of 50 candles per request.
- One stream per request.
- Chronological order within a batch.
- Plain HTTP is refused unless explicitly enabled for local development.

## How much downtime the collector survives

After a restart the collector notices the candles it missed and re-reads them
from the terminal, as long as the broker still serves those bars. The default
limits allow this much downtime with no M1 candle lost:

| Timeframe | Downtime absorbed with defaults |
| --- | --- |
| M1 | about 69 days (100000 bars) |
| M5 | about 347 days |
| M15 | far beyond a year |

W1 and MN1 have no fixed bar length, so they are not checked arithmetically:
a missing weekly bar cannot be told from a market holiday. Instead the source
is asked which bars exist since the last stored one, and every bar it reports
that is missing locally is persisted. Only open times the source itself reports
are used, so no bar is invented and a holiday simply does not appear.

In practice the real limit is the broker's own M1 history depth, which is often
only a few months. When the broker no longer serves a missing bar, the gap is
reported in the window and in the event log; it is never silently ignored.

The relevant settings are `catchup_max_bars` (how far back a repair reaches)
and `catchup_bar_limit` (the largest single terminal read; a long repair is
walked in pages of this size, so memory stays flat no matter how long the
outage was). `max_plan_bars` bounds how far back the planner will deliver, but
does not limit repair: a hole is bounded by the candles on either side of it,
and nothing is stored inside it, so those neighbours stay adjacent whatever the
planner window is.

## What the receiver must not do

- Never require the collector to open an inbound port.
- Never ask the collector for broker credentials. It has none to give.
- Never accept or act on a trade instruction. The collector sends candles only.
