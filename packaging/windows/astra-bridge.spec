# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for the AURA MT5 bridge.
#
# Produces a single-file bridge.exe that the C++ backend launches as a child
# process. MetaTrader5 is an optional dependency: when the package is present on
# the build host it is bundled; when absent the frozen bridge still boots and
# reports MT5_TERMINAL_UNAVAILABLE (the honest degraded state), so the installer
# can be produced on a host without a terminal.
#
# Build (from the repo root):
#     pyinstaller --clean --noconfirm packaging/windows/astra-bridge.spec
#
# Output: dist/bridge.exe

import os

from PyInstaller.utils.hooks import collect_all

REPO_ROOT = os.path.abspath(os.path.join(SPECPATH, "..", ".."))
BRIDGE_PKG = os.path.join(REPO_ROOT, "bridge", "mt5_python")

hiddenimports = [
    "bridge_service",
    "mt5_client",
    "schemas",
    "mt5_csv_feed",
    "read_candles",
    # MetaTrader5 ships as a single top-level compiled extension (MetaTrader5.pyd)
    # with its own native dependencies, not an importable Python *package*.
    # collect_all()/collect_submodules() therefore return nothing for it; the
    # module name must be listed directly so modulegraph pulls the .pyd and the
    # binaries it links into the frozen bundle. When MetaTrader5 is not on the
    # build host this is a harmless "hidden import not found" warning and the
    # bridge still boots in the MT5_TERMINAL_UNAVAILABLE degraded state.
    "MetaTrader5",
    # MetaTrader5 imports numpy from its compiled core, which modulegraph cannot
    # see inside the .pyd. Without numpy bundled, `import MetaTrader5` fails at
    # runtime with "No module named 'numpy'" even though the .pyd is present.
    "numpy",
]

binaries = []
datas = []

# If a future MetaTrader5 becomes a real package, also gather any submodules,
# data files and DLLs it ships. No-op for the current single-extension wheel.
try:
    import MetaTrader5  # noqa: F401
    mt5_datas, mt5_binaries, mt5_hidden = collect_all("MetaTrader5")
    datas += mt5_datas
    binaries += mt5_binaries
    hiddenimports += mt5_hidden
except Exception:
    pass

a = Analysis(
    [os.path.join(REPO_ROOT, "bridge", "bridge_entry.py")],
    pathex=[BRIDGE_PKG],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="bridge",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    target_arch=None,
)
