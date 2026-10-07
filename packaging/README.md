# ASTRA / AURA — Packaging & Python Bundling (T07)

This directory owns how the **bundled Python runtime** and the **MT5 bridge**
are assembled into the installed desktop layout that the C++ runtime expects.

It is the source-side counterpart to the runtime path logic in
`src/platform/windows/`:

| Runtime consumer | Contract |
|---|---|
| `PathResolver.{h,cpp}` | all paths derive from the executable location, never the CWD |
| `BundleLocator.{h,cpp}` | finds `runtime/python/…` (bundled) and `resources/bridge/mt5_python/bridge_service.py`; system interpreter is development-only |
| `PackagingConfig.{h,cpp}` | declares `bridgeRelativePath`, `pythonRelativePath`, `bundlePython=true`, `allowSystemPython=false` |
| `src/api/RuntimeManifest.json` | declares `requires_manual_cmd: false`, `managed_by_application: true` |

## Installed layout

```text
ASTRA/
  aura_backend_host            # backend host entry point (double-click)
  resources/
    bridge/mt5_python/         # bundled bridge (copied verbatim)
      bridge_service.py
      mt5_client.py
      read_candles.py
      schemas.py
      requirements.txt
      README.md
    python/                    # bundled interpreter (self-contained)
  config/
  data/
  logs/
```

The layout is declared once in `bundle_manifest.json`. `scripts/bundle.py`
stages it and verifies it; nothing resolves a path from the CWD.

## Bundling

```text
python3 scripts/bundle.py --out dist/ASTRA
python3 scripts/bundle.py --out dist/ASTRA --backend-host build/aura_backend_host
python3 scripts/bundle.py --out dist/ASTRA --verify
```

The stager copies the bridge files, creates `resources/python`, `config`,
`data`, and `logs`, and refuses to let `__pycache__`, virtualenvs, `.git`, or
`*.pyc` leak into the bundle.

## The bundled interpreter

A release bundle is **self-contained**: it ships its own interpreter so the end
user never installs Python and never opens a CMD window.

- Windows: the official embeddable CPython distribution (or an equivalent
  self-contained build) unpacked under `resources/python/` as `python.exe`.
- POSIX (development/CI): `resources/python/python3`, or a system interpreter
  passed explicitly for local smoke tests only.

`BundleLocator` treats a system interpreter as `SYSTEM_FALLBACK` and only when
`allowSystemFallback` is set; `PackagingConfig::validate` rejects a config that
neither bundles nor permits a fallback.

## Runtime dependencies

`requirements-runtime.txt` mirrors `bridge/mt5_python/requirements.txt` and is
installed into the bundled interpreter at build time. The bridge imports only
the standard library plus `MetaTrader5` (which pulls in `numpy`).

## Known discrepancies (reported, not silently resolved)

1. **Python runtime path.** `PathResolver::resolve` sets
   `pythonRuntimeDir = resourceDir/python` (`resources/python`), and
   `BundleLocator.h`/`HANDOFF.md` agree. But `PackagingConfig.h` declares
   `pythonRelativePath = "runtime/python/python.exe"`, which
   `interpreterPath()` joins with `resourceDir` to produce
   `resources/runtime/python/python.exe`. The two C++ sources disagree. The
   bundle follows the runtime-authoritative `resources/python`; the header is
   protected `src/` and is left for the Lead to reconcile.

2. **pandas dependency.** `defaultPackagingConfig()` in
   `src/platform/windows/PackagingConfig.cpp` declares `pandas>=2.0` as
   required, but neither the bridge nor the runtime requirements import pandas.
   pandas is therefore **not** installed into the bundle. Whether the C++
   dependency list should drop pandas is a Lead decision.

3. **numpy pin drift (C-3).** The numpy version spec disagrees across three
   files: `src/platform/windows/PackagingConfig.cpp` (`>=1.24`),
   `bridge/mt5_python/requirements.txt` (`>=1.23`), and
   `packaging/requirements-runtime.txt` (`>=1.24`). Cosmetic, but it is another
   manifest-vs-manifest drift to reconcile. `bridge/mt5_python/requirements.txt`
   is in Agent-C's zone; the other two are protected/declared. Not changed
   pending the Lead's reconciliation decision.

All three are recorded in `coordination/agent-c/comm.md` and escalated to the
human by the Lead (21:22 / 21:29 UTC).

## Runtime completeness (scope limit — read before shipping)

The stager produces a **layout**, not a runnable product. It copies the bridge
source, creates the directories, and verifies the arrangement — but it does
**not** place a Python interpreter binary under `resources/python/`.

Consequently, on a staged bundle `BundleLocator::locate()` still finds no
bundled interpreter until a real interpreter distribution is dropped into
`resources/python/`. That payload is a **binary distribution** (the Windows
embeddable CPython build, or an equivalent self-contained build) and is out of
scope for this source repository.

So the accurate claim for T07 is **"layout parity + stager"**, not
"self-contained runtime". A runtime-complete bundle requires:

1. an interpreter payload under `resources/python/` (build step, not this repo), and
2. reconciliation of C-1 so the declared and searched runtime paths agree.

Neither was in T07's acceptance criteria; both are recorded so no one mistakes
the staged tree for a shippable bundle.

## Startup requirement

`requires_manual_cmd: false`. The host locates the bundled interpreter and the
bridge script from the executable root, launches the bridge as a supervised
child, and waits for the handshake. No CMD, no PATH setup, no manual `python`.
