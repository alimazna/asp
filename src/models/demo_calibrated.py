"""Deterministic end-to-end demo of the T05 calibrated pipeline.

SYNTHETIC data only — a pipeline check, not a market result. There is no real
XAUUSD data in this container. RULE C: nothing here is a published probability.

Flow demonstrated: base model fit on development; calibrator fit on validation;
calibrated probabilities evaluated on OOS. All three calibrators are compared.

Run:
    python3 -m src.models.demo_calibrated
"""

from __future__ import annotations

from src.models.calibrated import run_calibrated
from src.models.dataset import build_labeled_examples, purge_split
from src.models.demo_baseline import (
    SYNTHETIC_BANNER,
    _dedupe_sorted,
    _synthetic_series,
)

METHODS = ("platt", "isotonic", "histogram")


def main() -> int:
    feature_sets, closes = _synthetic_series(2000)
    examples = _dedupe_sorted(build_labeled_examples(feature_sets, closes, horizon=1))
    split = purge_split(examples)
    if not (split.development and split.validation and split.oos):
        print("demo could not populate all three partitions")
        return 1

    print(SYNTHETIC_BANNER)
    print(
        f"rows: dev={len(split.development)} val={len(split.validation)} "
        f"oos={len(split.oos)}"
    )
    print()
    for method in METHODS:
        report = run_calibrated(
            split.development, split.validation, split.oos, method=method
        )
        raw = report.raw["oos"].calibration
        cal = report.calibrated["oos"].calibration
        print(f"[{method}] OOS")
        print(
            f"  raw        brier={raw.brier:.4f} ece={raw.ece:.4f} "
            f"mce={raw.mce:.4f}"
        )
        print(
            f"  calibrated brier={cal.brier:.4f} ece={cal.ece:.4f} "
            f"mce={cal.mce:.4f} meets_target={cal.meets_target()}"
        )
    print()
    print(
        "NOTE: synthetic data only — a pipeline check, not evidence about XAUUSD. "
        "Uncalibrated/unpublished until the T11 audit (RULE C)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
