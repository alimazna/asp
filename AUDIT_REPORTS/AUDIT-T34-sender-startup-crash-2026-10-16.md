# AUDIT-T34 — Windows sender startup crash (relative import)

Date: 2026-10-16
Branch: `deploy/astra-stack`
Status: RESOLVED, released as `sender-v0.4.0`

## Reported symptom

Owner ran the packaged `AstraSender.exe` from release `sender-v0.3.0` and got:

```
ImportError: attempted relative import with no known parent package
```

## Root cause

`windows-sender/astra_sender/__main__.py` used relative imports
(`from .config import ...`). PyInstaller runs the entry-point script as a
top-level module, so `__package__` is empty and those imports cannot resolve.
The exe therefore crashed on every real startup.

`--version` returned before the relative imports were reached, so the CI smoke
test passed on a completely non-functional build.

## Evidence

Both revisions were frozen with PyInstaller and executed:

| Revision | `--version` | real startup |
|---|---|---|
| pre-fix | exit 0 | `ImportError: attempted relative import with no known parent package` |
| post-fix | exit 0 | `ASTRA Sender self-check OK` |

The pre-fix row reproduces the owner's exact error while the old CI check still
reports success — that is how the defect shipped.

## Fix

1. `__main__.py`: when `__package__` is empty, insert the package's parent on
   `sys.path` and re-dispatch through `astra_sender.__main__` so relative
   imports resolve.
2. Added `--selfcheck`, which imports every runtime module (tkinter guarded)
   and exits 0, so the frozen smoke test exercises the real import graph.
3. CI now runs `--selfcheck` on the frozen exe and fails if it does not pass.
4. `tests/test_entrypoint.py` runs the module script-style — the same way the
   PyInstaller bootloader does — and asserts the relative-import error cannot
   return.

## Follow-on defect found and fixed

`MetaTrader5` was not bundled. `mt5_client.py` is shipped as a *data* file, so
PyInstaller never scanned its `import MetaTrader5`. Added `MetaTrader5` and
`numpy` as Windows `hiddenimports`, dropped `numpy` from `excludes`, and made CI
assert `MetaTrader5: bundled` in the exe's self-check report. The release exe
now needs only the MT5 terminal, not Python or `pip install`.

## Verification (release `sender-v0.4.0`)

- Built on `windows-latest` by *Build Windows Astra Sender*.
- `--version` exit 0, `--selfcheck` exit 0, `MetaTrader5: bundled`.
- Asset: `AstraSender.exe`, 24,630,166 bytes, PE (`MZ`) header verified.
- SHA256 `9f9b79fa5d7c93f5eec9693423381c9022e6b832d44460673995f395aaa601e4`.
- Sender unit suite: 31/31 pass.
- `sender-v0.3.0` retitled "(BROKEN — do not use)".

## Not verified

- Live MT5 candle read and end-to-end ingestion: no MT5 terminal is reachable
  from the agent environment. The sender logic is covered by the fake-MT5 test
  double; live terminal integration remains unverified by the agent.
