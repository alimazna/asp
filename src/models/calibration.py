"""Calibration utilities (skeleton) — deterministic, dependency-free.

Implements the mission's required measurement surface:

    * Brier score            (success: better than the 0.25 uninformative baseline)
    * Expected Calibration Error, ECE  (success: ECE < 0.05, failure: ECE > 0.10)
    * Reliability diagram data (bin-by-bin predicted vs empirical frequency)
    * Coverage analysis per probability tier (RULE D — coverage honesty)

Everything here is a pure function of its inputs. No fitting, no randomisation,
no external packages. These functions *measure* calibration; they do not
*establish* it, and per RULE C nothing here may be used to publish a
probability until Agent-D has audited the calibration (T11).

Vocabulary (MISSION.md §8): a probability is a claim about frequency. A model is
calibrated when, among all forecasts it made with probability ~p, the observed
frequency is ~p. ECE measures the average gap; Brier measures overall skill.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence

from src.models.splits import SplitError

# The mission's uninformative baseline: a constant 0.5 forecast scores 0.25.
BRIER_BASELINE = 0.25
ECE_TARGET = 0.05
ECE_FAILURE = 0.10

# Coverage tiers for RULE D. Tiers partition the probability axis; every
# forecast falls in exactly one tier.
TIER_BOUNDS = (
    ("low", 0.0, 1.0 / 3.0),
    ("medium", 1.0 / 3.0, 2.0 / 3.0),
    ("high", 2.0 / 3.0, 1.0),
)


def _validate(probabilities: Sequence[float], outcomes: Sequence[int]) -> None:
    if len(probabilities) != len(outcomes):
        raise SplitError(
            f"length mismatch: {len(probabilities)} probabilities vs "
            f"{len(outcomes)} outcomes"
        )
    if not probabilities:
        raise SplitError("empty sample")
    for p in probabilities:
        if not (0.0 <= p <= 1.0):
            raise SplitError(f"probability out of range [0,1]: {p}")
    for y in outcomes:
        if y not in (0, 1):
            raise SplitError(f"outcome must be 0 or 1, got {y!r}")


def brier_score(probabilities: Sequence[float], outcomes: Sequence[int]) -> float:
    """Mean squared error between forecast probability and 0/1 outcome.

    Lower is better. 0.25 is the uninformative 0.5-forecast baseline.
    """
    _validate(probabilities, outcomes)
    total = 0.0
    for p, y in zip(probabilities, outcomes):
        diff = p - y
        total += diff * diff
    return total / len(probabilities)


def brier_skill_score(probabilities: Sequence[float], outcomes: Sequence[int]) -> float:
    """Skill relative to the 0.25 baseline: 1 - (brier / 0.25).

    > 0 means better than uninformative; == 0 means no skill (a failure);
    < 0 means worse than a constant 0.5 forecast.
    """
    return 1.0 - (brier_score(probabilities, outcomes) / BRIER_BASELINE)


def _bin_index(p: float, bins: int) -> int:
    """Deterministic equal-width bin index in [0, bins-1] over [0, 1].

    p == 1.0 lands in the last bin rather than overflowing.
    """
    if p >= 1.0:
        return bins - 1
    index = int(p * bins)
    if index < 0:
        return 0
    if index >= bins:
        return bins - 1
    return index


@dataclass(frozen=True)
class BinStats:
    index: int
    lower: float
    upper: float
    count: int
    mean_predicted: float
    empirical_rate: float

    @property
    def gap(self) -> float:
        """Signed calibration gap: empirical - predicted.

        Positive means the model under-forecasts in this bin; negative means it
        over-forecasts. The absolute value is what ECE averages.
        """
        return self.empirical_rate - self.mean_predicted


def reliability_diagram(
    probabilities: Sequence[float], outcomes: Sequence[int], bins: int = 10
) -> List[BinStats]:
    """Bin-by-bin predicted vs empirical frequency. Empty bins are omitted.

    The returned list is ordered by ascending bin index, so it is directly
    renderable and reproducible.
    """
    _validate(probabilities, outcomes)
    if bins <= 0:
        raise SplitError("bins must be positive")

    counts = [0] * bins
    pred_sum = [0.0] * bins
    outcome_sum = [0] * bins
    for p, y in zip(probabilities, outcomes):
        b = _bin_index(p, bins)
        counts[b] += 1
        pred_sum[b] += p
        outcome_sum[b] += y

    width = 1.0 / bins
    stats: List[BinStats] = []
    for b in range(bins):
        if counts[b] == 0:
            continue
        stats.append(
            BinStats(
                index=b,
                lower=b * width,
                upper=(b + 1) * width,
                count=counts[b],
                mean_predicted=pred_sum[b] / counts[b],
                empirical_rate=outcome_sum[b] / counts[b],
            )
        )
    return stats


def expected_calibration_error(
    probabilities: Sequence[float], outcomes: Sequence[int], bins: int = 10
) -> float:
    """ECE: sample-weighted mean absolute gap between confidence and accuracy.

    ECE = sum_b (n_b / N) * |acc_b - conf_b|. Bins with no samples contribute
    nothing. This is the mission's headline calibration metric.
    """
    _validate(probabilities, outcomes)
    diagram = reliability_diagram(probabilities, outcomes, bins=bins)
    n = len(probabilities)
    return sum((b.count / n) * abs(b.gap) for b in diagram)


def maximum_calibration_error(
    probabilities: Sequence[float], outcomes: Sequence[int], bins: int = 10
) -> float:
    """Worst-bin absolute gap. A single badly-calibrated bin shows up here even
    when ECE is small, so it is reported alongside ECE rather than instead of it.
    """
    _validate(probabilities, outcomes)
    diagram = reliability_diagram(probabilities, outcomes, bins=bins)
    return max((abs(b.gap) for b in diagram), default=0.0)


@dataclass(frozen=True)
class TierCoverage:
    tier: str
    lower: float
    upper: float
    count: int
    coverage: float          # fraction of the whole sample in this tier
    accuracy: float          # empirical positive rate within the tier
    mean_probability: float  # mean forecast within the tier

    @property
    def gap(self) -> float:
        return self.accuracy - self.mean_probability


def coverage_analysis(
    probabilities: Sequence[float], outcomes: Sequence[int]
) -> List[TierCoverage]:
    """Coverage and accuracy per probability tier (low / medium / high).

    RULE D — coverage honesty. This is how we avoid presenting a
    high-confidence subset as if it were the whole sample: coverage is always
    reported next to accuracy, per tier. Empty tiers are returned with
    count 0 and coverage 0.0 so their absence is visible, never hidden.
    """
    _validate(probabilities, outcomes)
    n = len(probabilities)
    results: List[TierCoverage] = []
    for name, lower, upper in TIER_BOUNDS:
        members = [
            (p, y)
            for p, y in zip(probabilities, outcomes)
            if lower <= p < upper or (name == "high" and p >= upper)
        ]
        count = len(members)
        if count == 0:
            results.append(TierCoverage(name, lower, upper, 0, 0.0, 0.0, 0.0))
            continue
        accuracy = sum(y for _, y in members) / count
        mean_p = sum(p for p, _ in members) / count
        results.append(
            TierCoverage(
                tier=name,
                lower=lower,
                upper=upper,
                count=count,
                coverage=count / n,
                accuracy=accuracy,
                mean_probability=mean_p,
            )
        )
    return results


@dataclass(frozen=True)
class CalibrationReport:
    n: int
    brier: float
    brier_skill: float
    ece: float
    mce: float
    bins: int
    diagram: List[BinStats]
    coverage: List[TierCoverage]

    def meets_target(self) -> bool:
        """Mission success gate: ECE < 0.05 and Brier better than 0.25."""
        return self.ece < ECE_TARGET and self.brier < BRIER_BASELINE

    def is_failure(self) -> bool:
        """Mission failure gate: ECE > 0.10 or no skill over baseline."""
        return self.ece > ECE_FAILURE or self.brier >= BRIER_BASELINE


def calibration_report(
    probabilities: Sequence[float], outcomes: Sequence[int], bins: int = 10
) -> CalibrationReport:
    """Compute the full calibration surface in one deterministic pass."""
    _validate(probabilities, outcomes)
    return CalibrationReport(
        n=len(probabilities),
        brier=brier_score(probabilities, outcomes),
        brier_skill=brier_skill_score(probabilities, outcomes),
        ece=expected_calibration_error(probabilities, outcomes, bins=bins),
        mce=maximum_calibration_error(probabilities, outcomes, bins=bins),
        bins=bins,
        diagram=reliability_diagram(probabilities, outcomes, bins=bins),
        coverage=coverage_analysis(probabilities, outcomes),
    )
