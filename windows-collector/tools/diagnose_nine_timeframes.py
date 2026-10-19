"""Full nine-timeframe diagnostic.

Exercises the real engine against the test doubles for every supported
timeframe and prints one complete row per timeframe: what the receiver stored,
what the ledger acknowledged, and whether the two agree. An earlier manual run
truncated before D1, so this prints the whole table and a final verdict.

Run:  python3 tools/diagnose_nine_timeframes.py
"""

from __future__ import annotations

import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "tests"))

from collector.config import CollectorConfig  # noqa: E402
from collector.engine import SyncEngine  # noqa: E402
from collector.identity import TIMEFRAMES, TIMEFRAME_SECONDS  # noqa: E402
from collector.ledger import DeliveryLedger  # noqa: E402
from collector.receiver import ReceiverClient  # noqa: E402
from collector.store import CandleStore  # noqa: E402
from doubles import FakeMt5Terminal, FakeReader, FakeReceiver  # noqa: E402


def main() -> int:
    directory = tempfile.mkdtemp(prefix="astra_diag_")
    terminal = FakeMt5Terminal(symbol="XAUUSD", total_bars=400,
                               last_time=int(time.time()))
    receiver = FakeReceiver(token="diag-token")
    reader = FakeReader(terminal, symbol="XAUUSD")
    config = CollectorConfig(
        backend_url=receiver.url,
        sender_id="diagnostic",
        symbol="XAUUSD",
        timeframes=list(TIMEFRAMES),
        prime_history_bars=300,
        catchup_max_bars=500,
        catchup_bar_limit=5000,
        poll_interval_seconds=30,
        request_timeout_seconds=10,
        allow_insecure_http=True)
    store = CandleStore(os.path.join(directory, "candles.sqlite"))
    ledger = DeliveryLedger(os.path.join(directory, "delivery.sqlite"))
    client = ReceiverClient(receiver.url, "diag-token", timeout_seconds=10)
    engine = SyncEngine(config, store, ledger, client, reader=reader)
    destination = config.destination_id()

    print("=" * 78)
    print("ASTRA Collector - nine-timeframe diagnostic")
    print(f"timeframes: {' '.join(TIMEFRAMES)}")
    print(f"prime_history_bars: {config.prime_history_bars}")
    print("=" * 78)

    engine.sync_once()

    # A second pass must add nothing: it proves the prime is stable and that an
    # acknowledged candle is never re-sent.
    receiver.state.requests = []
    engine.sync_once()
    resent = sum(len(r.get("candles", [])) for r in receiver.state.requests)

    print(f"{'TF':<5}{'stored':>8}{'acked':>8}{'newest stored':>15}"
          f"{'newest acked':>15}  verdict")
    print("-" * 78)

    failures = []
    for timeframe in TIMEFRAMES:
        stored_times = sorted(
            int(c["open_time_utc"]) for c in receiver.state.stored.values()
            if c["timeframe"] == timeframe and c["symbol"] == "XAUUSD")
        acked_times = ledger.acknowledged_open_times(
            destination, "XAUUSD", timeframe)
        step = TIMEFRAME_SECONDS[timeframe]

        problems = []
        if not stored_times:
            problems.append("nothing stored")
        if acked_times != stored_times:
            problems.append("acked != stored")
        for earlier, later in zip(stored_times, stored_times[1:]):
            if later - earlier != step:
                problems.append(f"gap {earlier}->{later}")
                break
        newest_closed = terminal.last_time - (terminal.last_time % step) - step
        if stored_times and stored_times[-1] > newest_closed:
            problems.append("stored a forming candle")

        verdict = "ok" if not problems else "FAIL: " + "; ".join(problems)
        if problems:
            failures.append((timeframe, problems))
        print(f"{timeframe:<5}{len(stored_times):>8}{len(acked_times):>8}"
              f"{str(stored_times[-1]) if stored_times else '-':>15}"
              f"{str(acked_times[-1]) if acked_times else '-':>15}  {verdict}")

    print("-" * 78)
    print(f"candles re-sent on the second pass: {resent} (expected 0)")
    if resent:
        failures.append(("(global)", [f"{resent} candles re-sent"]))

    status = engine.status()
    print(f"mt5_state={status['mt5_state']} "
          f"receiver_state={status['receiver_state']} "
          f"last_error={status['last_error'] or 'none'}")
    print(f"totals: sent={status['sent_total']} "
          f"acknowledged={status['acknowledged_total']} "
          f"deduplicated={status['deduplicated_total']} "
          f"failed={status['failed_total']} "
          f"rejected={status['rejected_total']}")

    store.close()
    ledger.close()
    receiver.stop()

    print("=" * 78)
    if failures:
        print("RESULT: FAILURES")
        for timeframe, problems in failures:
            print(f"  {timeframe}: {'; '.join(problems)}")
        return 1
    print("RESULT: all nine timeframes primed, acknowledged, stored, "
          "contiguous, and stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
