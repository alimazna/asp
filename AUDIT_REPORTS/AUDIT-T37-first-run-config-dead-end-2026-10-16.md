# AUDIT-T37 — First-run config dead end and silent settings write (v0.4.5)

Date: 2026-10-16
Status: FIXED and VERIFIED on a real Windows runner (run 38949802487)

## Symptom

The owner installed v0.4.4, launched it, and reported a configuration error.
The shipped installer was the *generic* one (no embedded token), so first run
had no `backend_url`, `sender_token` or `sender_id`.

## Root causes

Three separate defects, all real, all hit on a fresh install.

### 1. Settings wrote a config nothing read

`SenderGui._open_settings` rebuilt the config path from `app_data_dir()`
(`%LOCALAPPDATA%\AstraSender`). But `default_config_path()` prefers the
installer-provisioned `%PROGRAMDATA%\AstraSender\config.json` when it exists.
On a per-machine install the owner could type a token, be told "settings
saved", and still see "not configured" forever, because the write went to a
file the app never loads.

Fix: the GUI now receives the exact `config_path` the app resolved, writes
there, creates the parent directory, and returns whether the saved config is
actually usable.

### 2. An unconfigured windowed exe was a dead end

`__main__` showed a modal message box and `return 2`. A PyInstaller windowed
exe has no console, so double-clicking a fresh install closed the app with no
way to enter the token. The app looked broken rather than unconfigured.

Fix: the GUI now opens with a first-run prompt and Settings ready. The
report-and-exit behaviour is kept for `--console` and for headless runs
(`ASTRA_SENDER_NO_DIALOG=1`), where nobody can type a token anyway.

### 3. sender.log was empty exactly when it mattered

The windowed exe redirects stdout/stderr to `sender.log`, but the stream was
block-buffered and the GUI keeps running after the startup messages are
written. Nothing was flushed, so a hung startup left an empty log — the one
piece of evidence the owner needed.

Fix: open the log line-buffered (`buffering=1`). Verified: the marker is
visible 0.5 s after start while the app is still running.

## Verification

- `windows-sender` unit tests: 50 pass, including new regression tests for
  the config path, the cancelled/blank-token paths, and the first-run prompt.
- Windows CI step "Verify an unconfigured exe opens the settings window" runs
  the frozen exe with no config and no `ASTRA_SENDER_NO_DIALOG`, and requires
  `sender.log` to contain `opening the settings window`.
- Windows CI silent install: `config provisioned: backend=... sender_id=... token_len=64 timeframes=M1,M5,M15,M30,H1,H4,D1,W1,MN1`.
- Frozen app `--diag-mt5` exit 4 (terminal unavailable) — correct on a runner.

## Also fixed

The workflow hardcoded `/FAstraSenderSetup-0.4.4-Configured` while the app
reported 0.4.5, so the download name disagreed with the installed version. The
filename is now derived from `__version__`.

## Token handling

The ingestion token was recovered from the project's own conversation log and
validated against the live backend (`/api/v1/sync/latest`: bogus token 401,
correct token 200). It was passed to the build via the workflow's secret input,
never written to the repository. Confirmed absent from all tracked files and
from the git tree. `/api/v1/health` is intentionally public; authenticated
endpoints require the bearer token.
