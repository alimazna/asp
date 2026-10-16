# AUDIT-T36 — Windows installer, elevated machine-wide install, and the silent-startup fix

Date: 2026-10-16
Branch: `deploy/astra-stack`
Owner report: "التطبيق ما شتغل، أفقص عليه ما شتغل" (the app does not run; I
double-click it and nothing happens), then: install it properly, have it ask
the system for permissions, and let it store its files inside the system.

## 1. Root cause of the silent startup

The exe is built windowed (`console=False`), so on Windows `sys.stdout` and
`sys.stderr` are `None`. On a first run with no `config.json`, the old code did
`print("ASTRA Sender is not configured.", file=sys.stderr)` and returned 2.
Writing to `None` discards the message, so the process exited instantly with no
window, no dialog and no log. Double-clicking was indistinguishable from doing
nothing.

Reproduced locally by setting `stdout=None; stderr=None`: exit 0, zero output.

## 2. Fixes (sender v0.4.1, carried into v0.4.2)

- `_install_console_fallback()` routes `stdout`/`stderr` to
  `%LOCALAPPDATA%\AstraSender\sender.log` when there is no console.
- `_show_error_dialog()` raises a native `MessageBoxW` on setup errors — but
  only when there is genuinely no console and `ASTRA_SENDER_NO_DIALOG` is
  unset. A message box blocks until dismissed, so raising one with a console
  attached (or in CI) hangs the process. This was caught in the first build:
  the Windows test job timed out at 120s/60s because the dialog waited for a
  click that never came.
- `main()` wraps startup so an unexpected crash is logged and shown.
- First launch writes a template config pre-filled with the backend URL and
  `sender_id`, so the user supplies only the ingestion token.
- Regression test simulates `stdout=None; stderr=None` and requires exit 2 plus
  a populated `sender.log`.

## 3. Installer and machine-wide data (v0.4.2)

Owner asked for a real installer that requests permissions and keeps files
inside the system.

- `windows-sender/installer/astra_sender.iss` (Inno Setup):
  - `PrivilegesRequired=admin` → the UAC prompt ("requests permissions").
  - Installs the app under `Program Files\AstraSender`.
  - Creates `%PROGRAMDATA%\AstraSender` with `users-modify`, so the sender
    writes its ledger there without an access-denied error for any user.
  - Start Menu + optional desktop shortcut + uninstaller.
  - If a `config.json` sits next to the setup program, setup copies it to
    `%PROGRAMDATA%\AstraSender\config.json` (only when none exists). The
    credential therefore travels beside the setup program and is never compiled
    into the binary.
- `config.py`: `machine_data_dir()`, and `default_config_path()` now prefers a
  provisioned `%PROGRAMDATA%\AstraSender\config.json`; the ledger follows the
  active config.
- `gui.py`: a **Settings** button edits backend URL / sender ID / token in-app
  (token masked, never logged), so the JSON need not be edited by hand.
- CI builds the installer with `choco install innosetup` and uploads it beside
  the exe; the release on a `sender-*` tag carries both.

## 4. Verification

| Check | Result |
|---|---|
| Local sender unit tests | 32 passed |
| `--selfcheck` (source) | OK, `MetaTrader5: NOT bundled` (expected off-Windows) |
| Windows CI job | success: tests, PyInstaller, Inno Setup, smoke-run, artifact |
| Artifacts | `AstraSender.exe` 24,649,552 B; `AstraSenderSetup-0.4.2.exe` 26,343,037 B |
| Setup PE magic | `MZ` (valid Windows executable) |
| Token auth vs production | `GET /api/v1/sync/latest` → 200 |
| Malformed ingest | `POST /candles/ingest` `{"candles":[]}` → 400 `missing_fields`, nothing stored |
| Unauthenticated ingest | `POST /candles/ingest` → 401 |
| Production candle rows | 0 across M1/M15/H1/D1 — no contamination from these checks |
| Endpoints | livez 200, health 200, timeframes 200, readyz 503 (honest: no data) |

## 5. Delivery

The token could not be stored as a GitHub Actions secret
(`403 Resource not accessible by integration` — the token lacks `secrets:write`),
so CI builds a **generic** installer. The configured package is assembled here
with `config.json` beside the setup program and served over the work host:

- `AstraSenderSetup-0.4.2.exe`
- `config.json` (contains the token — keep private)
- `SETUP_AR.md`

Base URL: `https://work-1-pfmzzojsozqrxzue.prod-runtime.all-hands.dev/`

## 6. Not verified / caveats

- **NOT VERIFIED**: the installer was not executed on a real Windows machine
  with a live MT5 terminal — no Windows host is available to this environment.
  The exe and setup program were produced on a Windows runner and smoke-tested
  there; the UAC and `%PROGRAMDATA%` write behaviour are from the Inno Setup
  directives, not observed.
- The token is exposed in chat logs. **Rotate `ASTRA_SENDER_TOKEN`** after
  confirming the install; rotating invalidates the delivered `config.json`.
- `readyz` stays 503 until real MT5 candles arrive from the sender.

## 7. Addendum — single self-contained installer (owner: "one link, install and run")

Owner asked for one download that installs and runs immediately, with no edits.

- `astra_sender.iss` gained `/DEmbedConfig=1`, which compiles a `config.json`
  from the build directory into the setup, producing one self-contained
  installer. The default stays external so a public build carries no secret.
- The workflow gained `workflow_dispatch` inputs `sender_token` / `embed`. The
  token is read from `$GITHUB_EVENT_PATH` on disk: putting it in the step's
  `env:` block made GitHub echo it into the log, so the first attempt was
  deleted and the read path changed. Confirmed 0 occurrences afterwards.
- The run that produced the artifact was deleted, so its artifact is not
  retained on GitHub. The credential-bearing installer is served only from the
  ephemeral work host.

### Silent-install verification (Windows runner, real install)

The workflow now installs the configured setup with
`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART` and asserts the result:

| Assertion | Observed |
|---|---|
| `%PROGRAMDATA%\AstraSender\config.json` exists | yes |
| provisioned token non-empty | `token_len=64` |
| backend URL / sender id | `https://astra-server-production.up.railway.app`, `win10-desk-01` |
| nine timeframes provisioned | `M1,M5,M15,M30,H1,H4,D1,W1,MN1` |
| app installed | `C:\Program Files\AstraSender\AstraSender.exe` |
| installed app `--selfcheck` | exit code `0` |
| token in run log | 0 occurrences |

This check found and fixed a real defect: Inno had defaulted to 32-bit mode, so
the app installed under `Program Files (x86)`. `ArchitecturesInstallIn64BitMode`
was restored to make the path deterministic.

### Delivered artifact

`AstraSenderSetup-0.4.2-Configured.exe` (26,342,797 B), served at
`https://work-1-pfmzzojsozqrxzue.prod-runtime.all-hands.dev/`, with
`START_HERE_AR.md`. The generic installer remains public on the
`sender-v0.4.2` release; the configured one is deliberately not published.
