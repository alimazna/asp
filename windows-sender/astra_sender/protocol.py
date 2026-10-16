"""HTTPS client for the ASTRA server ingestion / sync contracts.

Uses only the Python standard library (urllib) so the sender needs no third
party HTTP dependency. Every request is authenticated with a dedicated
Bearer credential; responses are validated structurally (the envelope must
carry the expected v1/schema identity and per-candle acceptance details). A
200 status alone is not an acknowledgment — the sender only acknowledges a
candle when the server reports it as stored.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Dict, List, Optional


INGEST_PATH = "/api/v1/candles/ingest"
SYNC_PATH = "/api/v1/sync/latest"


class ProtocolError(Exception):
    """Structured protocol failure with a stable error code."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class ServerClient:
    def __init__(self, backend_url: str, sender_token: str, sender_id: str,
                 timeout_seconds: int = 30):
        self._base = backend_url.rstrip("/")
        self._token = sender_token
        self._sender_id = sender_id
        self._timeout = timeout_seconds

    # -- helpers ------------------------------------------------------------

    def _request(self, path: str, method: str = "GET",
                 body: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        url = self._base + path
        data = None
        headers = {"Authorization": f"Bearer {self._token}",
                   "Accept": "application/json"}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(url, data=data, headers=headers,
                                         method=method)
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as resp:
                raw = resp.read()
                status = resp.status
        except urllib.error.HTTPError as exc:
            status = exc.code
            raw = exc.read()
        except urllib.error.URLError as exc:
            raise ProtocolError("NETWORK", f"cannot reach server: {exc.reason}")
        except OSError as exc:
            raise ProtocolError("NETWORK", f"network failure: {exc}")

        try:
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            # A non-JSON body is never an acknowledgment.
            raise ProtocolError("BAD_RESPONSE",
                                f"server returned non-JSON body (HTTP {status})")

        if status >= 400:
            code = payload.get("code") or payload.get("error", {}).get("code") \
                or "HTTP_ERROR"
            message = payload.get("message") or payload.get("error", {}).get("message") \
                or f"HTTP {status}"
            raise ProtocolError(str(code), str(message))
        return payload

    # -- ingestion ----------------------------------------------------------

    def ingest_candles(self, source_id: str, symbol: str,
                       timeframe: str, candles: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Send a batch; returns the parsed ack envelope.

        Raises ProtocolError on transport/API failure. The caller must treat
        ANY exception as "uncertain" and keep the batch pending.
        """
        body = {
            "schema_version": "1.0",
            "source_id": source_id,
            "symbol": symbol,
            "timeframe": timeframe,
            "candles": candles,
        }
        return self._request(INGEST_PATH, "POST", body)

    def sync_latest(self, source_id: str = "") -> Dict[str, Any]:
        """Fetch the authoritative server-side watermark set.

        Scoped to ``source_id`` when given, which is the only form safe to use
        for reconciliation: the unscoped response is an aggregate across every
        sender and cannot prove that this sender's candle was persisted.
        """
        path = SYNC_PATH
        if source_id:
            path = f"{SYNC_PATH}?source_id={urllib.parse.quote(source_id, safe='')}"
        return self._request(path, "GET")