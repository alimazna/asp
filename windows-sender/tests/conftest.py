"""Shared fixtures for the sender test suite.

The fake MetaTrader5 module stands in for the external Windows-only package so
the real ``mt5_client.py`` closed-bar/staleness logic can be exercised on any
host. It is placed early on ``sys.path`` (exactly like the repo's Python
integration tests) before any import of the collector.
"""

from __future__ import annotations

import os
import sys

_REPO = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
_FAKE_MT5 = os.path.join(_REPO, "tests", "integration", "fake_mt5")

if _FAKE_MT5 not in sys.path:
    sys.path.insert(0, _FAKE_MT5)

os.environ["FAKE_MT5_AVAILABLE"] = "1"
os.environ.setdefault("FAKE_MT5_TOTAL_BARS", "300")

# Reference the real bridge modules next to the sender.
_BRIDGE = os.path.normpath(os.path.join(_REPO, "bridge", "mt5_python"))
if _BRIDGE not in sys.path:
    sys.path.insert(0, _BRIDGE)