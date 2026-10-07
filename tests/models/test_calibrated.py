"""Deterministic tests for the end-to-end calibrated runner (T05)."""

from __future__ import annotations

import unittest
from datetime import datetime, timezone

from src.models.calibrated import run_calibrated
from src.models.dataset import LabeledExample
from src.models.splits import SplitError

BASE = int(datetime(2021, 1, 1, tzinfo=timezone.utc).timestamp())
DAY = 86400


def example(i, label, a, b=0.5):
    return LabeledExample(
        timestamp=BASE + i * DAY,
        features={"a": a, "b": b},
        label=label,
        label_timestamp=BASE + (i + 1) * DAY,
    )


def partition(start, n, overconfident=True):
    rows = []
    for k in range(n):
        # a in [-2, 2]; label driven by the sign of a.
        a = -2.0 + 4.0 * (k + 0.5) / n
        label = 1 if a > 0 else 0
        rows.append(example(start + k, label, a))
    return rows


class CalibratedRunnerTest(unittest.TestCase):
    def test_runs_all_three_methods(self):
        for method in ("platt", "isotonic", "histogram"):
            report = run_calibrated(
                partition(0, 60), partition(1000, 60), partition(2000, 60), method
            )
            self.assertEqual(report.method, method)
            self.assertIn("validation", report.calibrated)
            self.assertIn("oos", report.calibrated)

    def test_development_is_raw_only(self):
        report = run_calibrated(partition(0, 60), partition(1000, 60))
        self.assertIn("development", report.raw)
        self.assertNotIn("development", report.calibrated)

    def test_oos_absent_unless_supplied(self):
        report = run_calibrated(partition(0, 60), partition(1000, 60))
        self.assertNotIn("oos", report.raw)

    def test_oos_present_when_supplied(self):
        report = run_calibrated(
            partition(0, 60), partition(1000, 60), partition(2000, 60)
        )
        self.assertIn("oos", report.raw)
        self.assertIn("oos", report.calibrated)

    def test_calibration_does_not_hurt_on_clean_signal(self):
        # On a clean separable signal the calibrated Brier should stay strong.
        report = run_calibrated(
            partition(0, 80), partition(1000, 80), partition(2000, 80), "platt"
        )
        self.assertLess(report.calibrated["oos"].calibration.brier, 0.25)

    def test_deterministic(self):
        a = run_calibrated(partition(0, 60), partition(1000, 60), partition(2000, 60))
        b = run_calibrated(partition(0, 60), partition(1000, 60), partition(2000, 60))
        self.assertEqual(
            a.calibrated["oos"].calibration, b.calibrated["oos"].calibration
        )

    def test_empty_development_rejected(self):
        with self.assertRaises(SplitError):
            run_calibrated([], partition(1000, 60))

    def test_empty_validation_rejected(self):
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), [])

    def test_inconsistent_columns_rejected(self):
        bad = [LabeledExample(1, {"a": 1.0, "z": 0.0}, 1, 2)]
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), bad)

    def test_overlapping_dev_val_rejected(self):
        # F1 (T05 audit): the runner must not fit and score on the same rows.
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), partition(0, 60))

    def test_validation_equal_to_oos_rejected(self):
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), partition(1000, 60), partition(1000, 60))

    def test_chronological_inversion_rejected(self):
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), partition(2000, 60), partition(1000, 60))

    def test_unknown_method_rejected(self):
        with self.assertRaises(SplitError):
            run_calibrated(partition(0, 60), partition(1000, 60), method="nope")


if __name__ == "__main__":
    unittest.main()
