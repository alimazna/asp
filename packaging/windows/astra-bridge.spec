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

from PyInstaller.utils.hooks import collect_all, collect_submodules

REPO_ROOT = os.path.abspath(os.path.join(SPECPATH, "..", ".."))
BRIDGE_PKG = os.path.join(REPO_ROOT, "bridge", "mt5_python")

hiddenimports = [
    "bridge_service",
    "mt5_client",
    "schemas",
    "mt5_csv_feed",
    "read_candles",
]

binaries = []
datas = []

# MetaTrader5 is not pure Python: its core is a compiled extension module plus
# native DLLs. `collect_submodules` alone returns only the Python submodule
# *names*, so a frozen bridge that used it imported the package shell but failed
# to load the native code at runtime ("No module named 'MetaTrader5'" in the
# installed app, even though the build host had it). `collect_all` gathers the
# package's submodules, data files, and DLL/pyd binaries, which is what makes
# the frozen bridge importable on the end user's machine.
#
# Include MetaTrader5 only when it is importable on the build host; without it
# the bridge still boots and reports MT5_TERMINAL_UNAVAILABLE.
try:
    import MetaTrader5  # noqa: F401
    mt5_datas, mt5_binaries, mt5_hidden = collect_all("MetaTrader5")
    datas += mt5_datas
    binaries += mt5_binaries
    hiddenimports += mt5_hidden
    hiddenimports += collect_submodules("MetaTrader5")
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
