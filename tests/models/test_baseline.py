"""Deterministic tests for the T03 baseline runner (no OOS tuning)."""

from __future__ import annotations

import unittest
from datetime import datetime, timezone

from src.models.baseline import run_baseline
from src.models.dataset import LabeledExample
from src.models.splits import SplitError

BASE = int(datetime(2021, 1, 1, tzinfo=timezone.utc).timestamp())
DAY = 86400


def example(i, label, feature_value):
    return LabeledExample(
        timestamp=BASE + i * DAY,
        features={"a": feature_value, "b": 0.5},
        label=label,
        label_timestamp=BASE + (i + 1) * DAY,
    )


def dev_set():
    # feature 'a' increases with the label -> learnable.
    return [example(i, 1 if i > 5 else 0, float(i)) for i in range(12)]


def val_set():
    return [example(100 + i, 1 if i > 5 else 0, float(i)) for i in range(12)]


class BaselineRunnerTest(unittest.TestCase):
    def test_fits_and_reports(self):
        report = run_baseline(dev_set(), val_set())
        self.assertIsNotNone(report.model)
        self.assertEqual(report.development.n, 12)
        self.assertEqual(report.validation.n, 12)
        self.assertIsNone(report.oos)

    def test_learns_a_separable_signal(self):
        report = run_baseline(dev_set(), val_set())
        self.assertGreater(report.development.accuracy, 0.9)
        self.assertGreater(report.validation.accuracy, 0.9)

    def test_report_is_deterministic(self):
        a = run_baseline(dev_set(), val_set())
        b = run_baseline(dev_set(), val_set())
        self.assertEqual(a.model.weights, b.model.weights)
        self.assertEqual(a.development.calibration, b.development.calibration)

    def test_oos_only_when_provided(self):
        report = run_baseline(dev_set(), val_set(), oos=val_set())
        self.assertIsNotNone(report.oos)
        self.assertEqual(report.oos.name, "oos")

    def test_columns_fixed_from_development(self):
        report = run_baseline(dev_set(), val_set())
        self.assertEqual(report.columns, ("a", "b"))

    def test_inconsistent_columns_rejected(self):
        bad = [LabeledExample(1, {"a": 1.0, "c": 2.0}, 1, 2)]
        with self.assertRaises(SplitError):
            run_baseline(dev_set(), bad)

    def test_empty_development_rejected(self):
        with self.assertRaises(SplitError):
            run_baseline([], val_set())


if __name__ == "__main__":
    unittest.main()
