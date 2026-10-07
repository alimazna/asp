"""Deterministic tests for T15 horizon labelling and comparison."""

from __future__ import annotations

import unittest

from src.models.horizon import (
    DOWN,
    FLAT,
    UP,
    direction_label,
    label_distribution,
)
from src.models.splits import SplitError


class DirectionLabelTest(unittest.TestCase):
    def test_up(self):
        self.assertEqual(direction_label([100.0, 105.0], 0, 1, 1.0), UP)

    def test_down(self):
        self.assertEqual(direction_label([100.0, 95.0], 0, 1, 1.0), DOWN)

    def test_flat_inside_dead_band(self):
        self.assertEqual(direction_label([100.0, 100.5], 0, 1, 1.0), FLAT)

    def test_exactly_at_theta_is_flat(self):
        # Strict inequality: a move equal to the dead-band does not clear cost.
        self.assertEqual(direction_label([100.0, 101.0], 0, 1, 1.0), FLAT)

    def test_theta_zero_never_flat_on_move(self):
        self.assertEqual(direction_label([100.0, 100.001], 0, 1, 0.0), UP)

    def test_horizon_reaches_past_series(self):
        with self.assertRaises(SplitError):
            direction_label([100.0, 101.0], 1, 1, 0.5)

    def test_bad_horizon(self):
        with self.assertRaises(SplitError):
            direction_label([100.0, 101.0], 0, 0, 0.5)

    def test_negative_theta_rejected(self):
        with self.assertRaises(SplitError):
            direction_label([100.0, 101.0], 0, 1, -1.0)


class DistributionTest(unittest.TestCase):
    def test_counts_and_shares(self):
        dist = label_distribution([UP, UP, DOWN, FLAT])
        self.assertEqual(dist[UP]["count"], 2)
        self.assertAlmostEqual(dist[UP]["share"], 0.5)
        self.assertAlmostEqual(dist[DOWN]["share"], 0.25)
        self.assertAlmostEqual(dist[FLAT]["share"], 0.25)

    def test_empty_rejected(self):
        with self.assertRaises(SplitError):
            label_distribution([])

    def test_unknown_label_rejected(self):
        with self.assertRaises(SplitError):
            label_distribution(["SIDEWAYS"])


class EvaluateHorizonTest(unittest.TestCase):
    def test_evaluate_runs_and_labels_output(self):
        from src.models.demo_baseline import _synthetic_series
        from src.models.horizon import evaluate_horizon

        features, closes = _synthetic_series(1500)
        result = evaluate_horizon(features, closes, horizon=4, theta=0.6)
        self.assertEqual(result.horizon, 4)
        self.assertGreater(result.n, 0)
        self.assertIn(result.label, ("probability", "score"))
        # RULE C gate: label must match the ECE target decision.
        if result.ece < 0.05:
            self.assertEqual(result.label, "probability")
        else:
            self.assertEqual(result.label, "score")

    def test_too_few_examples_rejected(self):
        from src.models.demo_baseline import _synthetic_series
        from src.models.horizon import evaluate_horizon

        features, closes = _synthetic_series(200)
        with self.assertRaises(SplitError):
            evaluate_horizon(features, closes, horizon=1, theta=100.0)


if __name__ == "__main__":
    unittest.main()
