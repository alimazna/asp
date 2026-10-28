"""A copy-paste brief that describes the receiving side of the contract.

The owner can hand this text to whoever builds the receiver — a developer, a
different agent, or a future session — without knowing the protocol themselves.
It is generated from the same constants the client uses, so it cannot drift
from what the collector actually sends.

Deliberately self-contained: it names every field, the exact failure each
missing field causes, and the deduplication rule. A receiver built from this
text alone interoperates.
"""

from __future__ import annotations

from typing import List

from .identity import TIMEFRAMES
from .receiver import CANDLES_PATH, HEALTH_PATH, INGEST_PATH, SYNC_PATH

#: Version of the brief itself, so a receiver author can say which revision
#: they built against.
BRIEF_VERSION = "1.0"

#: Batches the client actually sends. Stated so a receiver sizes its limits
#: to reality rather than guessing.
MAX_BATCH_CANDLES = 50


def build_receiver_brief(base_url: str, sender_id: str, symbol: str,
                         clock_offset_seconds: int = 0) -> str:
    """Render the brief for one concrete deployment.

    The placeholders are filled with the values the collector is configured
    with, so the reader sees a request they can reproduce with curl instead of
    a schema they have to interpret.
    """
    base = (base_url or "").strip().rstrip("/") or "https://YOUR-RECEIVER/api/v1"
    sender = (sender_id or "").strip() or "win10-desk-01"
    instrument = (symbol or "").strip() or "XAUUSD"
    offset = int(clock_offset_seconds or 0)
    timeframes = " ".join(TIMEFRAMES)

    return f"""\
# ASTRA Collector -> receiver brief (v{BRIEF_VERSION})

You are building the receiving side for an ASTRA Collector. The collector runs
on a Windows PC next to a MetaTrader 5 terminal and sends **closed** XAUUSD
candles to you over HTTPS. It never sends orders and never opens an inbound
port: every connection is outbound from the collector.

Configured for this deployment:

    base URL          {base}
    sender id         {sender}
    symbol            {instrument}
    broker UTC offset {offset} seconds
    timeframes        {timeframes}

## Authentication

Every request except the liveness probe carries a bearer token:

    Authorization: Bearer <token>

Answer **401** for a missing or wrong token. The collector treats 401/403 as an
authentication failure and records **no** candle as delivered, so a wrong token
must never be answered with 200.

## Endpoints

### GET {HEALTH_PATH}

No authentication. Liveness only.

    200 {{"data": {{"status": "ok"}}}}

### GET {CANDLES_PATH}?symbol={instrument}&timeframe=<tf>&limit=<n>

What you currently hold for one stream. The collector uses it to detect gaps
after a restart, so an empty stream must be **200 with an empty list**, never
404.

    200 {{"data": {{"bars": [{{"time": 1792432800, "open": 2000.0, "high": 2001.0,
                            "low": 1999.0, "close": 2000.5,
                            "tick_volume": 120, "spread": 20,
                            "real_volume": 0}}],
                      "symbol": "{instrument}", "timeframe": "M15",
                      "count": 1}}}}

The collector also accepts a bare `open_times` array or a generic `candles`
array, so any of the three shapes works.

### GET {SYNC_PATH}?source_id={sender}

Authenticated. What you hold for **this sender only**, per stream. The collector
calls it to verify a token without sending a candle, and to reconcile its local
ledger after a local database problem.

    {{
      "data": [
        {{"symbol": "{instrument}", "timeframe": "M15",
          "last_open_time_utc": 1792432800, "count": 120}}
      ]
    }}

Scope the answer to `source_id`. Without that parameter an aggregate across
every sender is returned, which is fine for a diagnostic but is **not** proof
that this sender's candle was persisted, so the collector must not treat an
unscoped answer as delivery confirmation. Answer an empty list when you hold
nothing for that sender; do not answer 404.

### POST {INGEST_PATH}

Deliver a batch from one stream.

    {{
      "schema_version": "1.0",
      "source_id": "{sender}",
      "symbol": "{instrument}",
      "timeframe": "M15",
      "clock_offset_seconds": {offset},
      "candles": [
        {{
          "uid": "{instrument}:M15:1792432800@2066ed6de246f92a",
          "symbol": "{instrument}",
          "timeframe": "M15",
          "time": 1792432800,
          "open": 2000.0, "high": 2001.0, "low": 1999.0, "close": 2000.5,
          "tick_volume": 120, "spread": 20, "real_volume": 0,
          "source": "BrokerName"
        }}
      ]
    }}

Three details decide whether this works at all. Each one fails in a way that
looks like success from a distance:

1. **The open time field is `time`, not `open_time_utc`.** `open_time_utc` is
   the collector's internal name. A receiver that reads only `open_time_utc`
   answers 200 and rejects every candle; a sender that only checks the status
   code then believes it succeeded.

2. **`source_id` is required in the envelope.** Validate it before reading the
   candles and answer
   `400 {{"error": true, "code": "missing_fields", "message": "..."}}` when it
   is absent.

3. **`clock_offset_seconds` is the broker's UTC offset, in seconds.** MT5
   stamps bar open times in broker server time, so a bar from a UTC+3 broker
   carries a stamp three hours ahead of UTC. Apply the offset before rejecting
   a candle as future-dated. Without it, every M1..H1 bar whose open time falls
   inside the offset window is refused while D1/W1/MN1 pass, so the sender
   looks partly healthy while the intraday data never arrives.

## Response

Name every candle you accepted or already had. Answer:

    {{
      "data": {{
        "received": 1792432800,
        "accepted":   [{{"identity": "symbol={instrument}&timeframe=M15&open_time=1792432800",
                        "state": "stored_new"}}],
        "duplicates": [{{"identity": "symbol={instrument}&timeframe=M15&open_time=1792432740",
                        "state": "already_stored"}}],
        "rejected":   [{{"identity": "symbol={instrument}&timeframe=M15&open_time=1792432680",
                        "state": "rejected", "reason": "invalid ohlc"}}]
      }}
    }}

Echoing the sender's `uid` back instead of `identity` also works — the
collector accepts either dialect. What matters is that **every candle in the
request is named in `accepted`, `duplicates`, or `rejected`**. A candle named
in none of them stays pending and is retried, which is safe but delays
delivery; a candle that is silently dropped is a permanent gap.

An HTTP 200 whose body names nothing confirms nothing. The collector records a
candle as delivered **only** when you name it in `accepted` or `duplicates`.

## Idempotency

Put a uniqueness constraint on `(source_id, symbol, timeframe, open_time)` —
or on `uid`. A repeated request for the same candle must create no second
record and must trigger no duplicate downstream analysis; return it under
`duplicates` with `already_stored`.

This is what makes retry safe. The collector retries at least once whenever a
response is lost, and the network *will* lose responses after you have already
committed. Deduplication is the half of the guarantee that lives on your side:
together, at-least-once sending plus server-side uniqueness means each candle
is persisted and analysed exactly once logically.

## Validation to apply

Reject a candle — with a reason, in `rejected` — when:

- OHLC ordering is violated (`high < low`, or `high` below `open`/`close`);
- any price is not a finite number, or is negative;
- `time` is not a positive epoch timestamp;
- `time` is not aligned to the timeframe grid;
- `time` is in the future after applying `clock_offset_seconds`.

Treat volume fields as optional. Tick volume, spread, and real volume are not
available from every broker and may be 0; 0 is a real value, not a missing one.
Never invent a value for an absent field.

## Limits the collector respects

- at most {MAX_BATCH_CANDLES} candles per request;
- one stream (one symbol and timeframe) per request;
- chronological order within a batch;
- HTTPS only.

Size your request-body limit and rate limit above these. The collector sends on
a poll interval, not in a tight loop, so a modest rate limit is enough.

## What your receiver must never do

- Require the collector to open an inbound port.
- Ask the collector for broker credentials. It has none to give — it reads the
  already-logged-in terminal and never transmits an account password.
- Accept or act on a trade instruction. The collector sends candles only, and
  ASTRA is a shadow-only system with no order execution.
"""


def brief_placeholder_notes() -> List[str]:
    """Short lines shown next to the button, in the window."""
    return [
        f"protocol brief v{BRIEF_VERSION}",
        f"max {MAX_BATCH_CANDLES} candles per request",
        "open time field: time",
    ]


__all__ = [
    "BRIEF_VERSION",
    "MAX_BATCH_CANDLES",
    "build_receiver_brief",
    "brief_placeholder_notes",
]
