"""Receiver client: talk to whichever endpoint is configured.

The endpoint is a *setting*, not a constant. Everything here is keyed on the
destination identity derived from that URL, so pointing the collector at a
different receiver cannot make it believe the new receiver already has candles
it has never seen.

Protocol (all over HTTPS; the token is sent as a bearer credential and is never
logged, echoed, or stored in the ledger):

  GET  <base>/health          -> liveness probe
  GET  <base>/candles         -> the receiver's authoritative candle set
  POST <base>/candles/ingest  -> deliver a batch, returns per-candle state

The ingest response is only trusted when it names each candle's unique id and
states whether it was newly stored or already present. A bare 200 that does not
name the candle is treated as "unconfirmed" and the candle stays pending.
"""

from __future__ import annotations

import http.client
import json
import ssl
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Sequence

from .store import StoredCandle

HEALTH_PATH = "/health"
CANDLES_PATH = "/candles"
INGEST_PATH = "/candles/ingest"

# Per-candle states a receiver may report. "stored_new" and "already_stored"
# both mean "persisted"; anything else is not a confirmation.
STATE_STORED_NEW = "stored_new"
STATE_ALREADY_STORED = "already_stored"
CONFIRMED_STATES = frozenset({STATE_STORED_NEW, STATE_ALREADY_STORED})


class ReceiverError(Exception):
    """A transport or protocol failure. Carries a stable code, never a token."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass
class IngestOutcome:
    """Per-candle result of one ingest request."""

    confirmed: List[str]              # uids the receiver confirmed persisted
    rejected: Dict[str, str]          # uid -> reason
    unconfirmed: List[str]            # uids the response did not account for
    received_at: int = 0

    @property
    def ok(self) -> bool:
        return not self.unconfirmed


class ReceiverClient:
    """Authenticated HTTPS client for one receiver endpoint."""

    def __init__(self, base_url: str, token: str,
                 timeout_seconds: float = 30.0,
                 verify_tls: bool = True):
        self.base_url = (base_url or "").strip().rstrip("/")
        self._token = token or ""
        self._timeout = timeout_seconds
        self._context: Optional[ssl.SSLContext] = None
        if self.base_url.lower().startswith("https://") and not verify_tls:
            self._context = ssl.create_default_context()
            self._context.check_hostname = False
            self._context.verify_mode = ssl.CERT_NONE

    # -- transport ----------------------------------------------------------

    def _request(self, path: str, method: str = "GET",
                 body: Optional[bytes] = None,
                 params: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        if not self.base_url:
            raise ReceiverError("NO_DESTINATION", "no receiver URL configured")
        url = self.base_url + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        headers = {
            "Accept": "application/json",
            "User-Agent": "astra-collector/1.0",
        }
        if self._token:
            headers["Authorization"] = "Bearer " + self._token
        if body is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(url, data=body, headers=headers,
                                         method=method)
        try:
            with urllib.request.urlopen(request, timeout=self._timeout,
                                        context=self._context) as response:
                payload = response.read().decode("utf-8", "replace")
                status = response.status
        except urllib.error.HTTPError as exc:
            payload = exc.read().decode("utf-8", "replace")
            if exc.code in (401, 403):
                raise ReceiverError("AUTH_REJECTED",
                                    "receiver rejected the credential") from None
            if exc.code == 429:
                raise ReceiverError("RATE_LIMITED",
                                    "receiver rate limit reached") from None
            raise ReceiverError("HTTP_ERROR",
                                f"receiver returned HTTP {exc.code}: "
                                f"{_safe_message(payload)}") from None
        except urllib.error.URLError as exc:
            reason = getattr(exc, "reason", exc)
            raise ReceiverError("UNREACHABLE",
                                f"cannot reach receiver: {reason}") from None
        except TimeoutError:
            raise ReceiverError("TIMEOUT", "receiver request timed out") from None
        except (http.client.HTTPException, ConnectionError, OSError) as exc:
            # A dropped connection after the request was sent is the classic
            # uncertain delivery: the receiver may or may not have committed.
            # Reported as TIMEOUT so the candle stays pending and is retried
            # with the same unique id, which is what makes that safe.
            raise ReceiverError("TIMEOUT",
                                f"connection lost before a response: "
                                f"{type(exc).__name__}") from None

        if status != 200:
            raise ReceiverError("HTTP_ERROR",
                                f"receiver returned HTTP {status}: "
                                f"{_safe_message(payload)}")
        if not payload.strip():
            return {}
        try:
            parsed = json.loads(payload)
        except json.JSONDecodeError:
            raise ReceiverError("BAD_RESPONSE",
                                "receiver response was not JSON") from None
        return parsed if isinstance(parsed, dict) else {"data": parsed}

    # -- endpoints ----------------------------------------------------------

    def health(self) -> Dict[str, Any]:
        return self._request(HEALTH_PATH)

    def remote_open_times(self, symbol: str,
                          timeframe: str) -> List[int]:
        """The receiver's authoritative open times for one stream.

        The ASTRA server answers ``/candles`` with ``data.bars[]`` where each bar
        carries ``time`` (the open time in epoch seconds). A summary form
        (``open_times``) and a generic ``candles`` list are also accepted so the
        client is not welded to one receiver build. An empty result is reported
        as empty, never guessed.
        """
        response = self._request(CANDLES_PATH, params={
            "symbol": symbol, "timeframe": timeframe, "limit": "100000"})
        data = response.get("data", response)
        if not isinstance(data, dict):
            return []
        if isinstance(data.get("open_times"), list):
            return sorted({int(t) for t in data["open_times"]})
        bars = data.get("bars")
        if bars is None:
            bars = data.get("candles")
        if not isinstance(bars, list):
            return []
        times = []
        for candle in bars:
            if not isinstance(candle, dict):
                continue
            if candle.get("symbol") not in (None, symbol):
                continue
            if candle.get("timeframe") not in (None, timeframe):
                continue
            value = candle.get("open_time_utc", candle.get("time"))
            if value is not None:
                times.append(int(value))
        return sorted(set(times))

    def ingest(self, symbol: str, timeframe: str,
               candles: Sequence[StoredCandle]) -> IngestOutcome:
        """Deliver candles. Only explicitly named, persisted candles confirm."""
        if not candles:
            return IngestOutcome([], {}, [])
        payload = {
            "symbol": symbol,
            "timeframe": timeframe,
            "candles": [c.to_payload() for c in candles],
        }
        response = self._request(
            INGEST_PATH, method="POST",
            body=json.dumps(payload).encode("utf-8"))
        data = response.get("data", response)
        # The receiver may name a candle by our uid (echoed back) or by its own
        # identity string (symbol/timeframe/open_time). Both are acceptable
        # proof, but the ledger is keyed on our uid, so the receiver's form is
        # translated here rather than trusted verbatim.
        by_uid = {c.uid: c for c in candles}
        by_key = {(c.symbol, c.timeframe, int(c.open_time_utc)): c
                  for c in candles}
        confirmed: List[str] = []
        rejected: Dict[str, str] = {}
        seen: set = set()

        def resolve(entry: Dict[str, Any]) -> str:
            """Map an ack entry back to our uid. "" when it names nothing we sent."""
            named = entry.get("uid")
            if named and named in by_uid:
                return named
            identity = entry.get("identity")
            if identity:
                # "symbol=XAUUSD&timeframe=M15&open_time=1792905300"
                parts = {}
                for pair in str(identity).split("&"):
                    if "=" in pair:
                        key, _, value = pair.partition("=")
                        parts[key.strip()] = value.strip()
                try:
                    key = (parts.get("symbol", ""), parts.get("timeframe", ""),
                           int(parts.get("open_time", "")))
                except ValueError:
                    key = None
                if key in by_key:
                    return by_key[key].uid
            if named:
                return named
            return ""

        for entry in data.get("accepted", []) or []:
            if not isinstance(entry, dict):
                continue
            uid = resolve(entry)
            if uid and entry.get("state", "") in CONFIRMED_STATES:
                confirmed.append(uid)
                seen.add(uid)
        for entry in data.get("duplicates", []) or []:
            if not isinstance(entry, dict):
                continue
            uid = resolve(entry)
            if uid:
                confirmed.append(uid)
                seen.add(uid)
        for entry in data.get("rejected", []) or []:
            if not isinstance(entry, dict):
                continue
            uid = resolve(entry)
            if uid:
                rejected[uid] = str(entry.get("reason") or "rejected")
                seen.add(uid)

        unconfirmed = [c.uid for c in candles if c.uid not in seen]
        return IngestOutcome(confirmed=confirmed, rejected=rejected,
                             unconfirmed=unconfirmed,
                             received_at=int(data.get("received", 0) or 0))


def _safe_message(payload: str) -> str:
    """Trim a receiver error body to something safe to log.

    Bounded and stripped of anything that looks like a credential, so a
    diagnostic never leaks a token into the local event log.
    """
    text = (payload or "").strip().replace("\n", " ")
    if len(text) > 200:
        text = text[:200] + "..."
    lowered = text.lower()
    for marker in ("bearer ", "token", "authorization"):
        index = lowered.find(marker)
        if index >= 0:
            text = text[:index] + "[redacted]"
            break
    return text
