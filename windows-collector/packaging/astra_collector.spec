# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller specification for the ASTRA Collector.

Builds one windowed executable with no console flash and no external data
files. MetaTrader5 is deliberately NOT bundled: the collector talks to the
already-installed MT5 terminal through the package that terminal provides, and
bundling a second copy would shadow it.

Build (on Windows):
    pyinstaller --clean --noconfirm packaging/astra_collector.spec
Output:
    dist/ASTRA-Collector.exe
"""

block_cipher = None

a = Analysis(
    ["../astra_collector_app.py"],
    pathex=[".."],
    binaries=[],
    datas=[],
    hiddenimports=[
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
    ],
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
