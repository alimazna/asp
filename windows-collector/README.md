# ASTRA Collector

A standalone Windows application that reads **closed** candles from an existing
MetaTrader 5 terminal and delivers them to a receiving endpoint over HTTPS.

This app is independent. It has no server component and no dependency on the
ASTRA backend: the receiving endpoint is a setting, not a compiled-in constant.
It is designed to run alongside other systems, and it only ever sends data — it
never places, modifies, or cancels an order.

- Reads nine timeframes: M1, M5, M15, M30, H1, H4, D1, W1, MN1.
- Gives every candle a deterministic unique id.
- Stores every candle locally in one durable file.
- Sends only what the receiving side has not already confirmed.
- Never sends the same candle twice.
- Resumes correctly after a restart, a terminal restart, or a network outage.

## The five guarantees

1. **A unique id per candle.** `symbol:timeframe:open_time@hash`. It is derived
   from *when* the candle opened, never from its prices, so two candles with
   identical OHLC still get different ids. Both sides compute the same id from
   the same three inputs.
2. **One local store for all nine timeframes.** A single SQLite file holds every
   candle, keyed by its unique id. A repeated write of the same id is a no-op,
   so a captured candle is never mutated or duplicated.
3. **Sequential, non-mixing send tracking.** A delivery ledger records, per
   candle id and per destination, whether it is pending, acknowledged, or
   rejected. Acknowledged candles are never re-sent.
4. **Gap-based first run and catch-up.** On first contact the receiver is asked
   what it holds; the difference is computed and exactly the missing candles are
   sent, in chronological order. After a shutdown the collector re-reads the
   candles it missed and repairs the hole, walked back in bounded pages. With the
   default limits this covers about 69 days of M1 downtime, or years of M5 and
   above; the real ceiling is the broker's own history depth. See
   `docs/PROTOCOL.md` for the table.
5. **Safe destination changes.** Confirmations are tracked *per destination*. A
   new receiver has confirmed nothing, so it receives everything it lacks. The
   app warns before this happens and never silently treats a new endpoint as
   already caught up.

## What it does not do

- No live trading and no order execution of any kind.
- No fabricated data. A value that is not available is shown as a dash and
  stored as null, never invented.
- No inbound connection to your PC. The app only makes outbound HTTPS requests.
- No broker password. It uses the MetaTrader 5 terminal that is already logged
  in and never asks for or transmits account credentials.

## Install and run

See [docs/SETUP.md](docs/SETUP.md) for the owner-facing instructions, and
[docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire contract the receiving side
must implement.

## Layout

```
windows-collector/
  collector/            application package (standard library only)
    identity.py         unique ids, timeframes, gap computation
    store.py            local candle store (one SQLite file)
    ledger.py           delivery ledger (sent vs acknowledged, per destination)
    planner.py          what to send, in what order
    receiver.py         HTTPS client for the receiving endpoint
    engine.py           the sync loop
    mt5_reader.py       MetaTrader 5 reader, closed candles only
    config.py           configuration and token handling
    cli.py              command-line entry point
    gui.py              desktop window
    display.py          presentation helpers (no toolkit import)
  tests/                test suite; only MT5 and the receiver are doubled
  packaging/            PyInstaller spec and Windows build script
  astra_collector_app.py  entry point for the packaged executable
```

## Tests

```
cd windows-collector
python -m unittest discover -s tests -v
```

The suite runs on Linux and Windows. The MT5 terminal and the receiving
endpoint are replaced by test doubles; the application's own logic —
identity, storage, ledger, planning, delivery, retry, and reconciliation — runs
for real. No synthetic candle is ever written to a real store.
