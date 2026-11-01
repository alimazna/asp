# biquote → ASTRA link

`biquote_sender.py` connects the ASTRA backend to **biquote**
(<https://biquote.io>), the approved real XAUUSD market-data source.

## What it is

A real candle sender. It fetches live OHLC bars from biquote (public, keyless)
and posts the **closed** bars to the existing ASTRA receiving API — the same
contract `mock_sender.py` exercises. `mock_sender.py` stays synthetic and
test-only; this one carries real prices.

## Rules it honours

- **Closed bars only.** The in-progress bar (`isOpen: true`) is never sent, so
  nothing future-dated can reach the analysis path.
- **UTC timestamps, verbatim.** `clock_offset_seconds` is `0`: biquote already
  stamps in UTC, so there is no broker-clock shift to correct (unlike MT5).
- **Own source identity.** `source_id="biquote"`, so its rows never collide
  with the MT5 sender's — candle identity is
  `(source_id, symbol, timeframe, open_time_utc)`.
- **Honest volume.** biquote `volume` is always 0 for FX/CFD, so it is not
  sent; `tickVolume` (real activity) is sent as `tick_volume`.
- **No secrets.** The API needs no key; nothing is stored or embedded.

## Timeframes

biquote exposes 7 intervals: `1m, 5m, 15m, 30m, 1h, 4h, 1d` → ASTRA
`M1, M5, M15, M30, H1, H4, D1`. **W1 and MN1 are not offered by biquote**
(verified: it rejects `1w`/`1mo`), so MT5 remains their source. Reads for W1/MN1
honestly return `no candle data available`.

## Behaviour

- **First run:** backfills `--backfill` bars (default 100) per timeframe.
- **Later runs:** sends only bars newer than the last one already delivered for
  that timeframe (watermark in `--state`, a JSON file). A restart or outage
  catches up instead of leaving a permanent gap.
- **Delivery proof:** the watermark advances only on a clean accepted batch;
  a rejected or failed batch is retried, never skipped.

## Usage

```bash
# One shot against a local server
ASTRA_SENDER_TOKEN=dev-token ./build-linux/astra_server \
    --data-dir /tmp/astra-demo --port 8790 &
python3 tools/ayzr_agents/biquote_sender.py --port 8790 --token dev-token

# Continuous, poll every 30s
python3 tools/ayzr_agents/biquote_sender.py --port 8790 --token dev-token \
    --loop --interval-seconds 30 --state /tmp/biquote_state.json

# Dry run: fetch and print, never post
python3 tools/ayzr_agents/biquote_sender.py --dry-run --backfill 5
```

Posting to a non-loopback host (e.g. the Railway backend) additionally needs
`--allow-remote`.

## Verified (live, 2026-10-30)

- 700 real bars ingested (100 × 7 timeframes), all `HTTP 200`, `rejected=0`.
- Replay → `duplicates`, `ingested=0` (idempotent).
- Read-back: 100 bars per timeframe for the 7 supported; W1/MN1 empty.
- Live catch-up: newest stored M1 advanced `07:33 → 07:36` over a 150 s loop.
- Open-bar exclusion: biquote's open bar `07:34` was **not** stored.
- `mock_sender.py` still PASSES (no regression).
