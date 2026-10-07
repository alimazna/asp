"""Deterministic tests for the walk-forward scaffolding."""

from __future__ import annotations

import unittest
from datetime import datetime, timedelta, timezone

from src.models.splits import Sample, SplitError
from src.models.walk_forward import (
    WalkForwardConfig,
    assert_no_leakage,
    test_segments_overlap,
    walk_forward,
)

BASE = datetime(2021, 1, 1, tzinfo=timezone.utc)


def series(n: int):
    return [Sample(timestamp=BASE + timedelta(hours=i), payload=i) for i in range(n)]


class WalkForwardTest(unittest.TestCase):
    def test_fold_count_is_deterministic(self):
        folds = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        # offsets 0,3,6,9,12 -> 5 folds; offset 15 needs 15+5+3=23 > 20
        self.assertEqual(len(folds), 5)
        self.assertEqual([f.index for f in folds], [0, 1, 2, 3, 4])

    def test_fixed_train_and_test_sizes(self):
        folds = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        for fold in folds:
            self.assertEqual(fold.train_size, 5)
            self.assertEqual(fold.test_size, 3)

    def test_train_precedes_test_and_no_leakage(self):
        folds = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        assert_no_leakage(folds)  # must not raise
        for fold in folds:
            self.assertLess(fold.train[-1].timestamp, fold.test[0].timestamp)

    def test_default_step_gives_non_overlapping_tests(self):
        folds = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        self.assertFalse(test_segments_overlap(folds))

    def test_overlapping_step_is_detected(self):
        folds = walk_forward(
            series(20), WalkForwardConfig(train_size=5, test_size=3, step=1)
        )
        self.assertTrue(test_segments_overlap(folds))

    def test_train_windows_do_not_overlap_each_other_within_a_fold(self):
        folds = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        for fold in folds:
            train_ts = {s.timestamp for s in fold.train}
            test_ts = {s.timestamp for s in fold.test}
            self.assertEqual(train_ts & test_ts, set())

    def test_too_short_series_rejected(self):
        with self.assertRaises(SplitError):
            walk_forward(series(4), WalkForwardConfig(train_size=5, test_size=3))

    def test_non_positive_sizes_rejected(self):
        for cfg in (
            WalkForwardConfig(train_size=0, test_size=3),
            WalkForwardConfig(train_size=5, test_size=0),
            WalkForwardConfig(train_size=5, test_size=3, step=-1),
        ):
            with self.assertRaises(SplitError):
                walk_forward(series(20), cfg)

    def test_same_input_gives_identical_folds(self):
        a = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        b = walk_forward(series(20), WalkForwardConfig(train_size=5, test_size=3))
        self.assertEqual(
            [(f.index, f.train, f.test) for f in a],
            [(f.index, f.train, f.test) for f in b],
        )


if __name__ == "__main__":
    unittest.main()
