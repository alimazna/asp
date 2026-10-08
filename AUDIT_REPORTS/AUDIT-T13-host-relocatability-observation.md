# Audit Observation — T13 real-host relocatability (Agent-C proposal)

- **Auditor:** Agent-D (Verification & Audit)
- **Date:** 2026-10-08 06:55 UTC
- **Repo HEAD:** a5b83de
- **Status:** read-only corroboration of a load-bearing claim in Agent-C's T13
  proposal. No task verdict; no source touched.

Agent-C's T13 proposal asserts the real `aura_backend_host` is not app-root
relocatable and therefore starts DEGRADED in a fresh checkout. Verified
independently:

## Source

- `src/platform/windows/PathResolver.cpp:81` — `out.appRootDir = dirName(exe);`
  (no env/flag override anywhere in `resolve()`; `bridgeScript` is
  `appRoot/resources/bridge/mt5_python/bridge_service.py`).
- `src/platform/windows/AuraBackendHost.cpp:78` — default calibration-audit path
  is `appRootDir + "/AUDIT_REPORTS/AUDIT-T11-calibration.md"`.
- `--calibration-audit <path>` exists (line 71-75), so the audit artifact is
  stageable; there is **no** equivalent override for the bridge script path.

## Runtime confirmation (read-only run of the real binary)

```
$ ./build/aura_backend_host --once --api-port 45999
  app root: /workspace/project/asp/build
  startup stage: DEGRADED
  startup not ready: bridge script not found at
        /workspace/project/asp/build/resources/bridge/mt5_python/bridge_service.py
  calibration audit: absent ... (artifact not found:
        /workspace/project/asp/build/AUDIT_REPORTS/AUDIT-T11-calibration.md)
  api system/state: ... "mode":"DEGRADED","bridge_state":"OFFLINE"
  issue: no ingestor (bridge unavailable)
```

The claim is **accurate**: the app root is derived from the binary's directory
(`build/`), the bridge resource and the audit artifact both live at the repo root,
and no override exists for either. The host is honest about DEGRADED.

## Relevance to T24 / F24-1

Consistent with F24-1: T24's synthetic harness drives the *mock*, which is not
app-root sensitive; the real host's DEGRADED startup is why a real-host e2e needs
resource staging. This is Agent-C-zone (`src/platform/`, packaging) — Agent-D
records the observation only; the fix/ruling belongs to the Lead and Agent-C.
