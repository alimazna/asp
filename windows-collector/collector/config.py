"""Collector configuration.

Separated concerns:

  * non-secret settings (destination URL, symbol, timeframes, intervals) live in
    ``config.json`` and are safe to inspect, copy, and back up;
  * the bearer token is a secret and is stored in a separate file with
    owner-only permissions, never in ``config.json``, never in the ledger, never
    in a log line.

The destination is a first-class setting: changing it changes the delivery
identity (see ``ledger.destination_id``), so a switch to a new receiver cannot
silently mark candles as delivered.
"""

from __future__ import annotations

import json
import os
import stat
import tempfile
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .identity import TIMEFRAMES, is_known_timeframe

APP_DIR_NAME = "AstraCollector"
CONFIG_FILE_NAME = "config.json"
TOKEN_FILE_NAME = "token"
STORE_FILE_NAME = "candles.sqlite"
LEDGER_FILE_NAME = "deliveries.sqlite"


def app_data_dir() -> str:
    """Per-user writable directory. No elevation required to run the app."""
    base = os.environ.get("ASTRA_COLLECTOR_DIR")
    if base:
        return base
    if os.name == "nt":
        root = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
        if root:
            return os.path.join(root, APP_DIR_NAME)
        return os.path.join(os.path.expanduser("~"), APP_DIR_NAME)
    root = os.environ.get("XDG_DATA_HOME") or os.path.join(
        os.path.expanduser("~"), ".local", "share")
    return os.path.join(root, APP_DIR_NAME)


def default_config_path() -> str:
    return os.path.join(app_data_dir(), CONFIG_FILE_NAME)


def default_token_path() -> str:
    return os.path.join(app_data_dir(), TOKEN_FILE_NAME)


def default_store_path() -> str:
    return os.path.join(app_data_dir(), STORE_FILE_NAME)


def default_ledger_path() -> str:
    return os.path.join(app_data_dir(), LEDGER_FILE_NAME)


@dataclass
class CollectorConfig:
    """Everything except the token."""

    backend_url: str = ""
    sender_id: str = ""
    symbol: str = "XAUUSD"
    timeframes: List[str] = field(default_factory=lambda: list(TIMEFRAMES))

    # Bounds. A brand-new destination is primed with a bounded window so the
    # downstream engine has a decision-grade feature window immediately,
    # instead of waiting for dozens of live candles.
    prime_history_bars: int = 300
    catchup_max_bars: int = 500
    catchup_bar_limit: int = 5000
    #: Hard bound on how far back the planner will ever look, per stream. This
    #: is what stops a first run from uploading months of history: only the
    #: newest ``max_plan_bars`` candles of a stream are ever candidates for
    #: delivery. Older captured candles stay local and are never sent.
    max_plan_bars: int = 2000

    poll_interval_seconds: int = 30
    request_timeout_seconds: int = 30
    #: Refuse plain HTTP unless explicitly allowed (local development only).
    allow_insecure_http: bool = False

    def validate(self) -> List[str]:
        problems: List[str] = []
        url = (self.backend_url or "").strip()
        if not url:
            problems.append("Destination URL is empty.")
        elif not (url.startswith("https://") or url.startswith("http://")):
            problems.append("Destination URL must start with https:// or http://.")
        elif url.startswith("http://") and not self.allow_insecure_http:
            problems.append(
                "Destination uses plain HTTP. Use https:// or explicitly allow "
                "insecure HTTP for local development only.")
        if not self.sender_id:
            problems.append("Sender ID is empty; a stable instance name is "
                            "required for idempotent delivery.")
        if not self.symbol:
            problems.append("Symbol is empty.")
        unknown = [t for t in self.timeframes if not is_known_timeframe(t)]
        if unknown:
            problems.append("Unknown timeframe(s): " + ", ".join(unknown))
        if not self.timeframes:
            problems.append("At least one timeframe is required.")
        if self.poll_interval_seconds < 5:
            problems.append("Poll interval must be at least 5 seconds.")
        if self.prime_history_bars < 0 or self.prime_history_bars > 10000:
            problems.append("prime_history_bars must be between 0 and 10000.")
        if self.catchup_max_bars < 0:
            problems.append("catchup_max_bars must not be negative.")
        if self.catchup_bar_limit <= 0:
            problems.append("catchup_bar_limit must be positive.")
        if self.max_plan_bars <= 0:
            problems.append("max_plan_bars must be positive.")
        if self.request_timeout_seconds <= 0:
            problems.append("request_timeout_seconds must be positive.")
        return problems

    def destination_id(self) -> str:
        from .ledger import DeliveryLedger
        return DeliveryLedger.destination_id(self.backend_url)

    # -- persistence --------------------------------------------------------

    def to_dict(self) -> Dict[str, Any]:
        return {
            "backend_url": self.backend_url,
            "sender_id": self.sender_id,
            "symbol": self.symbol,
            "timeframes": list(self.timeframes),
            "prime_history_bars": self.prime_history_bars,
            "catchup_max_bars": self.catchup_max_bars,
            "catchup_bar_limit": self.catchup_bar_limit,
            "max_plan_bars": self.max_plan_bars,
            "poll_interval_seconds": self.poll_interval_seconds,
            "request_timeout_seconds": self.request_timeout_seconds,
            "allow_insecure_http": self.allow_insecure_http,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CollectorConfig":
        config = cls()
        config.backend_url = str(data.get("backend_url", "") or "").strip()
        config.sender_id = str(data.get("sender_id", "") or "").strip()
        config.symbol = str(data.get("symbol", config.symbol) or config.symbol).strip()
        timeframes = data.get("timeframes")
        if isinstance(timeframes, list) and timeframes:
            config.timeframes = [str(t) for t in timeframes]
        config.prime_history_bars = int(
            data.get("prime_history_bars", config.prime_history_bars))
        config.catchup_max_bars = int(
            data.get("catchup_max_bars", config.catchup_max_bars))
        config.catchup_bar_limit = int(
            data.get("catchup_bar_limit", config.catchup_bar_limit))
        config.max_plan_bars = int(
            data.get("max_plan_bars", config.max_plan_bars))
        config.poll_interval_seconds = int(
            data.get("poll_interval_seconds", config.poll_interval_seconds))
        config.request_timeout_seconds = int(
            data.get("request_timeout_seconds", config.request_timeout_seconds))
        config.allow_insecure_http = bool(
            data.get("allow_insecure_http", config.allow_insecure_http))
        return config

    @classmethod
    def load(cls, path: str) -> "CollectorConfig":
        if not path or not os.path.isfile(path):
            return cls()
        try:
            with open(path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, json.JSONDecodeError):
            return cls()
        if not isinstance(data, dict):
            return cls()
        return cls.from_dict(data)

    def save(self, path: str) -> None:
        """Atomic write: a crash mid-save cannot leave a truncated config."""
        directory = os.path.dirname(os.path.abspath(path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        handle = tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", dir=directory, delete=False,
            prefix=".config-", suffix=".tmp")
        try:
            json.dump(self.to_dict(), handle, indent=2, sort_keys=True)
            handle.flush()
            os.fsync(handle.fileno())
        finally:
            handle.close()
        os.replace(handle.name, path)


def load_token(path: Optional[str] = None) -> str:
    """Read the bearer token from its own file. Empty when absent."""
    target = path or default_token_path()
    if not os.path.isfile(target):
        return ""
    try:
        with open(target, "r", encoding="utf-8") as handle:
            return handle.read().strip()
    except OSError:
        return ""


def save_token(token: str, path: Optional[str] = None) -> None:
    """Write the token with owner-only permissions.

    On Windows the file inherits the user's profile ACL, which is already
    per-user. On POSIX the mode is set explicitly to 0600 so no other account
    can read it.
    """
    target = path or default_token_path()
    directory = os.path.dirname(os.path.abspath(target))
    if directory:
        os.makedirs(directory, exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=directory, delete=False,
        prefix=".token-", suffix=".tmp")
    try:
        handle.write(token.strip() + "\n")
        handle.flush()
        os.fsync(handle.fileno())
    finally:
        handle.close()
    if os.name != "nt":
        try:
            os.chmod(handle.name, stat.S_IRUSR | stat.S_IWUSR)
        except OSError:
            pass
    os.replace(handle.name, target)
