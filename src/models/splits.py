"""Chronological train / validation / OOS split harness.

Deterministic by construction: splits are defined by calendar year and rows are
ordered by timestamp. There is no shuffling, no random seed, and no resampling.
A row can belong to at most one partition, and partitions never overlap.

Default partition years (Agent-B Phase 2.0 directive):

    development : 2021, 2022   (train + model selection)
    validation  : 2023, 2024   (threshold / calibration fitting)
    oos         : 2025         (touched once, at the end; never tuned on)

The years are configurable, but the split is always chronological and disjoint.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable, List, Sequence, Tuple

DEVELOPMENT_YEARS: Tuple[int, ...] = (2021, 2022)
VALIDATION_YEARS: Tuple[int, ...] = (2023, 2024)
OOS_YEARS: Tuple[int, ...] = (2025,)

_EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)


class SplitError(ValueError):
    """Raised when a sample cannot be partitioned without violating causality."""


@dataclass(frozen=True)
class Sample:
    """One observation, identified by the UTC timestamp of its closed bar.

    `timestamp` is the only field the harness needs to partition rows; payload
    (features, label) is opaque to this module.
    """

    timestamp: datetime
    payload: object = None


@dataclass(frozen=True)
class Split:
    """A disjoint chronological partition of a sample set."""

    development: List[Sample]
    validation: List[Sample]
    oos: List[Sample]

    def as_dict(self) -> dict:
        return {
            "development": list(self.development),
            "validation": list(self.validation),
            "oos": list(self.oos),
        }


def year_of(timestamp: datetime) -> int:
    """Calendar year of a timestamp, normalised to UTC first."""
    if timestamp.tzinfo is None:
        raise SplitError("timestamp must be timezone-aware (UTC)")
    return timestamp.astimezone(timezone.utc).year


def _sorted_unique(samples: Iterable[Sample]) -> List[Sample]:
    ordered = sorted(samples, key=lambda s: s.timestamp)
    for previous, current in zip(ordered, ordered[1:]):
        if current.timestamp == previous.timestamp:
            raise SplitError(
                "duplicate timestamp in sample set: " + current.timestamp.isoformat()
            )
    return ordered


def chronological_split(
    samples: Sequence[Sample],
    development_years: Sequence[int] = DEVELOPMENT_YEARS,
    validation_years: Sequence[int] = VALIDATION_YEARS,
    oos_years: Sequence[int] = OOS_YEARS,
) -> Split:
    """Partition `samples` into development / validation / OOS by year.

    Guarantees:
      * partitions are disjoint (a year belongs to exactly one partition);
      * each partition is chronologically ordered, ascending;
      * no sample is dropped or duplicated.

    Raises SplitError on overlapping partition definitions, duplicate
    timestamps, or a sample whose year is covered by no partition.
    """
    development_years = tuple(development_years)
    validation_years = tuple(validation_years)
    oos_years = tuple(oos_years)

    groups = {
        "development": set(development_years),
        "validation": set(validation_years),
        "oos": set(oos_years),
    }
    covered: set = set()
    for name, years in groups.items():
        overlap = covered & years
        if overlap:
            raise SplitError(
                f"partition '{name}' overlaps another partition on years {sorted(overlap)}"
            )
        covered |= years
    if not covered:
        raise SplitError("no partition years configured")

    buckets: dict = {"development": [], "validation": [], "oos": []}
    for sample in _sorted_unique(samples):
        year = year_of(sample.timestamp)
        if year in groups["development"]:
            buckets["development"].append(sample)
        elif year in groups["validation"]:
            buckets["validation"].append(sample)
        elif year in groups["oos"]:
            buckets["oos"].append(sample)
        else:
            raise SplitError(
                f"sample year {year} is not covered by any partition "
                f"(covered: {sorted(covered)})"
            )

    return Split(
        development=buckets["development"],
        validation=buckets["validation"],
        oos=buckets["oos"],
    )


def assert_causal(split: Split) -> None:
    """Verify the split respects chronology: every development sample precedes
    every validation sample, which precedes every OOS sample.

    This is the leakage guard for the split itself. It does not prove the
    features are causal — that is Agent-A's T02 and Agent-D's T10.
    """
    partitions = [
        ("development", split.development),
        ("validation", split.validation),
        ("oos", split.oos),
    ]
    present = [(name, rows) for name, rows in partitions if rows]
    for (name_a, rows_a), (name_b, rows_b) in zip(present, present[1:]):
        last_a = rows_a[-1].timestamp
        first_b = rows_b[0].timestamp
        if last_a >= first_b:
            raise SplitError(
                f"chronology violated: last {name_a} ({last_a.isoformat()}) "
                f"is not before first {name_b} ({first_b.isoformat()})"
            )
