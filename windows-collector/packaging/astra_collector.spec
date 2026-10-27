# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller specification for the ASTRA Collector.

Builds one windowed executable with no console flash and no external data
files.

MetaTrader5 IS bundled. The collector talks to the already-installed MT5
terminal through this package, and it is the only way to read candles, so a
build without it can start but can never read a single bar: it would fail with
MT5_PACKAGE_MISSING the moment the owner presses Start. The package is
Windows-only, so it is added conditionally and only when building on Windows.
Bundling it does not shadow the terminal: the terminal is a separate process
the package connects to.

Build (on Windows):
    pyinstaller --clean --noconfirm packaging/astra_collector.spec
Output:
    dist/ASTRA-Collector.exe
"""

import os

block_cipher = None

# MetaTrader5 ships Windows-only wheels and is imported lazily inside
# Mt5Reader.connect, so PyInstaller's static scan cannot see it. Name it
# explicitly, and only when the build host can actually provide it.
_hidden = [
    "collector",
    "collector.cli",
    "collector.config",
    "collector.display",
    "collector.engine",
    "collector.gui",
    "collector.identity",
    "collector.ledger",
    "collector.mt5_reader",
    "collector.planner",
    "collector.receiver",
    "collector.store",
]
if os.name == "nt":
    _hidden += ["MetaTrader5", "numpy"]

a = Analysis(
    ["../astra_collector_app.py"],
    pathex=[".."],
    binaries=[],
    datas=[],
    hiddenimports=_hidden,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "numpy", "pandas", "matplotlib", "scipy", "PIL",
        "pytest", "setuptools", "pip",
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name="ASTRA-Collector",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
