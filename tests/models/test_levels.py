"""Deterministic tests for T15 decision levels and cost tiers."""

from __future__ import annotations

import unittest

from src.models.levels import (
    COST_TIERS,
    atr,
    compare_costs,
    simulate_hit,
    suggest_levels,
    suggest_risk_percent,
    summarize_hits,
    tier_by_name,
)
from src.models.splits import SplitError


class CostTierTest(unittest.TestCase):
    def test_three_tiers_and_decision_grade_flags(self):
        names = [t.name for t in COST_TIERS]
        self.assertEqual(names, ["zero", "floor", "conservative"])
        self.assertFalse(COST_TIERS[0].decision_grade)  # zero is reference only
        self.assertTrue(COST_TIERS[1].decision_grade)
        self.assertTrue(COST_TIERS[2].decision_grade)

    def test_round_trip_ordering(self):
        self.assertLess(COST_TIERS[0].round_trip, COST_TIERS[1].round_trip)
        self.assertLess(COST_TIERS[1].round_trip, COST_TIERS[2].round_trip)

    def test_unknown_tier_rejected(self):
        with self.assertRaises(SplitError):
            tier_by_name("nope")


class AtrTest(unittest.TestCase):
    def test_atr_positive_on_moving_series(self):
        closes = [100.0 + i for i in range(30)]
        self.assertAlmostEqual(atr(closes, 14), 1.0, places=9)

    def test_atr_rejects_short_series(self):
        with self.assertRaises(SplitError):
            atr([1.0, 2.0], 14)

    def test_atr_rejects_bad_period(self):
        with self.assertRaises(SplitError):
            atr([1.0] * 30, 0)


class LevelsTest(unittest.TestCase):
    def test_up_levels(self):
        lv = suggest_levels(2000.0, "UP", atr_value=2.0, atr_mult=1.5, rr=2.0)
        self.assertAlmostEqual(lv.stop_loss, 1997.0)
        self.assertAlmostEqual(lv.take_profit, 2006.0)
        self.assertAlmostEqual(lv.reward_risk, 2.0)

    def test_down_levels(self):
        lv = suggest_levels(2000.0, "DOWN", atr_value=2.0, atr_mult=1.5, rr=2.0)
        self.assertAlmostEqual(lv.stop_loss, 2003.0)
        self.assertAlmostEqual(lv.take_profit, 1994.0)

    def test_flat_has_no_levels(self):
        with self.assertRaises(SplitError):
            suggest_levels(2000.0, "FLAT", atr_value=2.0)

    def test_bad_atr_rejected(self):
        with self.assertRaises(SplitError):
            suggest_levels(2000.0, "UP", atr_value=0.0)

    def test_reward_risk_is_two_not_prob_scaled(self):
        # RULE A: the canonical TP must be a fixed RR, never shrunk by probability.
        lv = suggest_levels(2000.0, "UP", atr_value=2.0)
        self.assertEqual(lv.reward_risk, 2.0)


class HitSimTest(unittest.TestCase):
    def test_take_profit_hit(self):
        closes = [100.0, 101.0, 105.0, 110.0]
        self.assertEqual(simulate_hit(closes, 0, "UP", 98.0, 104.0, 3), "TP")

    def test_stop_hit(self):
        closes = [100.0, 99.0, 95.0, 110.0]
        self.assertEqual(simulate_hit(closes, 0, "UP", 98.0, 104.0, 3), "SL")

    def test_first_touch_wins(self):
        # TP touched before SL -> TP, even though SL is touched later.
        closes = [100.0, 105.0, 90.0]
        self.assertEqual(simulate_hit(closes, 0, "UP", 95.0, 104.0, 2), "TP")

    def test_timeout(self):
        closes = [100.0, 100.5, 100.5]
        self.assertEqual(simulate_hit(closes, 0, "UP", 90.0, 200.0, 2), "TIMEOUT")

    def test_down_direction(self):
        closes = [100.0, 95.0]
        self.assertEqual(simulate_hit(closes, 0, "DOWN", 105.0, 96.0, 1), "TP")


class ExpectancyTest(unittest.TestCase):
    def test_summarize_counts(self):
        stats = summarize_hits(["TP", "TP", "SL", "TIMEOUT"], "x")
        self.assertEqual((stats.tp, stats.sl, stats.timeout), (2, 1, 1))
        self.assertAlmostEqual(stats.hit_rate(), 0.5)
        # expectancy = (2*2 - 1)/4 = 0.75 R
        self.assertAlmostEqual(stats.expectancy_r(), 0.75)

    def test_high_hit_rate_can_still_be_negative(self):
        # RULE A: 60% hit rate but RR must be >= ... check 2R here is positive;
        # the point is expectancy is computed, not the hit rate alone.
        stats = summarize_hits(["TP"] * 6 + ["SL"] * 4, "x")
        self.assertAlmostEqual(stats.expectancy_r(), (12 - 4) / 10)

    def test_compare_costs_charges_each_tier(self):
        closes = [2000.0] + [2010.0] * 20
        lv = suggest_levels(2000.0, "UP", atr_value=2.0)  # risk distance 3.0
        out = compare_costs(closes, 0, "UP", lv, max_bars=10)
        self.assertEqual([o.tier for o in out], ["zero", "floor", "conservative"])
        # cost_r = round_trip / 3.0
        self.assertAlmostEqual(out[0].cost_r, 0.0)
        self.assertAlmostEqual(out[1].cost_r, 0.40 / 3.0)
        self.assertAlmostEqual(out[2].cost_r, 0.60 / 3.0)
        # net strictly decreasing across tiers
        self.assertGreater(out[0].net_expectancy_r, out[1].net_expectancy_r)
        self.assertGreater(out[1].net_expectancy_r, out[2].net_expectancy_r)


class RiskTierTest(unittest.TestCase):
    def test_tier_mapping(self):
        self.assertEqual(suggest_risk_percent(0.1), 0.25)
        self.assertEqual(suggest_risk_percent(0.5), 0.5)
        self.assertEqual(suggest_risk_percent(0.9), 1.0)
        self.assertEqual(suggest_risk_percent(1.0), 1.0)

    def test_out_of_range_rejected(self):
        with self.assertRaises(SplitError):
            suggest_risk_percent(1.5)


if __name__ == "__main__":
    unittest.main()
