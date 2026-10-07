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


if __name__ == "__main__":
    unittest.main()
