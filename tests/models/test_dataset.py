"""Deterministic tests for causal dataset assembly and purged splits."""

from __future__ import annotations

import unittest
from datetime import datetime, timezone

from src.models.dataset import (
    LabeledExample,
    build_labeled_examples,
    feature_columns,
    purge_split,
    to_matrix,
)
from src.models.features import (
    CROSS_BOUNDS,
    CROSS_DISCRETE,
    PER_TIMEFRAME_BOUNDS,
    PER_TIMEFRAME_DISCRETE,
    FeatureSet,
    FeatureVector,
)
from src.models.splits import SplitError


def tf(timeframe, asof, seed=0.0):
    values = {
        name: (low + high) / 2 + seed * 0.01
        for name, (low, high) in PER_TIMEFRAME_BOUNDS.items()
    }
    for name in PER_TIMEFRAME_DISCRETE:
        values[name] = 0.0
    return FeatureVector(timeframe, asof, values, "VALID", True)


def feature_set(asof, seed=0.0):
    return FeatureSet(
        asOfBarOpenSec=asof,
        perTimeframe=(tf("M1", asof, seed), tf("M15", asof, seed)),
        cross=None,
        quality="VALID",
        valid=True,
    )


def year_ts(year, month, day):
    return int(datetime(year, month, day, tzinfo=timezone.utc).timestamp())


class BuildExamplesTest(unittest.TestCase):
    def test_labels_use_forward_close(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 6)]
        closes = [10.0, 11.0, 10.0, 12.0, 12.0]
        examples = build_labeled_examples(sets, closes, horizon=1)
        # 5 snapshots, horizon 1 -> 4 examples.
        self.assertEqual(len(examples), 4)
        self.assertEqual([e.label for e in examples], [1, 0, 1, 0])

    def test_last_horizon_snapshots_dropped(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 6)]
        closes = [1.0] * 5
        examples = build_labeled_examples(sets, closes, horizon=2)
        self.assertEqual(len(examples), 3)

    def test_label_timestamp_is_in_the_future(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 5)]
        closes = [1.0, 2.0, 3.0, 4.0]
        for e in build_labeled_examples(sets, closes, horizon=1):
            self.assertGreater(e.label_timestamp, e.timestamp)

    def test_non_increasing_snapshots_rejected(self):
        sets = [feature_set(100), feature_set(100)]
        with self.assertRaises(SplitError):
            build_labeled_examples(sets, [1.0, 2.0], horizon=1)

    def test_length_mismatch_rejected(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 5)]
        with self.assertRaises(SplitError):
            build_labeled_examples(sets, [1.0, 2.0], horizon=1)

    def test_zero_horizon_rejected(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 5)]
        with self.assertRaises(SplitError):
            build_labeled_examples(sets, [1.0, 2.0, 3.0, 4.0], horizon=0)


class ColumnsTest(unittest.TestCase):
    def test_columns_are_sorted_and_stable(self):
        sets = [feature_set(year_ts(2021, 1, d), seed=d) for d in range(1, 5)]
        examples = build_labeled_examples(sets, [1.0, 2.0, 3.0, 4.0])
        cols = feature_columns(examples)
        self.assertEqual(list(cols), sorted(cols))

    def test_matrix_shape(self):
        sets = [feature_set(year_ts(2021, 1, d)) for d in range(1, 5)]
        examples = build_labeled_examples(sets, [1.0, 2.0, 3.0, 4.0])
        cols = feature_columns(examples)
        x, y = to_matrix(examples, cols)
        self.assertEqual(len(x), len(examples))
        self.assertEqual(len(x[0]), len(cols))
        self.assertEqual(len(y), len(examples))


class PurgeSplitTest(unittest.TestCase):
    def _examples_across_years(self):
        # 2021 (dev), 2023 (val), 2025 (oos), one per day for a few days.
        specs = [
            (2021, [1, 2, 3, 4]),
            (2023, [1, 2, 3, 4]),
            (2025, [1, 2, 3, 4]),
        ]
        sets = []
        closes = []
        for year, days in specs:
            for d in days:
                sets.append(feature_set(year_ts(year, 1, d)))
                closes.append(1.0 + d)
        return build_labeled_examples(sets, closes, horizon=1)

    def test_partitions_are_populated(self):
        split = purge_split(self._examples_across_years())
        self.assertTrue(split.development)
        self.assertTrue(split.validation)
        self.assertTrue(split.oos)

    def test_no_label_window_crosses_a_seam(self):
        split = purge_split(self._examples_across_years())
        if split.development and split.validation:
            boundary = split.validation[0].timestamp
            for e in split.development:
                self.assertLess(e.label_timestamp, boundary)
        if split.validation and split.oos:
            boundary = split.oos[0].timestamp
            for e in split.validation:
                self.assertLess(e.label_timestamp, boundary)

    def test_partitions_are_chronological(self):
        split = purge_split(self._examples_across_years())
        for part in (split.development, split.validation, split.oos):
            stamps = [e.timestamp for e in part]
            self.assertEqual(stamps, sorted(stamps))


if __name__ == "__main__":
    unittest.main()
