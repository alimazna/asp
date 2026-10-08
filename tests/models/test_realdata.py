"""Deterministic tests for the T27 real-data calibration runner.

The corpus here is synthetic (the real corpus gates on E05) but it exercises the
exact real-data code path: load -> label -> causal partition -> calibrated run ->
RULE C verdict. Nothing reaches the network.
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from datetime import datetime, timedelta, timezone

from src.models import realdata
from src.models.calibration import ECE_FAILURE, ECE_TARGET
from src.models.features import (
    CROSS_BOUNDS,
    CROSS_DISCRETE,
    PER_TIMEFRAME_BOUNDS,
    PER_TIMEFRAME_DISCRETE,
)
from src.models.splits import SplitError


def _wiggle(seed):
    return (seed % 7) * 0.01


def _tf_values(seed):
    w = _wiggle(seed)
    values = {
        name: (low + high) / 2 + w
        for name, (low, high) in PER_TIMEFRAME_BOUNDS.items()
    }
    for name in PER_TIMEFRAME_DISCRETE:
        values[name] = 0.0
    return values


def _cross_values(seed):
    w = _wiggle(seed)
    values = {
        name: (low + high) / 2 + w
        for name, (low, high) in CROSS_BOUNDS.items()
    }
    for name in CROSS_DISCRETE:
        values[name] = 0.0
    return values


def _row(asof, close, seed=0.0):
    return {
        "asOfBarOpenSec": asof,
        "close": close,
        "quality": "VALID",
        "valid": True,
        "perTimeframe": [
            {
                "timeframe": "M1",
                "asOfBarOpenSec": asof,
                "values": _tf_values(seed),
                "quality": "VALID",
                "valid": True,
            },
            {
                "timeframe": "M15",
                "asOfBarOpenSec": asof,
                "values": _tf_values(seed + 1),
                "quality": "VALID",
                "valid": True,
            },
        ],
        "cross": {
            "asOfBarOpenSec": asof,
            "values": _cross_values(seed + 2),
            "quality": "VALID",
            "valid": True,
            "m15Available": True,
            "h4Available": True,
        },
    }


def _corpus_rows(per_year=30):
    """Weekly instants across 2021..2025 with oscillating closes (both labels)."""
    rows = []
    seed = 0.0
    for year in range(2021, 2026):
        start = datetime(year, 1, 1, tzinfo=timezone.utc)
        for i in range(per_year):
            ts = int((start + timedelta(days=7 * i)).timestamp())
            close = 1800.0 + (10.0 if i % 2 == 0 else -10.0) + seed * 0.1
            rows.append(_row(ts, close, seed))
            seed += 1.0
    return rows


def write_corpus(rows, tmpdir, fname="features.json"):
    path = os.path.join(tmpdir, fname)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(rows, handle)
    return path


class RuleCVerdictTest(unittest.TestCase):
    def test_boundaries(self):
        self.assertEqual(realdata.rule_c_verdict(0.0)[0], realdata.PUBLISH_PROBABILITY)
        self.assertEqual(
            realdata.rule_c_verdict(ECE_TARGET - 0.001)[0], realdata.PUBLISH_PROBABILITY
        )
        self.assertEqual(realdata.rule_c_verdict(ECE_TARGET)[0], realdata.PUBLISH_SCORE)
        self.assertEqual(realdata.rule_c_verdict(ECE_FAILURE)[0], realdata.PUBLISH_SCORE)
        self.assertEqual(
            realdata.rule_c_verdict(ECE_FAILURE + 0.001)[0], realdata.REPORT_AND_PIVOT
        )


class LoadCorpusTest(unittest.TestCase):
    def test_missing_dir(self):
        with self.assertRaises(SplitError):
            realdata.load_corpus("/no/such/dir")

    def test_empty_dir(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SplitError):
                realdata.load_corpus(d)

    def test_missing_close_is_an_error_not_interpolation(self):
        rows = _corpus_rows(per_year=2)
        del rows[0]["close"]
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            with self.assertRaises(SplitError) as ctx:
                realdata.load_corpus(d)
            self.assertIn("no interpolation", str(ctx.exception))

    def test_non_finite_close_rejected(self):
        rows = _corpus_rows(per_year=2)
        rows[0]["close"] = float("inf")
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            with self.assertRaises(SplitError):
                realdata.load_corpus(d)

    def test_duplicate_instant_rejected(self):
        rows = _corpus_rows(per_year=2)
        rows[1]["asOfBarOpenSec"] = rows[0]["asOfBarOpenSec"]
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            with self.assertRaises(SplitError):
                realdata.load_corpus(d)

    def test_unsorted_files_are_ordered(self):
        rows = _corpus_rows(per_year=3)
        with tempfile.TemporaryDirectory() as d:
            # split across two files, inserted out of order
            write_corpus(rows[3:], d, "b.json")
            write_corpus(rows[:3], d, "a.json")
            _, closes = realdata.load_corpus(d)
            self.assertEqual(closes, [r["close"] for r in rows])

    def test_custom_close_key(self):
        rows = _corpus_rows(per_year=2)
        for r in rows:
            r["px"] = r.pop("close")
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            _, closes = realdata.load_corpus(d, close_key="px")
            self.assertEqual(len(closes), len(rows))


class RunRealCalibrationTest(unittest.TestCase):
    def test_end_to_end_shape_and_verdict(self):
        rows = _corpus_rows(per_year=30)
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            report = realdata.run_real_calibration(d)
        self.assertEqual(report.verdict,
                         realdata.rule_c_verdict(report.ece_oos)[0])
        self.assertEqual(sum(report.partition_sizes.values()),
                         report.n_examples)
        self.assertTrue(3 <= len(report.coverage) == 3)
        # RULE D: every tier reported (incl. empty ones)
        self.assertEqual({t.tier for t in report.coverage}, {"low", "medium", "high"})
        # to_dict is JSON-serialisable
        json.dumps(report.to_dict())
        self.assertIn("VERDICT", report.summary())

    def test_empty_validation_partition_cannot_publish(self):
        rows = _corpus_rows(per_year=30)
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            report = realdata.run_real_calibration(
                d, development_years=(2021, 2022, 2023, 2024, 2025),
                validation_years=(1900,), oos_years=(1901,),
            )
        self.assertEqual(report.verdict, realdata.CANNOT_PUBLISH)
        self.assertEqual(report.calibrated_oos, None)

    def test_empty_oos_partition_cannot_publish(self):
        rows = _corpus_rows(per_year=30)
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            report = realdata.run_real_calibration(
                d, development_years=(2021, 2022),
                validation_years=(2023, 2024, 2025), oos_years=(1901,),
            )
        self.assertEqual(report.verdict, realdata.CANNOT_PUBLISH)

    def test_report_is_deterministic(self):
        rows = _corpus_rows(per_year=20)
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            a = realdata.run_real_calibration(d).to_dict()
            b = realdata.run_real_calibration(d).to_dict()
        self.assertEqual(a, b)


class CliTest(unittest.TestCase):
    def test_no_corpus_configured_exits_2(self):
        rc = realdata.main(["--corpus", ""])
        self.assertEqual(rc, 2)

    def test_runs_and_writes_report(self):
        rows = _corpus_rows(per_year=20)
        with tempfile.TemporaryDirectory() as d:
            write_corpus(rows, d)
            out = os.path.join(d, "report.json")
            rc = realdata.main(["--corpus", d, "--out", out])
            self.assertEqual(rc, 0)
            with open(out, "r", encoding="utf-8") as handle:
                doc = json.load(handle)
            self.assertIn(doc["verdict"], {
                realdata.PUBLISH_PROBABILITY, realdata.PUBLISH_SCORE,
                realdata.REPORT_AND_PIVOT, realdata.CANNOT_PUBLISH,
            })


if __name__ == "__main__":
    unittest.main()
