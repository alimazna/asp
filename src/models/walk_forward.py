"""Walk-forward scaffolding.

Deterministic rolling windows with a fixed train size and a fixed test size.
No shuffling, no randomisation, no overlap between a window's train and test
segments. The number of folds is a pure function of (n, train_size, test_size,
step), so two runs over the same input always yield the same folds.

Semantics
---------
    fold k train = samples[offset_k            : offset_k + train_size]
    fold k test  = samples[offset_k + train_size : offset_k + train_size + test_size]
    offset_{k+1}  = offset_k + step

`step` defaults to `test_size`, which gives non-overlapping test segments
(standard expanding-window-free walk-forward). A smaller step yields
overlapping test segments, which is allowed but must be reported, because
overlapping test windows are not independent samples.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence

from src.models.splits import Sample, SplitError


@dataclass(frozen=True)
class Fold:
    """One walk-forward fold: a train slice and the test slice that follows it."""

    index: int
    train: List[Sample]
    test: List[Sample]

    @property
    def train_size(self) -> int:
        return len(self.train)

    @property
    def test_size(self) -> int:
        return len(self.test)


@dataclass(frozen=True)
class WalkForwardConfig:
    train_size: int
    test_size: int
    step: int = 0  # 0 means "use test_size"; a negative step is invalid

    def resolved_step(self) -> int:
        if self.step < 0:
            raise SplitError("step must not be negative")
        return self.step if self.step > 0 else self.test_size


def walk_forward(
    samples: Sequence[Sample],
    config: WalkForwardConfig,
) -> List[Fold]:
    """Build the deterministic list of walk-forward folds.

    Raises SplitError for non-positive sizes or when the sample is too short to
    produce even one fold. Never shuffles and never reorders the input; the
    caller must pass chronologically ordered samples (see `splits`).
    """
    if config.train_size <= 0:
        raise SplitError("train_size must be positive")
    if config.test_size <= 0:
        raise SplitError("test_size must be positive")
    step = config.resolved_step()
    if step <= 0:
        raise SplitError("step must be positive")

    total = len(samples)
    folds: List[Fold] = []
    offset = 0
    index = 0
    while offset + config.train_size + config.test_size <= total:
        train = list(samples[offset : offset + config.train_size])
        test = list(
            samples[offset + config.train_size : offset + config.train_size + config.test_size]
        )
        folds.append(Fold(index=index, train=train, test=test))
        offset += step
        index += 1

    if not folds:
        raise SplitError(
            f"sample of {total} rows is too short for train_size="
            f"{config.train_size} + test_size={config.test_size}"
        )
    return folds


def test_segments_overlap(folds: Sequence[Fold]) -> bool:
    """True when any two folds' test segments share a sample timestamp.

    Callers must surface this: overlapping test windows violate the
    independence assumption behind pooled OOS metrics (RULE D — coverage
    honesty).
    """
    seen: set = set()
    for fold in folds:
        for sample in fold.test:
            if sample.timestamp in seen:
                return True
            seen.add(sample.timestamp)
    return False


def assert_no_leakage(folds: Sequence[Fold]) -> None:
    """Verify each fold's train strictly precedes its test.

    This is the walk-forward leakage guard: it proves the train window closes
    before the test window opens, so no test bar can inform its own training.
    """
    for fold in folds:
        if not fold.train or not fold.test:
            raise SplitError(f"fold {fold.index} has an empty train or test slice")
        last_train = fold.train[-1].timestamp
        first_test = fold.test[0].timestamp
        if last_train >= first_test:
            raise SplitError(
                f"fold {fold.index} leakage: last train ({last_train.isoformat()}) "
                f"is not before first test ({first_test.isoformat()})"
            )
