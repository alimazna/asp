"""Parity check: real C++ AnalyticalFeatureEngine output vs the Python adapter.

Agent-D asked (21:33 UTC) that the adapter be validated against REAL C++
producer output, not only synthetic dicts. `tests/models/fixtures/engine_set.json`
is genuine output from `AnalyticalFeatureEngine.cpp` (see its `provenance`),
captured at Agent-A's T01 causality fix (commit 60d04cb).

This test closes the loop:
  1. the real payload must PARSE and VALIDATE through src/models/features.py;
  2. all nine streams and the cross block must share ONE decision instant (F2);
  3. appending 20 future M15 bars must not change the payload at the pinned
     instant (F1 — the exact leak Agent-D demonstrated).

If Agent-A changes the engine contract, regenerate the fixture and this test
will flag any drift.
"""

from __future__ import annotations

import json
import os
import unittest

from src.models.features import parse_feature_set
from src.models.splits import SplitError

FIXTURE = os.path.join(os.path.dirname(__file__), "fixtures", "engine_set.json")


def load_fixture():
    with open(FIXTURE, "r", encoding="utf-8") as handle:
        return json.load(handle)


class EngineParityTest(unittest.TestCase):
    def setUp(self):
        self.fixture = load_fixture()

    def test_real_payload_parses_and_validates(self):
        # Must not raise: field names, ranges and the common-instant rule all
        # match the real C++ producer.
        parsed = parse_feature_set(self.fixture["set_before"])
        self.assertEqual(parsed.asOfBarOpenSec, self.fixture["asOf"])

    def test_all_nine_streams_share_one_decision_instant(self):
        parsed = parse_feature_set(self.fixture["set_before"])
        instants = {v.asOfBarOpenSec for v in parsed.perTimeframe}
        self.assertEqual(instants, {self.fixture["asOf"]})
        self.assertEqual(parsed.cross.asOfBarOpenSec, self.fixture["asOf"])

    def test_cross_features_are_invariant_to_future_m15_bars(self):
        # F1 regression on real output: the pinned payload must be identical
        # before and after 20 future M15 bars are appended.
        before = self.fixture["set_before"]
        after = self.fixture["set_after_future_m15"]
        self.assertEqual(before["cross"]["values"], after["cross"]["values"])
        self.assertEqual(before["asOfBarOpenSec"], after["asOfBarOpenSec"])

    def test_fixture_provenance_is_recorded(self):
        self.assertIn("producer", self.fixture["provenance"])

    def test_adapter_rejects_a_drifted_stream(self):
        # Mutating one stream's asOf must be rejected: proves the guard is live
        # on the real payload shape, not only synthetic ones.
        payload = json.loads(json.dumps(self.fixture["set_before"]))
        payload["perTimeframe"][0]["asOfBarOpenSec"] += 60
        with self.assertRaises(SplitError):
            parse_feature_set(payload)


if __name__ == "__main__":
    unittest.main()
