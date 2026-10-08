"""T27 — real-data calibration runner (E05 closer).

Feeds Agent-A's frozen `FeatureSet` JSON (plus the M15 close per instant) through
the T05 calibrated pipeline and reports the RULE C verdict. This is the same
measurement the synthetic demos perform, but on real features; the difference is
the *decision*, not the code path.

Data contract
-------------
The corpus is a directory of JSON files. Each file is either one `FeatureSet`
object (the interchange shape `src/models/features.py:parse_feature_set`
validates) or an array of them. Each row also carries its decision-bar close as a
**top-level sibling key** — by default ``close`` — because the label needs the
forward price move and labels are a *model-side* concern, never recomputed in
Agent-A's feature zone. (`parse_feature_set` reads only the FeatureSet fields, so
the sibling is ignored by the feature contract.) A row missing its close is an
error, not interpolation (data-phase rule: no silent interpolation).

The path is fully configurable (T28): pass ``corpus_dir`` / ``--corpus`` and
``close_key`` / ``--close-key``. Nothing here hardcodes a data location.

Honesty (RULE C / D / E)
------------------------
* RULE C — the calibration numbers are always reported. The *verdict* is a
  separate, explicit decision: publish as a probability only if OOS ECE < 0.05;
  0.05..0.10 publishes as a labelled score; > 0.10 reports the numbers and
  recommends a pivot. The runner never dresses a failure up as a probability.
* RULE D — per-tier coverage is reported next to accuracy; the tier mix is
  visible, never hidden. Empty partitions/tiers are stated, not fabricated.
* RULE E — a negative result is recorded (the report carries the numbers and the
  verdict even when the verdict is a failure).

Nothing here publishes a probability to the API surface. `/analysis/latest` stays
uncalibrated (E05) until the T29 audit signs off.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from typing import Callable, Dict, List, Optional, Sequence, Tuple

from src.models.baseline import PartitionMetrics
from src.models.calibrated import CalibratedReport, run_calibrated
from src.models.calibration import (
    ECE_FAILURE,
    ECE_TARGET,
    TIER_BOUNDS,
    TierCoverage,
    coverage_analysis,
)
from src.models.dataset import LabeledExample, assert_partitions_separated, build_labeled_examples
from src.models.features import FeatureSet, parse_feature_set
from src.models.splits import (
    DEVELOPMENT_YEARS,
    OOS_YEARS,
    VALIDATION_YEARS,
    Sample,
    SplitError,
    assert_causal,
    chronological_split,
)

DEFAULT_CLOSE_KEY = "close"

# RULE C verdicts.
PUBLISH_PROBABILITY = "probability"
PUBLISH_SCORE = "score"
REPORT_AND_PIVOT = "report_and_pivot"
CANNOT_PUBLISH = "cannot_publish"


# --------------------------------------------------------------------------
# Corpus loading
# --------------------------------------------------------------------------


def _iter_json_files(corpus_dir: str) -> List[str]:
    paths: List[str] = []
    for root, _dirs, files in os.walk(corpus_dir):
        for name in sorted(files):
            if name.endswith(".json"):
                paths.append(os.path.join(root, name))
    return sorted(paths)


def _coerce_rows(doc) -> List[Dict]:
    if isinstance(doc, list):
        return list(doc)
    return [doc]


def load_corpus(
    corpus_dir: str, close_key: str = DEFAULT_CLOSE_KEY
) -> Tuple[List[FeatureSet], List[float]]:
    """Load a corpus directory into ordered (feature_sets, closes).

    Each document is a FeatureSet object (or an array of them) carrying its
    decision-bar close as a top-level sibling key (`close_key`). Raises
    `SplitError` on a missing directory, an empty corpus, a missing/non-finite
    close, or duplicate instants. Never interpolates.
    """
    if not os.path.isdir(corpus_dir):
        raise SplitError(f"corpus directory does not exist: {corpus_dir}")
    paths = _iter_json_files(corpus_dir)
    if not paths:
        raise SplitError(f"no .json feature files under {corpus_dir}")

    rows: List[Tuple[int, FeatureSet, float]] = []
    for path in paths:
        with open(path, "r", encoding="utf-8") as handle:
            doc = json.load(handle)
        for entry in _coerce_rows(doc):
            if not isinstance(entry, dict):
                raise SplitError(f"{path}: feature entry is not an object")
            if close_key not in entry:
                raise SplitError(
                    f"{path}: feature row {entry.get('asOfBarOpenSec')!r} is "
                    f"missing its decision-bar close under '{close_key}' "
                    "(no interpolation)"
                )
            close = entry[close_key]
            if not isinstance(close, (int, float)) or isinstance(close, bool):
                raise SplitError(f"{path}: close must be numeric, got {close!r}")
            close = float(close)
            if not math.isfinite(close):
                raise SplitError(
                    f"{path}: non-finite close for "
                    f"{entry.get('asOfBarOpenSec')!r}: {close!r}"
                )
            fs = parse_feature_set(entry)
            rows.append((fs.asOfBarOpenSec, fs, close))

    rows.sort(key=lambda item: item[0])
    for (t0, _, _), (t1, _, _) in zip(rows, rows[1:]):
        if t1 == t0:
            raise SplitError(f"duplicate decision instant in corpus: {t0}")

    feature_sets = [row[1] for row in rows]
    closes = [row[2] for row in rows]
    return feature_sets, closes


# --------------------------------------------------------------------------
# RULE C verdict
# --------------------------------------------------------------------------


def rule_c_verdict(ece: float) -> Tuple[str, str]:
    """Map an OOS ECE to (verdict, human note). Strict boundaries per MISSION."""
    if ece < ECE_TARGET:
        return (
            PUBLISH_PROBABILITY,
            f"ECE {ece:.4f} < {ECE_TARGET}: calibration is within target; the "
            "value may be published as a probability once the T29 audit passes.",
        )
    if ece <= ECE_FAILURE:
        return (
            PUBLISH_SCORE,
            f"ECE {ece:.4f} in [{ECE_TARGET}, {ECE_FAILURE}]: publish as a "
            "'score', explicitly labelled — not as a probability.",
        )
    return (
        REPORT_AND_PIVOT,
        f"ECE {ece:.4f} > {ECE_FAILURE}: calibration does not hold; report the "
        "numbers and recommend a pivot. Do not present as a probability.",
    )


# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class RealCalibrationReport:
    corpus_dir: str
    close_key: str
    label_horizon: int
    method: str
    n_instants: int
    n_examples: int
    partition_sizes: Dict[str, int]
    raw_oos: Optional[PartitionMetrics]
    calibrated_oos: Optional[PartitionMetrics]
    coverage: Tuple[TierCoverage, ...]
    verdict: str
    note: str
    notes: Tuple[str, ...] = field(default_factory=tuple)

    @property
    def ece_oos(self) -> Optional[float]:
        return self.calibrated_oos.calibration.ece if self.calibrated_oos else None

    @property
    def brier_oos(self) -> Optional[float]:
        return self.calibrated_oos.calibration.brier if self.calibrated_oos else None

    @property
    def brier_skill_oos(self) -> Optional[float]:
        return self.calibrated_oos.calibration.brier_skill if self.calibrated_oos else None

    def to_dict(self) -> dict:
        def metric(m: Optional[PartitionMetrics]):
            if m is None:
                return None
            return {
                "name": m.name,
                "n": m.n,
                "accuracy": m.accuracy,
                "brier": m.calibration.brier,
                "brier_skill": m.calibration.brier_skill,
                "ece": m.calibration.ece,
                "mce": m.calibration.mce,
                "meets_target": m.calibration.meets_target(),
                "is_failure": m.calibration.is_failure(),
            }

        return {
            "corpus_dir": self.corpus_dir,
            "close_key": self.close_key,
            "label_horizon": self.label_horizon,
            "method": self.method,
            "n_instants": self.n_instants,
            "n_examples": self.n_examples,
            "partition_sizes": dict(self.partition_sizes),
            "raw_oos": metric(self.raw_oos),
            "calibrated_oos": metric(self.calibrated_oos),
            "coverage": [asdict(c) for c in self.coverage],
            "verdict": self.verdict,
            "note": self.note,
            "notes": list(self.notes),
        }

    def summary(self) -> str:
        lines = [
            f"T27 real-data calibration — corpus={self.corpus_dir}",
            f"  instants={self.n_instants} examples={self.n_examples} "
            f"partitions={self.partition_sizes} horizon={self.label_horizon} "
            f"method={self.method}",
        ]
        if self.calibrated_oos is not None:
            m = self.calibrated_oos.calibration
            lines.append(
                f"  OOS calibrated: brier={m.brier:.4f} "
                f"(skill={m.brier_skill:.4f}) ece={m.ece:.4f} mce={m.mce:.4f} "
                f"n={self.calibrated_oos.n}"
            )
            if self.raw_oos is not None:
                lines.append(
                    f"  OOS raw:        brier={self.raw_oos.calibration.brier:.4f} "
                    f"ece={self.raw_oos.calibration.ece:.4f}"
                )
            for tier in self.coverage:
                lines.append(
                    f"    tier {tier.tier:<6} coverage={tier.coverage:.3f} "
                    f"n={tier.count} accuracy={tier.accuracy:.3f} "
                    f"mean_p={tier.mean_probability:.3f} gap={tier.gap:+.3f}"
                )
        else:
            lines.append("  OOS: not available (partition empty)")
        lines.append(f"  VERDICT: {self.verdict} — {self.note}")
        for extra in self.notes:
            lines.append(f"  note: {extra}")
        return "\n".join(lines)


# --------------------------------------------------------------------------
# Runner
# --------------------------------------------------------------------------


def _to_examples(
    feature_sets: Sequence[FeatureSet], closes: Sequence[float], label_horizon: int
) -> List[LabeledExample]:
    return build_labeled_examples(feature_sets, closes, horizon=label_horizon)


def _partition(
    examples: Sequence[LabeledExample],
    development_years: Sequence[int],
    validation_years: Sequence[int],
    oos_years: Sequence[int],
):
    samples = [
        Sample(datetime.fromtimestamp(ex.timestamp, tz=timezone.utc), ex)
        for ex in examples
    ]
    split = chronological_split(
        samples, development_years, validation_years, oos_years
    )
    assert_causal(split)  # leak guard: dev < val < oos in time
    return split


def run_real_calibration(
    corpus_dir: str,
    *,
    close_key: str = DEFAULT_CLOSE_KEY,
    label_horizon: int = 1,
    method: str = "platt",
    model_factory: Optional[Callable] = None,
    development_years: Sequence[int] = DEVELOPMENT_YEARS,
    validation_years: Sequence[int] = VALIDATION_YEARS,
    oos_years: Sequence[int] = OOS_YEARS,
) -> RealCalibrationReport:
    """Run the T05 calibrated pipeline on a real corpus and apply the RULE C gate.

    Never tunes on OOS and never interpolates missing data. If a partition is
    empty the report says so and the verdict is `cannot_publish` — the absence is
    surfaced, not hidden.
    """
    feature_sets, closes = load_corpus(corpus_dir, close_key=close_key)
    examples = _to_examples(feature_sets, closes, label_horizon)
    split = _partition(examples, development_years, validation_years, oos_years)

    development = [s.payload for s in split.development]
    validation = [s.payload for s in split.validation]
    oos = [s.payload for s in split.oos]
    sizes = {
        "development": len(development),
        "validation": len(validation),
        "oos": len(oos),
    }

    notes: List[str] = []
    if not development or not validation:
        note = (
            "cannot fit/calibrate: development and/or validation partition is "
            "empty; the verdict is NOT a probability."
        )
        return RealCalibrationReport(
            corpus_dir=corpus_dir,
            close_key=close_key,
            label_horizon=label_horizon,
            method=method,
            n_instants=len(feature_sets),
            n_examples=len(examples),
            partition_sizes=sizes,
            raw_oos=None,
            calibrated_oos=None,
            coverage=(),
            verdict=CANNOT_PUBLISH,
            note=note,
            notes=tuple(notes),
        )

    report: CalibratedReport = run_calibrated(
        development, validation, (oos or None), method=method, model_factory=model_factory
    )

    if not oos:
        notes.append(
            "OOS partition is empty — no out-of-sample evidence; cannot publish."
        )
        return RealCalibrationReport(
            corpus_dir=corpus_dir,
            close_key=close_key,
            label_horizon=label_horizon,
            method=method,
            n_instants=len(feature_sets),
            n_examples=len(examples),
            partition_sizes=sizes,
            raw_oos=None,
            calibrated_oos=None,
            coverage=(),
            verdict=CANNOT_PUBLISH,
            note="no OOS partition: out-of-sample evidence is required to publish.",
            notes=tuple(notes),
        )

    raw_oos = report.raw.get("oos")
    calibrated_oos = report.calibrated.get("oos")
    if calibrated_oos is None:
        raise SplitError("calibrated report is missing the OOS partition")

    ece = calibrated_oos.calibration.ece
    verdict, note = rule_c_verdict(ece)

    # RULE D — per-tier coverage on the calibrated OOS probabilities. The
    # calibrated probabilities are recoverable from the report's metrics only
    # through coverage_analysis, which the CalibrationReport already carries.
    coverage = tuple(calibrated_oos.calibration.coverage)
    empty_tiers = [t.tier for t in coverage if t.count == 0]
    if empty_tiers:
        notes.append(
            f"empty tiers (zero coverage, reported not hidden): {empty_tiers}"
        )
    if calibrated_oos.calibration.mce > calibrated_oos.calibration.ece:
        notes.append(
            f"MCE {calibrated_oos.calibration.mce:.4f} exceeds ECE "
            f"{calibrated_oos.calibration.ece:.4f} (a tier is worse than average)."
        )

    return RealCalibrationReport(
        corpus_dir=corpus_dir,
        close_key=close_key,
        label_horizon=label_horizon,
        method=method,
        n_instants=len(feature_sets),
        n_examples=len(examples),
        partition_sizes=sizes,
        raw_oos=raw_oos,
        calibrated_oos=calibrated_oos,
        coverage=coverage,
        verdict=verdict,
        note=note,
        notes=tuple(notes),
    )


# --------------------------------------------------------------------------
# CLI (path configurable — T28)
# --------------------------------------------------------------------------


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="T27 real-data calibration runner")
    parser.add_argument(
        "--corpus",
        default=os.environ.get("ASTRA_FEATURE_CORPUS", ""),
        help="directory of FeatureSet JSON (env ASTRA_FEATURE_CORPUS)",
    )
    parser.add_argument(
        "--close-key",
        default=os.environ.get("ASTRA_CLOSE_KEY", DEFAULT_CLOSE_KEY),
        help="flat feature key holding the decision-bar close",
    )
    parser.add_argument("--horizon", type=int, default=1)
    parser.add_argument("--method", default="platt", choices=["platt", "isotonic", "histogram"])
    parser.add_argument("--out", default="", help="write the JSON report here")
    args = parser.parse_args(argv)

    if not args.corpus:
        print(
            "no corpus configured: pass --corpus DIR or set ASTRA_FEATURE_CORPUS "
            "(T25 data has not landed).",
            file=sys.stderr,
        )
        return 2

    report = run_real_calibration(
        args.corpus,
        close_key=args.close_key,
        label_horizon=args.horizon,
        method=args.method,
    )
    print(report.summary())
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(report.to_dict(), handle, indent=2, sort_keys=True)
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
