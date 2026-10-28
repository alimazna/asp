# AUDIT-T41 — a receiver that reports a stream empty is not re-sent

**Date:** 2026-10-28
**Component:** `windows-collector` (planner, engine)
**Severity:** medium — silent data loss on the receiver after a partial loss
**Status:** FIXED in 1.0.7 — see "Resolution" at the end

## What was asked

Build a receiver that accepts candles for every timeframe but deliberately
answers `GET /candles` with an empty list for three of them (`M15`, `M5`, `M1`),
to check that the collector re-sends what the receiver claims not to hold.

Tool added: `windows-collector/tools/hiding_receiver.py`.

## What the reproduction showed

The receiver behaved exactly as designed. Its log:

```
POST /candles/ingest XAUUSD M15 [HIDDEN STREAM] sent=1 new=1 dup=0 rejected=0
POST /candles/ingest XAUUSD M1  [HIDDEN STREAM] sent=4 new=4 dup=0 rejected=0
GET  /candles XAUUSD M15 -> REPORTED EMPTY (holding 4, hidden)
GET  /candles XAUUSD H1  -> 4 bar(s)
```

So the receiver held four M15 candles while telling the collector it held none.

The collector's second pass then queued **nothing at all**:

```
=== PASS 2: nothing changed, receiver reports M15/M5/M1 empty ===
  queued: 0 {}
  re-sent hidden streams : []
  re-sent visible streams: []
RESULT: FAIL
```

The three streams the receiver reported empty were **not** re-sent.

## Root cause

`plan_delivery` computes the delivery set as

```python
have_remote = set(remote) | set(acked)
stream.to_send = sorted(set(local) - have_remote)
```

`acked` is the local ledger, which already recorded these candles as delivered
during the prime. Because the union includes the ledger, a receiver that
**reports a stream empty** and a receiver that **cannot be reached** produce the
same result: the ledger wins and the stream is suppressed.

The conflation is visible at the interface. `_receiver_open_times()` returns a
plain `Dict[tuple, List[int]]` and returns `{}` on any `ReceiverError`, so the
planner receives no signal distinguishing:

| Situation | `remote` | Intended outcome | Actual outcome |
| --- | --- | --- | --- |
| receiver unreachable | `{}` | trust the ledger, do not re-send | correct |
| receiver reachable, stream empty | `{(sym,tf): []}` | re-send, the receiver lost it | **suppressed** |

Confirmed with a direct probe: an unreachable receiver and an empty-but-
reachable stream both present to the planner as `[]`.

The ledger fallback is correct and deliberate for case 1 — it is documented in
`plan_delivery` and covered by
`test_acknowledged_fallback_suppresses_resend_when_receiver_unreachable`. The
defect is that the same fallback also applies to case 2, where it is wrong.

## Impact

If the receiver loses a stream — a restored database, a bad migration, an
operator deleting rows, a partial restore from backup — the collector will not
repair it. The gap persists silently and nothing reports it. The owner sees a
healthy collector and a receiver missing data.

This is the mirror image of the failure the `_reconcile` path was written to
prevent: that path trusts the receiver when it reports holding *more*; this
path fails to distrust the ledger when the receiver reports *less*.

## Why the existing tests did not catch it

`test_nothing_planned_when_receiver_has_everything` covers remote ⊇ local.
`test_acknowledged_fallback_suppresses_resend_when_receiver_unreachable` covers
the unreachable case. No test covers *reachable, authenticated, and explicitly
reporting a stream it previously held as empty*. That is the untested quadrant.

## Suggested fix (not applied)

Distinguish "no answer" from "answered, empty". `_receiver_open_times()` should
signal reachability separately from inventory — return `None` (or raise a
sentinel) for a failed query and an empty list only for a real empty answer.
The planner then applies the ledger fallback **only** when the receiver did not
answer, and treats an authoritative empty answer as proof of absence, so the
stream is re-planned and re-sent.

Whatever is chosen must keep both existing guarantees:

1. a lost response must not cause a resend storm while the receiver is down;
2. a receiver that genuinely lost data must be repaired automatically.

## Reproduction

```bash
cd windows-collector
python3 tools/hiding_receiver.py --port 12001 \
    --token <TEST-TOKEN-REDACTED> --hide M15,M5,M1
# second terminal
python3 /tmp/hide_proof.py     # see AUDIT-T41 notes for the script
```

Expected once fixed: pass 2 queues candles for M15/M5/M1 and none for the six
streams the receiver reports holding.

## Resolution (1.0.7)

The root cause was that reachability and inventory were collapsed into one
signal. `ReceiverClient.remote_open_times` now returns `None` when the receiver
could not be asked and `[]` when it answered with nothing, and
`SyncEngine._receiver_open_times` records the answered-empty streams in
`_empty_streams`. `plan_delivery` takes that set and, for those streams only,
drops the ledger from `have_remote`:

```python
authoritative_empty = (symbol, timeframe) in empty_streams
have_remote = set(remote) if authoritative_empty else (set(remote) | set(acked))
```

Three further defects were fixed in the same pass, all found while tracing this
one:

1. **One bad stream blinded the other eight.** `_receiver_open_times` returned
   `{}` for the whole pass if any single query raised, so reconciliation was
   silently disabled for every timeframe. Each stream is now queried
   independently.
2. **An empty stream was misread as a transport failure.** The real ASTRA
   server answers a stream it holds nothing for with HTTP 503
   `dependency_unavailable`, not 200 with an empty array. That 503 is now
   mapped to an authoritative empty answer (and only on the candle query).
3. **The server status stuck on STARTING.** The receiver probe ran after
   `_ensure_reader()`, so a missing MT5 terminal raised first and the server
   was never contacted, leaving a reachable server displayed as STARTING. The
   probe now runs before the terminal is touched.

### Why the naive fix was not used

Making `remote` unconditionally authoritative would have caused a resend storm.
The real server clamps `?limit=` to 500 (`AstraApiServer.cpp`: `if (limit < 1
|| limit > 1000) limit = 500;`) and `CandleStore::recent` returns the *oldest*
rows (`ORDER BY open_time ASC LIMIT ?`). A stream with more than 500 candles
therefore answers with a truncated page, and treating that page as complete
would re-send every candle older than the page on every poll.

Restricting authority to the empty answer avoids this: truncation cannot
produce an empty page, so an empty answer is unambiguous evidence of loss.

### Residual limitation (server-side change required)

A *partial* loss — the receiver keeps some candles of a stream but loses others
— still cannot be detected while the server returns a truncated oldest-first
page. Detecting it needs a server-side change: a `?since=` cursor or a
pagination token, so a sender can walk the full inventory instead of a single
page. Until then the sender repairs total loss of a stream and nothing finer.

### Verification

```
windows-collector/tools/t41_verify.py   # the original scenario, now passing
```

Result after the fix:

```
acked per tf: {M1: 50, M5: 50, M15: 50, M30: 50, H1: 50, H4: 50, D1: 50, W1: 50, MN1: 50}
--- receiver now LOSES M1/M15/M5 (reports empty) ---
re-sent by tf: {'M15': 50, 'M5': 50, 'M1': 50}
RESULT: PASS - hidden streams re-sent
healthy streams dragged in: none
```

Covered by `tests/test_engine.py::ReceiverEmptyStreamTest` (5 tests) and
`tests/test_core.py::PlannerTest` (4 new tests). The full suite is 148 tests,
all passing.
