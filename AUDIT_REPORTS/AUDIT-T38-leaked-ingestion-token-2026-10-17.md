# AUDIT-T38 — Leaked ingestion token in a public release asset (2026-10-17)

## Incident

`AstraSenderSetup-0.4.6-Configured.exe` was published as a **public GitHub
Release asset** on `sender-v0.4.6`. It carried the ingestion credential compiled
into the setup binary.

This repository is public, so the asset was world-readable. Inno Setup
**compresses but does not encrypt**: the embedded `config.json` — including the
plaintext `sender_token` — is recoverable with `innoextract` or 7-Zip. Anyone who
downloaded that asset obtained a working ingestion credential.

## Evidence

The build log for the run that produced the asset
([39015816096](https://github.com/alimazna/asp/actions/runs/39015816096) is the
fix; the leaking run was 38959371455) printed:

```
single installer: embedding config (token hidden)
config provisioned: backend=https://astra-server-production.up.railway.app
  sender_id=win10-desk-01 token_len=64 timeframes=M1,M5,...,MN1
```

`token_len=64` proves a real 64-hex-character token was compiled in, not a
placeholder. The installer script confirms the mechanism — under
`/DEmbedConfig=1` it does `Source: "config.json"`, which places the file's bytes
inside the setup binary:

```
#ifdef EmbedConfig
Source: "config.json"; DestDir: "{commonappdata}\AstraSender"; ...
```

## Scope

- **Release assets:** `sender-v0.4.6` (token-bearing, confirmed) and
  `sender-v0.4.5` (named `-Configured` but built *generic* — the log for run
  38950076300 shows `generic installer (config read from beside the setup)`, so
  it holds no token; the misleading name is cosmetic).
- **Workflow artifacts:** five `embed=1` branch runs uploaded the token-bearing
  installer as a public artifact (`AstraSender-exe`): runs 38959371455,
  38949802487, 38949537777, 38949235326, 38947272338.
- **Older `ASTRA-windows*` artifacts** (2026-10-10, `build-windows.yml`, Qt C++
  app) contain no token handling. Not affected.
- The working tree, `.git/config`, and all commits are clean: no token was ever
  committed to source control.

## Remediation applied

1. Deleted the `sender-v0.4.6` configured release asset (now HTTP 404).
2. Deleted the five token-bearing workflow artifacts.
3. `sender-windows-build.yml` now removes any `*Configured*` installer **after**
   verifying that provisioning works and **before** the upload/release steps, and
   fails the run if a configured installer appears in a run that did not opt into
   an embed build — which is exactly how v0.4.6 shipped one from a tag push.
4. `sender-v0.4.7` was published with token-less assets only, and the tag was
   re-pointed at the fixed workflow so the tag run succeeds.

Verified on real Windows CI: run 39016469618 (`embed=1`) logged
`removing AstraSenderSetup-0.4.7-Configured.exe`, and the uploaded artifact
contained only `dist/AstraSender.exe`.

## Required follow-up — NOT DONE (no credentials in the agent environment)

**The leaked token must be rotated.** Deletion does not un-leak it; assume it is
compromised. Until it is rotated, anyone who downloaded the asset can POST
candles to the backend.

1. Generate a new 64-hex ingestion token.
2. Update `ASTRA_SENDER_TOKEN` on the Railway backend service and redeploy. This
   session had **no Railway or Cloudflare credentials** (none present in the
   environment), so this step is NOT VERIFIED.
3. In the sender's Settings window, paste the new token (the field is masked and
   never logged), or edit `%PROGRAMDATA%\AstraSender\config.json`.

Note the design already limits the blast radius: `AstraApiServer` keeps
`senderTokens` separate from admin tokens, and ingestion is rate-limited and
batch-bounded. A leaked sender token can submit candles; it cannot reach the
destructive `/api/v1/admin/candles` surface. The exposure is still real, because
fabricated candles would corrupt the analysis input.

## Why this was invisible

What is provable from the API and logs:

- The release was created at 18:55:51 and published at 20:05:52, on target
  `main`.
- The tag run (38959745674, 20:05:54) **failed** at the attach step with
  `Pattern '...*-Configured.exe' does not match any files` — so that run built no
  configured installer and cannot have attached the asset.
- A dispatch run with `embed=1` (38959371455) succeeded at 20:02:51, roughly
  three minutes before publication, and its log shows a real token was embedded.

So the asset came from an `embed=1` build, not from the tag run. The exact
attach mechanism (a manual upload, or an earlier workflow revision whose attach
step was not tag-gated) is **not proven** and is not reconstructed here.

The durable lesson, which the fix encodes: a guard must be based on what is on
disk rather than on what the run's inputs claim, because the run that leaked the
token was not the run whose inputs said it would.
