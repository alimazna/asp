# ASTRA Windows Data Sender

Reads **closed** XAUUSD candles from the user's existing MetaTrader 5 desktop
terminal and pushes them over authenticated HTTPS to the deployed ASTRA
backend. It is a **shadow, read-only** market-intelligence sender: it never
requests or executes orders and never transmits MT5 account passwords.

## What it does

- Connects to the already logged-in MT5 terminal session (first terminal found;
  no broker password prompt).
- Resolves the broker's XAUUSD symbol (`XAUUSD`, `GOLD`, suffixes, etc.) and
  shows the resolved name — it never silently uses an unrelated instrument.
- Reads the latest **closed** candle on the nine supported timeframes
  (`M1 M5 M15 M30 H1 H4 D1 W1 MN1`) using the real bridge
  (`bridge/mt5_python/mt5_client.py`), whose `start_pos=1` semantics guarantee
  the forming bar is never submitted.
- Keeps a durable SQLite acknowledgment ledger (per symbol/timeframe/open-time)
  so already-acknowledged candles are never re-sent, uncertain deliveries are
  retried with the same idempotency identity, and the sender resumes cleanly
  after network/terminal/computer restarts.
- Performs **bounded, chronological catch-up** when a gap exists between the
  last server watermark and the newest closed candle.
- Shows connection status, the last read/acked candle per timeframe, counters
  (sent / acked / dedup / queued / failed), a local event log, pause/resume,
  and reconnect in a lightweight ttk GUI.

## Quick start (owner's Windows PC)

1. Install **Python 3.10+** (the standard Windows installer) and the
   MetaTrader5 package:

   ```bat
   py -m pip install MetaTrader5
   ```

2. Get the executable.

   > **Do not use `sender-v0.3.0`.** That build crashes on launch with
   > `ImportError: attempted relative import with no known parent package`,
   > because PyInstaller runs `astra_sender/__main__.py` as a top-level script
   > and the module used relative imports. The bug was masked by a `--version`
   > CI smoke test that returned before the imports ran. It is fixed in the
   > current source; the release below supersedes it.

   - **download the release asset** (verified PE, SHA256
     checksummed by the release workflow),
     built by the *Build Windows Astra Sender* workflow on a Windows runner: 
     `https://github.com/alimazna/asp/releases/download/sender-v0.4.0/AstraSender.exe`
   - or build it yourself (below),
   - or download `AstraSender.exe` from the latest GitHub Actions artifact
     (`Actions` tab → workflow *Build Windows Astra Sender* → artifact).

3. Create the config file. On first run the app prints where it looks for
   `config.json` (default `%LOCALAPPDATA%\AstraSender\config.json`). Create it
   with these fields:

   ```json
   {
     "backend_url": "https://astra-server-production.up.railway.app",
     "sender_token": "<the-dedicated-ingestion-token>",
     "sender_id": "win10-desk-01",
     "symbol": "XAUUSD",
     "timeframes": ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"],
     "poll_interval_seconds": 30,
     "catchup_max_bars": 200,
     "request_timeout_seconds": 30
   }
   ```

   Protect `config.json` with NTFS ACLs for the current user only.

4. Double-click `AstraSender.exe`. The MT5 terminal must be running and logged
   in. The GUI shows the resolved symbol and, once the server accepts, ack
   counts and the last ack time.

5. On first install the sender sends the **newest closed candle per timeframe
   only** — it never floods months of history. Historical backfill must be
   explicitly configured (documented below).

## Headless mode

```bat
AstraSender.exe --console --config %LOCALAPPDATA%\AstraSender\config.json
```

## Building the executable

On a Windows machine (or the CI workflow):

```bat
cd windows-sender
py -m pip install pyinstaller
py -m PyInstaller --clean --noconfirm astra_sender.spec
REM artifact: dist\AstraSender.exe
```

The repo's GitHub Actions workflow
`.github/workflows/sender-windows-build.yml` builds and uploads the single-file
executable on every `sender-*` tag (and on manual dispatch).

## Config file reference

| Key | Default | Meaning |
| --- | --- | --- |
| `backend_url` | *(required)* | HTTPS base URL of the ASTRA server. |
| `sender_token` | *(required)* | Dedicated ingestion credential. |
| `sender_id` | *(required)* | Stable instance identity (used in idempotency keys). |
| `symbol` | `XAUUSD` | Requested instrument; broker-resolved at startup. |
| `timeframes` | all nine | Timeframes to sync. |
| `poll_interval_seconds` | `30` | Live loop interval. |
| `catchup_max_bars` | `200` | Upper bound per timeframe for catch-up history. |
| `catchup_bar_limit` | `1000` | Max bars read from MT5 per catch-up read. |
| `request_timeout_seconds` | `30` | HTTPS request timeout. |
| `ledger_path` | `%LOCALAPPDATA%\AstraSender\ledger.sqlite` | Override ledger location. |

## Delivery guarantees

- **Identity** = `sender_id | symbol | timeframe | open_time_utc` (stable
  hash), independent of price values.
- **Acknowledgment** is recorded only after the server reports
  `stored_new`/`already_stored` for the exact candle identity.
- **Timeout / lost response** → the delivery stays `pending`; the retry reuses
  the same identity, so the server-side unique constraint turns the replay
  into a harmless duplicate (never a second logical candle).
- **Server-side dedup** is enforced by a database unique constraint on
  `(source_id, symbol, timeframe, open_time)` in `CandleStore`.
- **Rejected** candles are marked `failed` with the server reason — never
  silently marked delivered.
- **Catch-up** reads a bounded chronological window from MT5 and filters out
  everything already acked; progress is per-candle.
- The app makes **no trading requests**. It only calls the documented ingestion
  and (optionally) sync-reconcile endpoints over HTTPS.

## Security notes

- The config holds the dedicated sender token (never broker passwords). It
  lives in `%LOCALAPPDATA%` and must be ACL-protected.
- Secrets are not stored in the SQLite ledger, never logged, and never placed
  in the frontend bundle (the browser never sees them).
- The sender never listens on a public port.

## Testing

The Linux-friendly tests use the repo's `tests/integration/fake_mt5` stand-in
for the Windows-only MetaTrader5 package and a loopback stub server:

```bash
cd windows-sender
python -m unittest discover -s tests -v
```

They cover: all nine timeframe mappings, closed-candle-only reads, OHLC
validation, ledger durability/restart/idempotency, dedup after restart,
uncertain-delivery retry with a stable identity, and bounded chronological
catch-up. These are automated tests using test data — they never touch a live
terminal or the production backend.