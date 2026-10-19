# Configured sender delivery (private)

The owner wants one installer that runs with zero edits. That means the
ingestion token has to be inside the package. The repository is **public**, so a
release asset would be world-readable — that is the v0.4.6 leak
(`AUDIT-T38-leaked-ingestion-token-2026-10-17.md`).

The channel is therefore a **draft release**. Verified properties of a draft:

| Check | Result |
| --- | --- |
| anonymous asset URL | `404` |
| anonymous release API | `404` |
| present in the public release list | no |
| authenticated download | `200`, byte-exact |

## How the owner downloads it

The draft is `sender-private-configured` and carries:

- `AstraSenderSetup-0.4.7-Configured.exe` — single installer, token inside
- `AstraSender-Configured-0.4.7.zip` — same installer plus `config.json` and
  `READ-ME-FIRST.txt`, for manual placement if wanted

Two ways to get it, both requiring the owner's own GitHub session:

1. **Browser** — open <https://github.com/alimazna/asp/releases> while logged in
   as `alimazna`. GitHub shows drafts to accounts with write access, at the top
   of the list. Download the `.exe` asset.
2. **CLI** (verified working from this environment):

   ```
   gh release download sender-private-configured --repo alimazna/asp \
     --pattern '*.exe' --cloned
   ```

   Note `releases/download/<tag>/<asset>` returns 404 for drafts; the asset must
   be fetched through the authenticated API. The CLI does that.

## Installing

1. Start MetaTrader 5 and log in. Leave it running.
2. Double-click the installer and accept UAC.
3. Setup writes `%PROGRAMDATA%\AstraSender\config.json` (pre-filled) and launches
   the app **non-elevated** — `runasoriginaluser` in `astra_sender.iss`. The app
   and `terminal64.exe` must run at the same integrity level or the MT5 IPC
   handshake fails and the UI reports `MT5 UNAVAILABLE`.

## Reproducing the build

```
gh workflow run "Build Windows Astra Sender" --repo alimazna/asp \
  --ref fix/sender-bridge-packaging \
  -f embed=1 -f deliver_draft=1 -f sender_token=<token>
```

`embed=1` compiles the token in; `deliver_draft=1` routes it to the private
draft. A configured installer produced **without both** inputs fails the run, so
the accidental-publish class stays closed. The workflow reads the token from
`$GITHUB_EVENT_PATH` rather than the `env:` block, because GitHub echoes a step's
env vars into the log.

## Rotation

The v0.4.6 token is compromised and must be rotated. Set a new
`ASTRA_SENDER_TOKEN` in Railway, then rebuild with `embed=1 deliver_draft=1` and
the new value. The app also accepts a token typed into Settings (masked), so
rotation does not require a new package.
