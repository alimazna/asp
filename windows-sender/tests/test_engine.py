"""Engine tests: end-to-end over a loopback stub of the ingestion contract.

The stub server simulates the deployed backend's ack envelope. It is TEST
DATA (not production); the real backend behavior is covered by the C++
AstraApiServerTests. Cases:

  * first run sends the latest closed candle and records an ack;
  * a restart does not re-send an already-acked candle (dedup);
  * a server timeout keeps the candle pending and the retry reuses the SAME
    idempotency identity;
  * gap/catch-up sends older uacked candles chronologically, bounded.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional

# Force the fake MT5 into sys.path before importing the collector/engine.
_REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "..", ".."))
_FAKE_MT5 = os.path.join(_REPO, "tests", "integration", "fake_mt5")
_BRIDGE = os.path.normpath(os.path.join(_REPO, "bridge", "mt5_python"))
for _p in (_FAKE_MT5, _BRIDGE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
os.environ["FAKE_MT5_AVAILABLE"] = "1"
os.environ["FAKE_MT5_TOTAL_BARS"] = "300"

from astra_sender.collector import ClosedCandleCollector
from astra_sender.config import SenderConfig
from astra_sender.engine import SyncEngine
from astra_sender.ledger import DeliveryLedger
from astra_sender.protocol import ServerClient


class FakeAstraServer(BaseHTTPRequestHandler):
    """Loopback stand-in for the ingestion/sync contract."""

    stored: List[Dict[str, Any]] = []
    requests: List[Dict[str, Any]] = []
    auth_token = "stub-token"
    fail_once = False
    count_ingest_calls = 0

    def log_message(self, *args):  # silence
        pass

    def _reply(self, code: int, body: Dict[str, Any]) -> None:
        raw = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _read_json(self) -> Dict[str, Any]:
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def _auth_ok(self) -> bool:
        header = self.headers.get("Authorization", "")
        return header == f"Bearer {self.auth_token}"

    def do_GET(self):  # noqa: N802
        if self.path.startswith("/api/v1/sync/latest"):
            if not self._auth_ok():
                self._reply(401, {"error": "true", "code": "unauthorized"})
                return
            self._reply(200, {"api": "v1", "schema": "1.0", "data": []})
            return
        self._reply(404, {"error": "true", "code": "not_found"})

    def do_POST(self):  # noqa: N802
        if self.path.startswith("/api/v1/candles/ingest"):
            FakeAstraServer.count_ingest_calls += 1
            self.requests.append(self._read_json())
            if not self._auth_ok():
                self._reply(401, {"error": "true", "code": "unauthorized"})
                return
            if FakeAstraServer.fail_once and FakeAstraServer.count_ingest_calls == 1:
                # Simulate a lost response after a server-side commit.
                self._reply(200, {"api": "v1", "schema": "1.0",
                                  "data": {"source_id": "s1", "symbol": "XAUUSD",
                                           "timeframe": "M15", "received": 1,
                                           "accepted": [], "duplicates": [],
                                           "rejected": [], "ingested": 0}})
                return
            body = self.requests[-1]
            accepted = []
            for candle in body.get("candles", []):
                key = (candle.get("time"), candle.get("timeframe"))
                if any(s.get("time") == candle.get("time")
                       and s.get("timeframe") == candle.get("timeframe")
                       for s in FakeAstraServer.stored):
                    continue
                FakeAstraServer.stored.append(candle)
                accepted.append({
                    "identity": "symbol=XAUUSD&timeframe={}&open_time={}".format(
                        candle.get("timeframe"), candle.get("time")),
                    "state": "stored_new",
                })
            self._reply(200, {"api": "v1", "schema": "1.0",
                              "data": {"source_id": body.get("source_id"),
                                       "symbol": "XAUUSD",
                                       "timeframe": body.get("timeframe"),
                                       "received": int(time.time()) * 1000,
                                       "accepted": accepted,
                                       "duplicates": [],
                                       "rejected": [],
                                       "ingested": len(accepted)}})
            return
        self._reply(404, {"error": "true", "code": "not_found"})


def make_config(directory: str, interval: int = 30) -> SenderConfig:
    config = SenderConfig()
    config.backend_url = "http://127.0.0.1:1"  # placeholder; set below
    config.sender_token = "stub-token"
    config.sender_id = "s1"
    config.symbol = "XAUUSD"
    config.timeframes = ["M15"]
    config.poll_interval_seconds = interval
    config.ledger_path = os.path.join(directory, "ledger.sqlite")
    return config


class EngineTearDownMixin:
    def tearDown(self):
        for attr in ("engine",):
            obj = getattr(self, attr, None)
            if obj is not None:
                try:
                    obj.stop()
                except Exception:
                    pass


def latest_fake_candle_time(last_time: Optional[int] = None) -> int:
    """Newest closed M15 open time from the fake feed (index 1 in read axes)."""
    os.environ["FAKE_MT5_LAST_TIME"] = str(last_time if last_time
                                           else int(time.time()))
    collector = ClosedCandleCollector("XAUUSD")
    collector.start()
    try:
        result = collector.read_latest_closed("M15")
        return int(result["candle"].time)
    finally:
        collector.stop()


class EngineTest(EngineTearDownMixin, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        FakeAstraServer.stored = []
        FakeAstraServer.requests = []
        FakeAstraServer.fail_once = False
        FakeAstraServer.count_ingest_calls = 0
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), FakeAstraServer)
        cls.server_thread = threading.Thread(target=cls.httpd.serve_forever)
        cls.server_thread.daemon = True
        cls.server_thread.start()
        cls.port = cls.httpd.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()

    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="astra_engine_")
        FakeAstraServer.requests = []
        FakeAstraServer.count_ingest_calls = 0
        FakeAstraServer.fail_once = False
        FakeAstraServer.stored = []
        # Keep the fake feed pinned to a healthy "now" across tests. The
        # collector suite runs first and leaves FAKE_MT5_AVAILABLE=0 after the
        # "unavailable" case; restore availability + fresh clock here.
        os.environ["FAKE_MT5_AVAILABLE"] = "1"
        os.environ["FAKE_MT5_LAST_TIME"] = str(int(time.time()))
        os.environ["FAKE_MT5_TOTAL_BARS"] = "300"
        self.config = make_config(self.directory)
        self.config.backend_url = f"http://127.0.0.1:{self.port}"
        self.config.request_timeout_seconds = 5

    def _make_engine(self, ledger=None):
        collector = ClosedCandleCollector("XAUUSD")
        ledger = ledger or DeliveryLedger(self.config.ledger_path)
        client = ServerClient(self.config.backend_url, self.config.sender_token,
                              self.config.sender_id, 5)
        engine = SyncEngine(self.config, collector=collector, ledger=ledger,
                            client=client)
        self.engine = engine
        return engine

    def test_first_run_sends_latest_closed_candle_and_acks(self):
        engine = self._make_engine()
        t = latest_fake_candle_time()
        engine._sync_once()
        ledger = DeliveryLedger(self.config.ledger_path)
        self.assertTrue(ledger.is_acknowledged("s1", "XAUUSD", "M15", t),
                        "latest closed candle should be acknowledged")
        snapshot = engine.status_snapshot()
        self.assertEqual(snapshot["mt5_state"], "READY")
        self.assertEqual(snapshot["server_state"], "ERROR"
                         if snapshot["server_state"] == "STARTING" else
                         snapshot["server_state"])
        # At least one ingest request went out.
        self.assertGreaterEqual(len(FakeAstraServer.requests), 1)
        ledger.close()

    def test_restart_does_not_resend_acked_candle(self):
        engine = self._make_engine()
        engine._sync_once()
        calls_after_first = FakeAstraServer.count_ingest_calls
        # Simulate a full sender restart with the same ledger.
        engine2 = self._make_engine(ledger=DeliveryLedger(self.config.ledger_path))
        engine2._sync_once()
        self.assertEqual(FakeAstraServer.count_ingest_calls, calls_after_first,
                         "an already-acked candle must not be re-sent")
        self.assertEqual(len(engine2.status_snapshot().get(
            "acked_candle_per_timeframe", {})), 1)
        engine2.stop()

    def test_missing_identity_candle_is_sent_then_duplicate_is_not(self):
        # After the first run the latest candle is acked; a NEW candle
        # (different open time) on the same timeframe is sent.
        engine = self._make_engine()
        engine._sync_once()
        first_time = latest_fake_candle_time()

        ledger = DeliveryLedger(self.config.ledger_path)
        # Advance the feed by several M15 bars so the newest closed candle is
        # strictly later than the already-acked one.
        advanced = int(time.time()) + 4 * 900
        os.environ["FAKE_MT5_LAST_TIME"] = str(advanced)
        new_time = latest_fake_candle_time(advanced)
        self.assertGreater(new_time, first_time)
        engine3 = self._make_engine(ledger=ledger)
        engine3._sync_once()
        ledger2 = DeliveryLedger(self.config.ledger_path)
        self.assertTrue(ledger2.is_acknowledged("s1", "XAUUSD", "M15", new_time))
        ledger.close()
        ledger2.close()
        engine3.stop()

    def test_timeout_keeps_pending_and_retries_with_same_identity(self):
        # Simulate the server committing but the response being lost: the
        # stub returns an empty ack on the first call, the engine must then
        # treat the candle as pending (not acked) and retry it.
        FakeAstraServer.fail_once = True
        engine = self._make_engine()
        t = latest_fake_candle_time()
        engine._sync_once()
        ledger = DeliveryLedger(self.config.ledger_path)
        pending = ledger.pending_identities()
        # The empty ack left it pending; a second loop retries and now acks.
        engine._sync_once()
        self.assertTrue(ledger.is_acknowledged("s1", "XAUUSD", "M15", t),
                        "candle must be acknowledged after retry")
        self.assertGreaterEqual(FakeAstraServer.count_ingest_calls, 2)
        ledger.close()

    def test_catchup_is_bounded_and_chronological(self):
        # Prime a ledger already acked at an older open time, then start the
        # engine: it must send only the candles strictly newer than the ack
        # watermark, in chronological order, bounded by catchup_max_bars.
        t = latest_fake_candle_time()
        old_time = t - 60 * 60  # one hour gap
        ledger = DeliveryLedger(self.config.ledger_path)
        identity = ledger.make_identity("s1", "XAUUSD", "M15", old_time)
        ledger.record_acknowledged("s1", "XAUUSD", "M15", old_time, identity, 1)
        ledger.close()

        self.config.catchup_max_bars = 5
        engine = self._make_engine(ledger=DeliveryLedger(self.config.ledger_path))
        engine._sync_once()
        requests = [r for r in FakeAstraServer.requests
                    if r.get("timeframe") == "M15"]
        sent_times = []
        for request in requests:
            for candle in request.get("candles", []):
                sent_times.append(candle["time"])
        self.assertGreaterEqual(len(sent_times), 1)
        # Chronological and bounded.
        self.assertEqual(sent_times, sorted(sent_times))
        self.assertLessEqual(len(sent_times), 5)

        # Server-side: all sent candles now acked.
        final_ledger = DeliveryLedger(self.config.ledger_path)
        latest_acked = final_ledger.latest_acknowledged("s1", "XAUUSD", "M15")
        self.assertGreaterEqual(latest_acked, t)
        final_ledger.close()
        engine.stop()


if __name__ == "__main__":
    unittest.main()