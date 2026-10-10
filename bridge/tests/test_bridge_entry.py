#!/usr/bin/env python3
"""PY-0006 - Regression teeth for the frozen bridge entry point.

``bridge/bridge_entry.py`` must boot the real service from a source checkout
without any manual ``sys.path`` setup. It previously inserted its own directory
(``bridge/``) instead of the sibling package directory (``bridge/mt5_python/``),
so ``python3 bridge/bridge_entry.py`` died with
``ModuleNotFoundError: No module named 'bridge_service'`` -- contradicting the
module's own docstring.

Run: python3 bridge/tests/test_bridge_entry.py
     python3 -m pytest bridge/tests/ -v
"""

from __future__ import annotations

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ENTRY_PATH = os.path.join(REPO_ROOT, "bridge", "bridge_entry.py")
PKG_DIR = os.path.join(REPO_ROOT, "bridge", "mt5_python")


def _load_entry():
    name = "astra_bridge_entry_under_test"
    spec = importlib.util.spec_from_file_location(name, ENTRY_PATH)
    assert spec and spec.loader, f"cannot load {ENTRY_PATH}"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_package_dir_is_the_sibling_mt5_python_dir() -> None:
    entry = _load_entry()
    assert entry._package_dir() == PKG_DIR, entry._package_dir()


def test_package_dir_contains_bridge_service() -> None:
    entry = _load_entry()
    # The whole point of _package_dir() is that importing bridge_service works
    # from there; assert the module actually resolves.
    sys.path.insert(0, entry._package_dir())
    try:
        import bridge_service  # noqa: F401
    finally:
        sys.path.remove(entry._package_dir())


if __name__ == "__main__":
    failures = 0
    for fn in (test_package_dir_is_the_sibling_mt5_python_dir,
               test_package_dir_contains_bridge_service):
        try:
            fn()
            print(f"[PASS] {fn.__name__}")
        except AssertionError as exc:
            failures += 1
            print(f"[FAIL] {fn.__name__}: {exc}")
    raise SystemExit(1 if failures else 0)
