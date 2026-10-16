# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for the ASTRA sender single-file executable.

Build on Windows (or in the GitHub Actions workflow):
    pyinstaller --clean --noconfirm astra_sender.spec

Produces `dist/AstraSender.exe`. The app bundles the bridge mt5_client and
schemas modules plus the Windows-only MetaTrader5 package, so the frozen exe
reads the local terminal with no extra setup on the owner's PC. MetaTrader5 is
still imported lazily by the bridge, so a missing terminal is reported rather
than fatal.
"""

import os

block_cipher = None

# Bundle the real bridge module (mt5_client.py, schemas.py) so symbol
# discovery and closed-bar semantics are the exact production code.
BRIDGE_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(SPECPATH)), "bridge", "mt5_python")
)
_bridge_module = [(os.path.join(BRIDGE_DIR, "mt5_client.py"), "astra_sender"),
                  (os.path.join(BRIDGE_DIR, "schemas.py"), "astra_sender")]

# mt5_client is shipped as a *data* file (so its source is the exact production
# bridge code), which means PyInstaller never scans it and would miss its
# `import MetaTrader5`. MetaTrader5 is only installable on Windows, so it is
# listed here rather than imported at build time.
_hidden = ["sqlite3"]
if os.name == "nt":
    _hidden += ["MetaTrader5", "numpy"]

a = Analysis(
    ["astra_sender/__main__.py"],
    pathex=["."],
    binaries=[],
    datas=_bridge_module,
    hiddenimports=_hidden,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["matplotlib", "pandas", "PIL"],
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
    name="AstraSender",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,  # GUI app; no console window on launch
    disable_windowed_traceback=False,
    icon=None,
)