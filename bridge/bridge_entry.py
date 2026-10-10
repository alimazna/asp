"""PY-0006 - Frozen entry point for the AURA MT5 bridge.

PyInstaller uses this as the boot script: it adds the sibling ``mt5_python``
package directory to ``sys.path`` and delegates to ``bridge_service.main``. In a
source checkout the package directory already sits next to this file; in a frozen
bundle it is collected as data (see astra-bridge.spec).

Only the Python standard library and the optional MetaTrader5 package are
required. When MetaTrader5 is absent the bridge still boots and reports
MT5_TERMINAL_UNAVAILABLE, exactly as the source service does.
"""

from __future__ import annotations

import os
import sys


def _package_dir() -> str:
    if getattr(sys, "frozen", False):
        # PyInstaller extracts bundled data under sys._MEIPASS. The spec places
        # the bridge modules at mt5_python/ inside that tree.
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        return os.path.join(base, "mt5_python")
    # Source checkout: the modules live in the sibling mt5_python/ package dir,
    # not next to this boot script.
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "mt5_python")


def main(argv=None) -> int:
    pkg = _package_dir()
    if pkg not in sys.path:
        sys.path.insert(0, pkg)
    from bridge_service import main as service_main  # noqa: E402
    return service_main(argv)


if __name__ == "__main__":
    raise SystemExit(main())
