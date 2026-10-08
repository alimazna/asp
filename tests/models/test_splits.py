"""Deterministic tests for the chronological split harness (T05 preparation)."""

from __future__ import annotations

import unittest
from datetime import datetime, timezone

from src.models.splits import (
    DEVELOPMENT_YEARS,
    OOS_YEARS,
    VALIDATION_YEARS,
    Sample,
    SplitError,
    assert_causal,
    chronological_split,
    fractional_split,
    year_of,
)


def ts(year: int, month: int = 6, day: int = 1) -> datetime:
    return datetime(year, month, day, tzinfo=timezone.utc)


def make_samples(years_and_counts):
    samples = []
    for year, count in years_and_counts:
        for i in range(count):
            samples.append(Sample(timestamp=ts(year, 1 + (i % 12), 1 + (i % 27)), payload=i))
    return sorted(samples, key=lambda s: s.timestamp)


class YearOfTest(unittest.TestCase):
    def test_year_is_utc(self):
        self.assertEqual(year_of(ts(2023)), 2023)

    def test_naive_timestamp_rejected(self):
        with self.assertRaises(SplitError):
            year_of(datetime(2023, 1, 1))


class ChronologicalSplitTest(unittest.TestCase):
    def test_default_years_are_disjoint(self):
        self.assertEqual(set(DEVELOPMENT_YEARS) & set(VALIDATION_YEARS), set())
        self.assertEqual(set(DEVELOPMENT_YEARS) & set(OOS_YEARS), set())
        self.assertEqual(set(VALIDATION_YEARS) & set(OOS_YEARS), set())

    def test_partitions_by_year(self):
        samples = make_samples([(2021, 5), (2022, 4), (2023, 3), (2024, 2), (2025, 6)])
        split = chronological_split(samples)
        self.assertEqual(len(split.development), 9)
        self.assertEqual(len(split.validation), 5)
        self.assertEqual(len(split.oos), 6)
        self.assertEqual(
            len(split.development) + len(split.validation) + len(split.oos),
            len(samples),
        )

    def test_each_partition_is_ascending(self):
        samples = make_samples([(2021, 5), (2023, 3), (2025, 6)])
        split = chronological_split(samples)
        for rows in (split.development, split.validation, split.oos):
            stamps = [s.timestamp for s in rows]
            self.assertEqual(stamps, sorted(stamps))

    def test_no_sample_is_duplicated_across_partitions(self):
        samples = make_samples([(2021, 5), (2023, 3), (2025, 6)])
        split = chronological_split(samples)
        all_ts = [s.timestamp for s in split.development + split.validation + split.oos]
        self.assertEqual(len(all_ts), len(set(all_ts)))

    def test_assert_causal_passes(self):
        samples = make_samples([(2021, 5), (2023, 3), (2025, 6)])
        assert_causal(chronological_split(samples))  # must not raise

    def test_uncovered_year_rejected(self):
        samples = make_samples([(2021, 2), (2019, 2)])
        with self.assertRaises(SplitError):
            chronological_split(samples)

    def test_duplicate_timestamp_rejected(self):
        samples = [Sample(ts(2021, 1, 1)), Sample(ts(2021, 1, 1))]
        with self.assertRaises(SplitError):
            chronological_split(samples)

    def test_overlapping_partition_years_rejected(self):
        samples = make_samples([(2021, 2)])
        with self.assertRaises(SplitError):
            chronological_split(
                samples,
                development_years=(2021, 2022),
                validation_years=(2022, 2023),
                oos_years=(2025,),
            )

    def test_empty_partition_definitions_rejected(self):
        with self.assertRaises(SplitError):
            chronological_split(
                make_samples([(2021, 1)]),
                development_years=(),
                validation_years=(),
                oos_years=(),
            )


class FractionalSplitTest(unittest.TestCase):
    def _window(self, n=100):
        # n hourly samples inside a single day -> one time window, no years change.
        base = datetime(2026, 6, 24, tzinfo=timezone.utc)
        return [
            Sample(timestamp=base.replace(hour=i % 24, day=24 + (i // 24)), payload=i)
            for i in range(n)
        ]

    def test_partitions_are_disjoint_and_ordered(self):
        split = fractional_split(self._window(), 0.6, 0.2)
        self.assertTrue(split.development and split.validation and split.oos)
        assert_causal(split)  # must not raise
        total = len(split.development) + len(split.validation) + len(split.oos)
        self.assertEqual(total, 100)

    def test_fractions_are_respected(self):
        split = fractional_split(self._window(), 0.5, 0.25)
        self.assertEqual(len(split.development), 50)
        self.assertEqual(len(split.validation), 25)
        self.assertEqual(len(split.oos), 25)

    def test_oos_is_the_last_partition(self):
        split = fractional_split(self._window(), 0.6, 0.2)
        self.assertLess(split.validation[-1].timestamp, split.oos[0].timestamp)

    def test_bad_fractions_rejected(self):
        for d, v in ((0.0, 0.2), (0.9, 0.2), (1.0, 0.0)):
            with self.assertRaises(SplitError):
                fractional_split(self._window(), d, v)

    def test_empty_rejected(self):
        with self.assertRaises(SplitError):
            fractional_split([])

    def test_zero_span_rejected(self):
        one = [Sample(timestamp=datetime(2026, 6, 24, tzinfo=timezone.utc))]
        with self.assertRaises(SplitError):
            fractional_split(one)


if __name__ == "__main__":
    unittest.main()
