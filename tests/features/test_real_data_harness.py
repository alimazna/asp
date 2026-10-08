"""T26 harness test: real M1 bars -> C++ engine -> frozen FeatureSet JSON.

Deterministic and hermetic: it drives the REAL ``build/aura_feature_dump`` binary
(no Python feature recompute) over a synthetic-but-realistic M1 series, then
asserts the emitted sets parse and validate through ``parse_feature_set``.

The synthetic series is a fixture for logic, not evidence about gold — the
real-data sample is produced separately by ``run_features.py --m1 <corpus>``.

Checks:
  1. chain runs and emits one validated set per decision instant;
  2. all nine streams + cross share the single decision instant (F2);
  3. causality: the first K decisions are byte-identical whether or not later
     bars exist (the no-lookahead invariant, F1);
  4. determinism: two runs produce byte-identical output.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO_ROOT)

from src.models.features import parse_feature_set  # noqa: E402

DUMP = os.path.join(REPO_ROOT, "build", "aura_feature_dump")

# research/ is a data/experiment tree (no __init__.py); load the driver by path.
_spec = importlib.util.spec_from_file_location(
    "run_features", os.path.join(REPO_ROOT, "research", "features_real",
                                 "run_features.py"))
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)


def _run(tmp, m1, decisions, tag):
    """Drive the dump binary; return the parsed output list and decision list."""
    bars_dir = os.path.join(tmp, f"bars_{tag}")
    os.makedirs(bars_dir, exist_ok=True)
    m15 = []
    for tf in R.TIMEFRAMES:
        bars = R.aggregate(m1, tf)
        R.write_bars(os.path.join(bars_dir, f"{tf}.csv"), bars)
        if tf == "M15":
            m15 = [b + 15 * 60 for b, _ in bars]  # decision = M15 close
    chosen = m15 if decisions == 0 else m15[:decisions]
    dec = os.path.join(bars_dir, "decisions.csv")
    with open(dec, "w") as handle:
        handle.write("\n".join(str(d) for d in chosen) + "\n")
    out = os.path.join(tmp, f"out_{tag}.json")
    proc = subprocess.run(
        [DUMP, bars_dir, dec, out, ",".join(R.TIMEFRAMES)],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"dump failed: {proc.stderr}")
    with open(out) as handle:
        return json.load(handle), chosen, out


@unittest.skipUnless(os.path.isfile(DUMP), "aura_feature_dump not built")
class RealDataHarnessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # ~1 month of minutes: enough for H4/D1 context, small enough to be fast.
        cls.m1 = R.synth_m1(45000)

    def test_chain_emits_validated_sets(self):
        with tempfile.TemporaryDirectory() as tmp:
            sets, decisions, _ = _run(tmp, self.m1, 200, "a")
            self.assertEqual(len(sets), len(decisions))
            for i, raw in enumerate(sets):
                parsed = parse_feature_set(raw)          # must not raise
                self.assertEqual(parsed.asOfBarOpenSec, decisions[i])
                instants = {v.asOfBarOpenSec for v in parsed.perTimeframe}
                instants.add(parsed.cross.asOfBarOpenSec)
                self.assertEqual(instants, {decisions[i]})  # F2
                self.assertIsInstance(raw["close"], (int, float))

    def test_output_is_consumable_by_t27_loader(self):
        # The T26 output contract is exactly what Agent-B's T27 corpus loader
        # expects: FeatureSet objects + a top-level decision-bar `close`.
        from src.models.realdata import load_corpus
        with tempfile.TemporaryDirectory() as tmp:
            _, _, out = _run(tmp, self.m1, 200, "c")
            corpus = os.path.join(tmp, "corpus")
            os.makedirs(corpus)
            with open(out) as src, open(os.path.join(corpus, "sets.json"), "w") as dst:
                dst.write(src.read())
            feature_sets, closes = load_corpus(corpus)
            self.assertEqual(len(feature_sets), 200)
            self.assertEqual(len(closes), 200)
            self.assertTrue(all(isinstance(c, float) for c in closes))

    def test_causality_later_bars_do_not_alter_early_sets(self):
        with tempfile.TemporaryDirectory() as tmp:
            head, _, _ = _run(tmp, self.m1[:20000], 100, "head")
            full, _, _ = _run(tmp, self.m1, 100, "full")
            # The same first 100 decisions must be byte-identical: appending
            # future bars cannot change a causal feature snapshot (F1).
            self.assertEqual(head, full)

    def test_determinism(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, _, path_a = _run(tmp, self.m1, 100, "d1")
            _, _, path_b = _run(tmp, self.m1, 100, "d2")
            with open(path_a, "rb") as fa, open(path_b, "rb") as fb:
                self.assertEqual(fa.read(), fb.read())


if __name__ == "__main__":
    unittest.main()
