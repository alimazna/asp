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

## Installing (recommended)

For a normal Windows install that requests permissions (UAC) and keeps its
files inside the system:

1. Download **`AstraSenderSetup-<version>.exe`** from the releases page.
2. Double-click it and accept the UAC prompt.
3. Setup installs the app under `Program Files\AstraSender` and creates
   `%PROGRAMDATA%\AstraSender` (writable by all users) for the config and the
   SQLite ledger, plus Start Menu / desktop shortcuts and an uninstaller.

If a `config.json` sits **next to the setup program**, setup copies it into
`%PROGRAMDATA%\AstraSender\config.json` (only when none exists), so the app is
configured on first launch with no JSON editing. Otherwise configure it in-app
with the **Settings** button, or edit the file.

## Portable / single-file use

Prefer no install? Download `AstraSender.exe` and run it directly. It then
keeps its config and ledger under `%LOCALAPPDATA%\AstraSender`.

## Quick start (owner's Windows PC)

1. Have the **MetaTrader 5 desktop terminal** installed and logged in on this
   PC. No Python and no `pip install` are needed: the release `.exe` bundles
   the MetaTrader5 package, so you only need MT5 itself.

2. Download the executable and run it.

   > **Do not use `sender-v0.3.0`.** That build crashes on launch with
   > `ImportError: attempted relative import with no known parent package`,
   > because PyInstaller runs `astra_sender/__main__.py` as a top-level script
   > and the module used relative imports. The bug was masked by a `--version`
   > CI smoke test that returned before the imports ran. It is fixed in the
   > current source; the release below supersedes it.

   - **download the release asset** (verified PE, SHA256
     checksummed by the release workflow),
     built by the *Build Windows Astra Sender* workflow on a Windows runner: 
     `https://github.com/alimazna/asp/releases/download/sender-v0.4.2/AstraSender.exe`
   - or build it yourself (below),
   - or download `AstraSender.exe` from the latest GitHub Actions artifact
     (`Actions` tab → workflow *Build Windows Astra Sender* → artifact).

3. Let the app create the config. The **first launch writes a template config**
   pre-filled with the backend URL, symbol, the nine timeframes and a
   `sender_id`, then shows a message box telling you the exact path (default
   `%LOCALAPPDATA%\AstraSender\config.json`). Open it and paste the dedicated
   ingestion token into `sender_token` — that is the only field you must
   supply:

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

## Troubleshooting

**Double-clicking does nothing at all (no window, no error).**
Until v0.4.1 this was the app's most confusing failure. The exe is built as a
*windowed* app (`console=False`), so it has no console and `sys.stdout` /
`sys.stderr` are `None`. When the config was missing, the old code printed the
"not configured" message to `stderr` — which in windowed mode is discarded — so
the process exited instantly and invisibly.

v0.4.1 fixes this three ways:

- a startup **message box** tells you what is wrong and where the config lives;
- every startup failure is appended to
  `%LOCALAPPDATA%\AstraSender\sender.log`;
- an unexpected crash is logged and shown in a dialog instead of vanishing.

If you are on v0.4.0 and see nothing, check for `sender.log` next to the config
path; if it is absent, the process died before logging — run the exe from
`cmd` once (it still prints to a real console when one is attached) to see the
message.

**Nothing happens, or the window closes instantly.**
Look at `%LOCALAPPDATA%\AstraSender\sender.log`. Each of these has a distinct
message:

| Log message | Cause | Fix |
|---|---|---|
| `not configured` / `Sender token is empty` | `config.json` incomplete | paste the ingestion token into `sender_token` |
| `AUTH_FAILED` / `server rejected credential` | wrong/rotated token | put the current token in `config.json` |
| `MT5` / `UNAVAILABLE` | terminal not running or not logged in | open and log in to MT5 |
| `cannot reach server` | no internet or wrong `backend_url` | check the URL and connection |
| `MetaTrader5: NOT bundled` | exe built without the MT5 package | use the release exe, not a manual build |

**Antivirus flags or deletes the exe.** One-file PyInstaller executables are
frequently false-positive flagged because they self-extract at runtime. Add an
exclusion for `AstraSender.exe`, or build the onedir variant. The exe is UPX
compressed, which increases false positives; build with `upx=False` if your AV
is strict.

**SmartScreen blocks the first launch.** The exe is unsigned, so Windows shows
"Windows protected your PC". Choose *More info → Run anyway*. Signing the
binary is the only way to remove this prompt.

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