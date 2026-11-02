"""AYZR-LAB-TEST — registry, cost model and promotion gate.

Deterministic, offline. Uses synthetic Trade objects so no corpus is required.
"""

import os
import tempfile
import unittest

from ayzr_lab.backtest import ExitReason, Trade
from ayzr_lab.stats import Summary
from ayzr_lab.lab_registry import (
    CostModel,
    ExperimentRegistry,
    PromotionGate,
    evaluate_split,
    record_experiment,
    run_fingerprint,
)
from ayzr_lab.backtest import SignalDirection


def mk_trade(entry_time_sec: int, r: float, direction=SignalDirection.LONG) -> Trade:
    t = Trade(direction=direction, entry_time_sec=entry_time_sec, entry_price=2000.0,
              stop=1990.0, target=2020.0, risk=10.0)
    t.r_multiple = r
    t.reason = ExitReason.TARGET if r > 0 else ExitReason.STOP
    t.bars_held = 5
    return t


class CostModelTest(unittest.TestCase):
    def test_defaults_are_not_frictionless(self):
        c = CostModel()
        self.assertGreater(c.spread, 0.0)
        self.assertGreater(c.slippage, 0.0)

    def test_frictionless_control_is_all_zero(self):
        c = CostModel.frictionless()
        self.assertEqual((c.spread, c.slippage, c.commission_per_trade), (0.0, 0.0, 0.0))


class FingerprintTest(unittest.TestCase):
    def test_deterministic(self):
        a = run_fingerprint("s", {"x": 1}, {"t0": 0, "t1": 10}, "duka")
        b = run_fingerprint("s", {"x": 1}, {"t0": 0, "t1": 10}, "duka")
        self.assertEqual(a, b)

    def test_parameter_sensitive(self):
        a = run_fingerprint("s", {"x": 1}, {"t0": 0, "t1": 10}, "duka")
        b = run_fingerprint("s", {"x": 2}, {"t0": 0, "t1": 10}, "duka")
        self.assertNotEqual(a, b)

    def test_key_order_insensitive(self):
        a = run_fingerprint("s", {"x": 1, "y": 2}, {"t0": 0, "t1": 10}, "duka")
        b = run_fingerprint("s", {"y": 2, "x": 1}, {"t0": 0, "t1": 10}, "duka")
        self.assertEqual(a, b)


class RegistryTest(unittest.TestCase):
    def test_append_and_read_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "exp.jsonl")
            reg = ExperimentRegistry(path)
            rec = record_experiment(
                reg, "trend", "1.0.0", {"z": 2.0}, CostModel(),
                {"t0": 0, "t1": 100}, "duka", {"expectancy_r": 0.1})
            self.assertEqual(rec.strategy_id, "trend")
            back = reg.read_all()
            self.assertEqual(len(back), 1)
            self.assertEqual(back[0].strategy_id, "trend")
            self.assertEqual(back[0].run_fingerprint, rec.run_fingerprint)

    def test_registry_is_append_only(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "exp.jsonl")
            reg = ExperimentRegistry(path)
            for i in range(3):
                record_experiment(reg, f"s{i}", "1", {}, CostModel(), {"t0": 0, "t1": 1},
                                  "duka", {})
            self.assertEqual(len(reg.read_all()), 3)

    def test_malformed_line_is_skipped_not_fatal(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "exp.jsonl")
            reg = ExperimentRegistry(path)
            record_experiment(reg, "good", "1", {}, CostModel(), {"t0": 0, "t1": 1},
                              "duka", {})
            with open(path, "a", encoding="utf-8") as fh:
                fh.write("{not valid json\n")
            back = reg.read_all()
            self.assertEqual(len(back), 1)
            self.assertEqual(reg.skipped_lines, 1)

    def test_fingerprint_counts_detect_repeats(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "exp.jsonl")
            reg = ExperimentRegistry(path)
            for _ in range(2):
                record_experiment(reg, "s", "1", {"p": 1}, CostModel(),
                                  {"t0": 0, "t1": 1}, "duka", {})
            counts = reg.fingerprints()
            self.assertEqual(list(counts.values()), [2])

    def test_missing_file_reads_empty(self):
        reg = ExperimentRegistry("/tmp/does_not_exist_ayzr_lab.jsonl")
        self.assertEqual(reg.read_all(), [])


class PromotionGateTest(unittest.TestCase):
    def test_insufficient_oos_trades_is_rejected(self):
        gate = PromotionGate(min_oos_trades=30)
        champ = [mk_trade(i, 0.1) for i in range(50)]
        chal = [mk_trade(i, 0.5) for i in range(5)]
        d = gate.evaluate("champ", champ, "chal", chal)
        self.assertFalse(d.promote)
        self.assertEqual(d.reason, "insufficient_oos_trades")

    def test_negative_challenger_is_rejected(self):
        gate = PromotionGate(min_oos_trades=30)
        champ = [mk_trade(i, 0.1) for i in range(50)]
        chal = [mk_trade(i, -0.2) for i in range(50)]
        d = gate.evaluate("champ", champ, "chal", chal)
        self.assertFalse(d.promote)
        self.assertEqual(d.reason, "challenger_oos_not_positive")

    def test_genuine_edge_is_promoted(self):
        gate = PromotionGate(min_oos_trades=30)
        champ = [mk_trade(i, 0.05) for i in range(50)]
        # A large, low-variance edge clears the bar even with the penalty.
        chal = [mk_trade(i, 0.40) for i in range(50)]
        d = gate.evaluate("champ", champ, "chal", chal)
        self.assertTrue(d.promote)
        self.assertEqual(d.reason, "promoted_on_oos")

    def test_marginal_edge_is_rejected(self):
        gate = PromotionGate(min_oos_trades=30, min_edge_r=0.20)
        champ = [mk_trade(i, 0.10) for i in range(50)]
        chal = [mk_trade(i, 0.15) for i in range(50)]   # only +0.05 over champ
        d = gate.evaluate("champ", champ, "chal", chal)
        self.assertFalse(d.promote)
        self.assertEqual(d.reason, "edge_below_threshold")

    def test_more_challengers_raise_the_bar(self):
        gate = PromotionGate(min_oos_trades=30, min_edge_r=0.05)
        champ = [mk_trade(i, 0.0) for i in range(50)]
        chal = [mk_trade(i, 0.15) for i in range(50)]
        one = gate.evaluate("c", champ, "x", chal, challengers_tested=1)
        many = gate.evaluate("c", champ, "x", chal, challengers_tested=20)
        self.assertLessEqual(one.min_required, many.min_required)


class SplitEvaluationTest(unittest.TestCase):
    def test_split_partitions_by_time(self):
        # Trades spread across a day; the OOS slice is the tail.
        trades = [mk_trade(1000 * i, 0.1) for i in range(1, 90)]
        ev = evaluate_split(trades, 0, 90 * 1000, train=0.66, folds=3)
        # IS and OOS are disjoint, non-empty slices of the whole span.
        self.assertGreater(ev.in_sample.n, 0)
        self.assertGreater(ev.out_of_sample.n, 0)
        self.assertLessEqual(ev.in_sample.n + ev.out_of_sample.n, len(trades))
        self.assertEqual(len(ev.folds), 3)
        # The dict form must be JSON-serialisable.
        import json
        json.dumps(ev.as_dict())


if __name__ == "__main__":
    unittest.main(verbosity=2)
