# Setup

Owner-facing instructions. Written so the application can be started normally,
without a command prompt and without installing a development toolchain.

## 1. Requirements

- Windows 10 or later.
- MetaTrader 5 desktop terminal, installed and already logged in to the account
  that has the symbol you want to send.
- The receiving endpoint URL and its access token, supplied by whoever runs the
  receiving side.

You do **not** need Python, pip, or any build tool to run the packaged
executable.

## 2. Install

1. Copy `ASTRA-Collector.exe` anywhere convenient, for example
   `C:\Users\<you>\ASTRA-Collector\`.
2. Run it. The window opens. No console appears.

Windows SmartScreen may warn about an unsigned application the first time. The
build is unsigned; choose "More info" then "Run anyway" only if you obtained the
file from the expected location and can verify its SHA-256.

## 3. Configure

Fill in the three fields at the top of the window:

| Field | Meaning |
| --- | --- |
| Receiver URL | The HTTPS endpoint that will receive the candles, e.g. `https://example.com` |
| Symbol | The instrument, e.g. `XAUUSD`. See symbol discovery below. |
| Sender id | A name for this computer, e.g. `desk-pc`. Identifies the sender in the receiver's records. |
| Token | The access token for the receiver. |

Then press **Save settings**.

The token is stored in a separate file with owner-only permissions, not in the
configuration file and never in the source. Press **Reveal token** only if you
need to read it back; the window warns you first.

**Test connection** checks both the MetaTrader terminal and the receiver and
reports what each one said, using real responses.

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
