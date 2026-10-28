# AUDIT-T42 — chronological ordering defeats batch grouping

**Date:** 2026-10-28
**Component:** `windows-collector` (`engine._group_batches`, `planner`)
**Severity:** low — performance only, no data loss or corruption
**Status:** open, not fixed

## Discovery

Found during the live test against `tools/hiding_receiver.py`. The receiver
accepted 2,628 candles across 1,227 HTTP requests. With `BATCH_SIZE = 50` the
ideal is ~53 requests, so the sender made **23x more requests than necessary**.

Per-timeframe churn:

| TF | candles | posts | candles/post | posts with sent=1 |
| --- | --- | --- | --- | --- |
| MN1 | 268 | 72 | 3.7 | 68 |
| W1 | 300 | 116 | 2.6 | 60 |
| D1 | 300 | 108 | 2.8 | 55 |
| H4 | 300 | 125 | 2.4 | 88 |
| H1 | 300 | 191 | 1.6 | 150 |
| M30 | 299 | 228 | 1.3 | 157 |
| M15 | 298 | 198 | 1.5 | 98 |
| M5 | 295 | 135 | 2.2 | 55 |
| M1 | 268 | 54 | 5.0 | 0 |

## Root cause

Two correct decisions combine into a bad outcome.

1. `plan_delivery` sorts the queue chronologically:
   `plan.queue.sort(key=lambda k: k.sort_key())`, and `sort_key` is
   `(open_time_utc, rank)`. This is deliberate — the receiver sees one
   chronological stream.

2. `_group_batches` starts a new batch whenever the stream changes:
   `if current and (stream != current_stream or len(current) >= BATCH_SIZE)`.

Because nine timeframes are interleaved by timestamp, the stream changes on
almost every candle. Batches fill to 1 instead of 50.

Simulated on a 2,700-candle queue (9 timeframes, 300 bars each):

```
candles in queue      : 2700
batches produced      : 1197 (BATCH_SIZE=50)
batch-size histogram  : {1: 700, 2: 178, 3: 84, 4: 83, 5: 79, 6: 41, 7: 27, 30: 1, 50: 4}
ideal if grouped by tf: 54
overhead factor       : 22.2x
```

700 batches carry a single candle. Only 4 reach the cap of 50.

The "one request to one stream" rule in `_group_batches` is not incidental: the
ingest endpoint takes a single `symbol` and `timeframe` per request, so a batch
genuinely cannot span streams. The batching is therefore correct as written;
what is wrong is the expectation that chronological ordering and stream
grouping can coexist.

## Impact

Performance and cost only. No candle is lost, duplicated, or mis-ordered, and
the receiver's dedupe held perfectly (zero duplicates across 1,227 requests).

The practical cost on the live test was throughput: a first-contact prime of
2,628 candles took roughly 25 minutes instead of about one. On a metered
connection or a slow link this is a 23x tax on the initial sync, and it delays
the receiver reaching a usable state.

It also distorts the progress display: M30 needed 228 requests for 299 candles,
so the UI's request counter races ahead of the candle count.

## Suggested fix (not applied)

Batch per stream first, then order the *batches* chronologically rather than
interleaving individual candles. Concretely: group each stream's candles into
batches of up to `BATCH_SIZE`, then sort the batch list by the first candle's
`sort_key`. This keeps the receiver's chronological view at batch granularity
while letting every request carry a full 50 candles.

The existing guarantee to preserve is that the receiver never sees a stream's
later candle before an earlier one, and that a single request never spans two
streams. Both hold under the fix.

## Verification

```bash
cd windows-collector
python3 -c "
import sys; sys.path.insert(0,'.')
from collector.engine import _group_batches, BATCH_SIZE
from collector.identity import CandleKey
tfs=['M1','M5','M15','M30','H1','H4','D1','W1','MN1']
step={'M1':60,'M5':300,'M15':900,'M30':1800,'H1':3600,'H4':14400,'D1':86400,'W1':604800,'MN1':2592000}
base=1800000000
q=[CandleKey('XAUUSD',tf,base-i*step[tf]) for i in range(300) for tf in tfs]
q.sort(key=lambda k:k.sort_key())
b=_group_batches(q)
print(len(q),'candles ->',len(b),'batches')
"
```

Expected once fixed: ~54 batches for 2,700 candles.
