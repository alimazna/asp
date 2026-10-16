"""Astra Sender configuration.

A single JSON file (saved beside the application data) holds the backend
URL and the dedicated ingestion credential / sender identity. Secrets are
never stored in the local acknowledgment database and never logged. The file
should be protected with NTFS ACLs; the app never accepts a token from the
command line or from logs.

The config file path is:
   %LOCALAPPDATA%/AstraSender/config.json   (Windows)
or ~/.config/astra_sender/config.json       (POSIX)

A startup check validates the required fields and gives clear recovery
guidance instead of failing cryptically.
"""

from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


# Canonical nine target timeframes (authoritative order).
TIMEFRAMES: List[str] = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]

# Preferred default symbol candidates (broker-specific names are discovered).
DEFAULT_SYMBOL_CANDIDATES: List[str] = ["XAUUSD", "GOLD", "XAUUSDm", "XAUUSD.a"]


def app_data_dir() -> str:
    """Deterministic per-user writable directory for config + SQLite ledger."""
    if sys.platform == "win32":  # pragma: no cover - Windows path
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        return os.path.join(base, "AstraSender")
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "astra_sender")


def machine_data_dir() -> str:
    """Machine-wide data directory used by a per-machine installer.

    Windows only; returns "" elsewhere. The installer writes the config (and
    the ledger) under %PROGRAMDATA% so the app works for every user with the
    elevated write rights the installer granted at setup time.
    """
    if sys.platform != "win32":  # pragma: no cover - Windows path
        return ""
    base = os.environ.get("PROGRAMDATA") or os.environ.get("ALLUSERSPROFILE")
    return os.path.join(base, "AstraSender") if base else ""


def _looks_configured(path: str) -> bool:
    """True when a config file exists and already holds the required fields."""
    if not path or not os.path.exists(path):
        return False
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return False
    return bool(data.get("backend_url") and data.get("sender_token")
                and data.get("sender_id"))


def default_config_path() -> str:
    """Prefer an installer-provisioned machine config, else the per-user path."""
    machine = machine_data_dir()
    if machine:
        machine_cfg = os.path.join(machine, "config.json")
        if _looks_configured(machine_cfg):
            return machine_cfg
    return os.path.join(app_data_dir(), "config.json")


def default_ledger_path() -> str:
    """Keep the ledger beside whichever config is in use."""
    return os.path.join(os.path.dirname(default_config_path()), "ledger.sqlite")


@dataclass
class SenderConfig:
    """Validated configuration (never contains a password or MT5 credentials)."""

    backend_url: str = ""
    sender_token: str = ""          # dedicated ingestion credential
    sender_id: str = ""             # stable local instance identity
    symbol: str = "XAUUSD"          # requested (broker-resolved separately)
    timeframes: List[str] = field(
        default_factory=lambda: list(TIMEFRAMES))
    poll_interval_seconds: int = 30
    catchup_max_bars: int = 200          # bound on offline gap catch-up
    catchup_bar_limit: int = 1000        # max bars fetched per catch-up read
    request_timeout_seconds: int = 30
    ledger_path: str = ""                # resolved; empty -> default
    # Explicit path to the broker's terminal64.exe. Optional: without it MT5
    # attaches to the running terminal by IPC. It is needed when a broker
    # installs a suffixed terminal (e.g. "MetaTrader 5 IC Markets (SC)") and
    # auto-discovery picks the wrong one.
    mt5_path: str = ""
    # IPC attach timeout. MT5's own default is 60000 ms, which is why a failed
    # attach looked like a hang rather than an error.
    mt5_timeout_ms: int = 15000

    # -- validation ---------------------------------------------------------

    def validate(self) -> List[str]:
        problems: List[str] = []
        if not self.backend_url:
            problems.append("Backend URL is empty.")
        else:
            url = self.backend_url.strip()
            if not (url.startswith("https://") or url.startswith("http://")):
                problems.append("Backend URL must start with https:// or http://.")
        if not self.sender_token:
            problems.append("Sender token is empty.")
        if not self.sender_id:
            problems.append("Sender ID is empty (a stable identifier is required "
                            "for idempotent delivery).")
        if not self.symbol:
            problems.append("Symbol is empty.")
        unknown_tf = [t for t in self.timeframes if t not in TIMEFRAMES]
        if unknown_tf:
            problems.append("Unknown timeframe(s): " + ", ".join(unknown_tf))
        if self.poll_interval_seconds < 5:
            problems.append("Poll interval must be at least 5 seconds.")
        if self.catchup_max_bars < 0 or self.catchup_bar_limit <= 0:
            problems.append("Catch-up bounds must be non-negative.")
        if self.mt5_path and not self.mt5_path.lower().endswith(".exe"):
            problems.append("MT5 terminal path must point at terminal64.exe.")
        if self.mt5_timeout_ms <= 0:
            problems.append("MT5 timeout must be a positive number of milliseconds.")
        return problems

    @staticmethod
    def from_dict(data: Dict[str, Any], base_dir: str = "") -> "SenderConfig":
        cfg = SenderConfig()
        cfg.backend_url = str(data.get("backend_url", "") or "").strip()
        cfg.sender_token = str(data.get("sender_token", "") or "")
        cfg.sender_id = str(data.get("sender_id", "") or "")
        cfg.symbol = str(data.get("symbol", "XAUUSD") or "XAUUSD").strip()
        tfs = data.get("timeframes")
        if isinstance(tfs, list) and tfs:
            cfg.timeframes = [str(t) for t in tfs]
        cfg.poll_interval_seconds = int(data.get("poll_interval_seconds", 30))
        cfg.catchup_max_bars = int(data.get("catchup_max_bars", 200))
        cfg.catchup_bar_limit = int(data.get("catchup_bar_limit", 1000))
        cfg.request_timeout_seconds = int(data.get("request_timeout_seconds", 30))
        ledger = str(data.get("ledger_path", "") or "")
        cfg.ledger_path = ledger if ledger else default_ledger_path()
        cfg.mt5_path = str(data.get("mt5_path", "") or "").strip()
        try:
            cfg.mt5_timeout_ms = int(data.get("mt5_timeout_ms", 15000))
        except (TypeError, ValueError):
            cfg.mt5_timeout_ms = 15000
        return cfg

    def to_dict(self) -> Dict[str, Any]:
        return {
            "backend_url": self.backend_url,
            "sender_token": self.sender_token,
            "sender_id": self.sender_id,
            "symbol": self.symbol,
            "timeframes": list(self.timeframes),
            "poll_interval_seconds": self.poll_interval_seconds,
            "catchup_max_bars": self.catchup_max_bars,
            "catchup_bar_limit": self.catchup_bar_limit,
            "request_timeout_seconds": self.request_timeout_seconds,
            "ledger_path": self.ledger_path,
            "mt5_path": self.mt5_path,
            "mt5_timeout_ms": self.mt5_timeout_ms,
        }

    @staticmethod
    def save(config: "SenderConfig", path: str) -> None:
        directory = os.path.dirname(path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(config.to_dict(), handle, indent=2)

    @staticmethod
    def load(path: str) -> "SenderConfig":
        cfg = SenderConfig()
        if not os.path.exists(path):
            return cfg
        try:
            with open(path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as exc:
            # Config unreadable: return empty config so the startup check
            # prompts reconfiguration rather than crashing.
            cfg = SenderConfig()
            cfg._load_error = f"cannot read config: {exc}"
            return cfg
        cfg = SenderConfig.from_dict(data)
        cfg._load_error = ""
        return cfg

    def resolved_ledger_path(self) -> str:
        if self.ledger_path:
            return self.ledger_path
        return default_ledger_path()


# Attached by load() on failure; used by the startup check.
SenderConfig._load_error = ""  # type: ignore[attr-defined]