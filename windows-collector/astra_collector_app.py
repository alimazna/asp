"""Windows entry point for the packaged collector.

Launches the desktop window. Frozen by PyInstaller into a single executable so
the owner never needs Python, pip, or a command prompt.
"""

from __future__ import annotations

import sys


def main() -> int:
    try:
        from collector.gui import main as gui_main
    except ImportError as exc:
        # tkinter missing is the one failure worth naming precisely.
        sys.stderr.write(
            "ASTRA Collector could not start its window.\n"
            f"Missing component: {exc}\n\n"
            "If this is a source checkout, install the tkinter package for "
            "your Python build.\n")
        return 3
    return gui_main()


if __name__ == "__main__":
    sys.exit(main())
