# Setup

Owner-facing instructions. Written so the application can be started normally,
without a command prompt and without installing a development toolchain.

> **Use 1.0.6 or later.** Releases 1.0.0 and 1.0.2 could not deliver a single
> candle: they omitted `source_id`, sent the bar open time under the wrong field
> name, and dropped the measured clock offset. The server rejected every batch
> (HTTP 400), or accepted it with `ingested: 0` and every candle in `rejected`.
> 1.0.3 fixed all three. 1.0.4 adds a first-run setup window that tests your
> connection before it saves it, a separate **Connect to MT5** button, and a
> copyable description of the receiving side. 1.0.5 makes a failed **Connect to
> MT5** click state the reason instead of leaving the label unchanged, and stops
> a timer error reaching the log when the window is closed. 1.0.6 keeps **Run
> setup again** reachable when the setup text is expanded on a small screen.

## 1. Requirements

- Windows 10 or later.
- MetaTrader 5 desktop terminal, installed and already logged in to the account
  that has the symbol you want to send.
- The receiving endpoint URL and its access token, supplied by whoever runs the
  receiving side.

You do **not** need Python, pip, or any build tool to run the packaged
executable.

## 2. Install

1. Download `ASTRA-Collector.exe` — direct link:
   <https://github.com/alimazna/asp/releases/download/collector-v1.0.6/ASTRA-Collector.exe>
   Release page: <https://github.com/alimazna/asp/releases/tag/collector-v1.0.6>
2. Put it in its own folder, for example `C:\Users\<you>\ASTRA-Collector\`.
   If you also copied `Start-ASTRA-Collector.cmd` from `packaging/`, keep it in
   the same folder; it is a one-click launcher.
3. Double-click `ASTRA-Collector.exe`. The window opens. No console appears.

The executable is self-contained: the MetaTrader5 package and its numpy backend
are bundled inside it, so there is nothing else to install.

Optional: verify the download before running it. In PowerShell:

```powershell
Get-FileHash .\ASTRA-Collector.exe -Algorithm SHA256
```

For the 1.0.6 release the expected SHA256 is:

```
BE64A3185563FC773279E250C0353DBDEC8AFA08F04B0C0856A037864B1DB1E4
```

Windows SmartScreen may warn about an unsigned application the first time. The
build is unsigned; choose "More info" then "Run anyway" only after the hash
matches.

### Command-line use (optional)

The same executable accepts arguments, which is useful for diagnosis:

| Command | Effect |
| --- | --- |
| `ASTRA-Collector.exe` | Opens the window (normal use). |
| `ASTRA-Collector.exe --version` | Prints the version and exits. |
| `ASTRA-Collector.exe --show-config` | Prints the effective settings, no token. |
| `ASTRA-Collector.exe --status` | Prints the current status and exits. |
| `ASTRA-Collector.exe --once` | Runs one synchronization pass and exits. |
| `ASTRA-Collector.exe --set-destination <url>` | Saves a new receiver URL. |
| `ASTRA-Collector.exe --selfcheck` | Imports every runtime module and exits. |

Run these from `cmd.exe` or PowerShell in the folder that holds the
executable. A windowed build has no console output of its own, so if startup
fails the reason is written to
`%LOCALAPPDATA%\AstraCollector\startup-error.log`.

## 3. Configure

On the very first run — when no destination and no token have been saved — a
setup window opens automatically. It asks for four values and **tests them
before it saves anything**:

| Field | Meaning |
| --- | --- |
| Receiver URL | The HTTPS endpoint that will receive the candles, including the API version, e.g. `https://your-backend.up.railway.app/api/v1` |
| Symbol | The instrument, e.g. `XAUUSD`. See symbol discovery below. |
| Sender id | A name for this computer, e.g. `desk-pc`. A sensible default is filled in. |
| Token | The access token for the receiver. |

Press **Test and save**. The app checks the receiver twice: first that the URL
answers, then that the token is accepted. If either fails it explains what went
wrong and saves nothing, so a mistyped token is caught here instead of looking
like a silent failure later.

To change any of these later, press **Run setup again**.

The token is stored in a separate file with owner-only permissions, not in the
configuration file and never in the source. Press **Reveal token** only if you
need to read it back; the window warns you first. You can paste with the right
mouse button if you prefer not to use Ctrl+V.

**Test connection** checks both the MetaTrader terminal and the receiver and
reports what each one said, using real responses.

### Connecting to MetaTrader

The app does not connect to your terminal on its own. Press **Connect to MT5**
when you want to. The button reports one of three real states:

| Shown | Meaning |
| --- | --- |
| connected (symbol @ broker) | The terminal answered and served a closed bar. |
| reachable, no closed bar | The terminal answered but had no closed bar to give. Check that the symbol is in Market Watch and that its history has downloaded. |
| not connected | The terminal is not running, not logged in, or the symbol was not found. The message says which. |

Press the button again at any time to re-check the live terminal. It becomes
**Check MT5** once connected.

## 4. Symbol discovery

Brokers name the instrument differently: `XAUUSD`, `GOLD`, or `XAUUSD` with a
suffix such as `XAUUSD.pro`. The app resolves the symbol against the terminal
and shows the resolved name in the status bar.

If the symbol is not found, the app reports it and sends nothing. It never
substitutes a different instrument. Enter the exact name the terminal uses; you
can see it in the terminal's Market Watch list.

## 5. Run

Press **Start**.

The window shows:

- **MT5** state and **Server** state, each coloured by its real condition.
- The resolved symbol and broker.
- The last successful synchronization time.
- Per timeframe: the last closed candle read, the last candle acknowledged by
  the receiver, how many candles are stored locally, and how many gaps remain.
- Counters: sent, acknowledged, deduplicated, queued, failed, rejected.
- An event log with real messages and recovery guidance.

**Pause** stops sending without closing the app; captured candles accumulate
locally and are sent when you resume. **Stop** ends the session cleanly and
flushes the local store. Closing the window stops the app the same way.

## 6. Where your data lives

Everything is under `%LOCALAPPDATA%\AstraCollector\`:

| File | Contents |
| --- | --- |
| `config.json` | Receiver URL, symbol, sender id, and limits. No secrets. |
| `token` | The access token. Owner-only permissions. |
| `candles.sqlite` | Every captured candle, all nine timeframes, keyed by unique id. |
| `deliveries.sqlite` | Delivery state per candle and per destination. |

**Open data folder** opens this directory.

Do not copy `token` into a shared location. If it is exposed, rotate it on the
receiving side and set the new value in the app.

## 7. First run and recovery

- **First run:** the app sends a bounded opening window of recent closed candles
  so the receiving side has enough history to work with immediately. It does not
  upload months of history.
- **After an outage:** the app compares its local store with the receiver,
  computes exactly what is missing, and fills it in chronological order.
- **A gap the broker no longer serves** is reported in the event log as an
  explicit gap. It is never silently skipped.
- **Changing the receiver URL** triggers a warning. Confirmations are tracked
  per destination, so the new receiver is sent everything it lacks. This is
  deliberate: it prevents the silent data loss that would otherwise happen.

## 8. Troubleshooting

| Symptom | Meaning and what to do |
| --- | --- |
| MT5: ERROR | The terminal is not running, not logged in, or the symbol is not in Market Watch. Open the terminal, log in, and add the symbol. |
| Server: AUTH_FAILED | The token is wrong or expired. Set the correct token and press Save. |
| Server: ERROR | The receiver is unreachable or returned an error. The message shows what it said. Check the URL and your internet connection. |
| Nothing is sent | Either everything is already confirmed (normal) or the receiver reports it holds the candles. Check the per-timeframe acknowledged column. |
| A candle is stuck pending | The receiver accepted the request but did not name the candle in its response. It is retried safely; the receiver must follow the protocol. |

## 9. Building the executable yourself

On a Windows machine with Python 3.9 or later:

```
powershell -ExecutionPolicy Bypass -File packaging/build_windows.ps1
```

The script checks tkinter, runs the test suite, and refuses to package a build
that does not pass. Output: `dist\ASTRA-Collector.exe`.

Without a local Python, the repository's GitHub Actions workflow
`.github/workflows/collector-windows-build.yml` builds the same executable on a
Windows runner and uploads it as the artifact `ASTRA-Collector-windows`.

## 10. Security notes

- The app makes only outbound HTTPS connections. It opens no inbound port.
- It never asks for or transmits broker credentials. It uses the terminal that
  is already logged in.
- It never places orders. There is no trading code in this application.
- Tokens never appear in the event log. Error text from the receiver is bounded
  and stripped of anything resembling a credential before it is shown.

## 11. Telling someone else how to receive your data

At the bottom of the window there is a **Receiver setup text** panel. Press
**Show setup text** to read it, **Copy setup text** to put it on the clipboard,
or **Save as file...** to write it to a file.

The text is a complete description of the receiving side: every endpoint, every
field, the exact failure each missing field causes, the deduplication rule, and
the response shape. It is generated from the same values the app actually uses,
so it cannot drift from what the app sends.

Send it to whoever is building your receiving service — a developer, or another
AI agent — and they can build a compatible receiver from that text alone,
without knowing this project.

The text contains no token and no secret. It is safe to paste into a message.
The receiver URL and sender id appear in it, which is intentional: the reader
needs them to configure their side.
