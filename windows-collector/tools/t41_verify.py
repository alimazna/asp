"""Replay the exact T41 scenario against the FIXED collector.

Before the fix: after priming, the hidden streams were never re-sent.
After the fix: the collector must re-send M15/M5/M1 because the receiver
answers for them authoritatively with "nothing".
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.abspath("."))
sys.path.insert(0, os.path.abspath("tests"))

from collector.config import CollectorConfig
from collector.engine import SyncEngine
from collector.identity import TIMEFRAMES
from collector.ledger import DeliveryLedger
from collector.receiver import ReceiverClient
from collector.store import CandleStore
from doubles import FakeMt5Terminal, FakeReader, FakeReceiver

HIDDEN = {"M15", "M5", "M1"}
d = tempfile.mkdtemp(prefix="t41_verify_")
now = int(time.time())
term = FakeMt5Terminal(symbol="XAUUSD", total_bars=400,
                       last_time=now - now % 900, clock_offset_seconds=0)
recv = FakeReceiver(token="tok")
reader = FakeReader(term, symbol="XAUUSD")
cfg = CollectorConfig(backend_url=recv.url, sender_id="verify", symbol="XAUUSD",
                      timeframes=list(TIMEFRAMES), prime_history_bars=50,
                      catchup_max_bars=100000, catchup_bar_limit=20000,
                      poll_interval_seconds=30, request_timeout_seconds=10,
                      allow_insecure_http=True)
store = CandleStore(os.path.join(d, "c.sqlite"))
ledger = DeliveryLedger(os.path.join(d, "l.sqlite"))
client = ReceiverClient(recv.url, "tok", timeout_seconds=10)
eng = SyncEngine(cfg, store, ledger, client, reader=reader)


def sent_by_tf():
    out = {}
    for req in recv.state.requests:
        for c in req.get("candles", []):
            out[c["timeframe"]] = out.get(c["timeframe"], 0) + 1
    return out


print("--- pass 1: prime, all streams acknowledged ---")
eng.sync_once()
eng.sync_once()
acked = {tf: len(ledger.acknowledged_open_times(
    cfg.destination_id(), "XAUUSD", tf)) for tf in TIMEFRAMES}
print("acked per tf:", {k: v for k, v in acked.items() if v})

print()
print("--- receiver now LOSES M1/M15/M5 (reports empty) ---")
recv.state.empty_streams = {("XAUUSD", tf) for tf in HIDDEN}
recv.state.requests = []
eng.sync_once()
got = sent_by_tf()
print("re-sent by tf:", got)

ok = all(got.get(tf, 0) > 0 for tf in HIDDEN)
extra = [tf for tf in got if tf not in HIDDEN]
print()
print("RESULT:", "PASS - hidden streams re-sent" if ok
      else "FAIL - still silent")
print("healthy streams dragged in:", extra or "none")
recv.stop()
store.close()
ledger.close()
