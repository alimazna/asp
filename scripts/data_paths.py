#!/usr/bin/env python3
"""T28 - configurable data/feature path surface (no hardcoded absolute paths).

One canonical place for the pipeline's data locations, so the real-data run
(T26 feature harness, T27 calibration) runs on the Lead's corpus without editing
code. Everything defaults to a path RELATIVE TO THIS REPO (computed from
``__file__``, never the current working directory) and is overridable by
environment variable:

    AURA_DATA_ROOT      root of the real M1 corpus
                        (default <repo>/research/data/xauusd_m1)
    AURA_FEATURES_DIR   where FeatureSet JSON is written/read
                        (default <repo>/research/features_real).
                        Compatibility alias: ASTRA_FEATURE_CORPUS (T27) is also
                        honoured; AURA_FEATURES_DIR wins if both are set.

Corpus layout under ``AURA_DATA_ROOT`` (see research/data/xauusd_m1/README.md):

    <root>/xauusd_m1_real.csv  the delivered canonical corpus (single CSV,
                               ``timestamp,open,high,low,close,volume``)
    <root>/<year>.csv          the planned per-year Dukascopy layout, if fetched
    <root>/ask/<year>.csv      raw ASK M1 (spread/cost, RULE B)
    <root>/sample_first_1000.csv   committed provenance sample
    <root>/checksums.sha256    committed integrity manifest
    <root>/README.md           committed source/format/timezone note
    <root>/QUALITY.md          committed mandatory quality report

FeatureSets under ``AURA_FEATURES_DIR`` (T26 output, T27 input):

    <dir>/corpus/real_corpus.json.gz   committed real corpus (gzip of the JSON)

CLI:
    python3 scripts/data_paths.py            # print the resolved paths (JSON)
    python3 scripts/data_paths.py --root X   # preview an override without exporting

This module never reads market data itself and never publishes anything; it only
resolves and describes locations. The frozen API v1 is untouched.
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from typing import Dict, List, Optional

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

DEFAULT_DATA_ROOT = os.path.join(REPO_ROOT, "research", "data", "xauusd_m1")
DEFAULT_FEATURES_DIR = os.path.join(REPO_ROOT, "research", "features_real")

ENV_DATA_ROOT = "AURA_DATA_ROOT"
ENV_FEATURES_DIR = "AURA_FEATURES_DIR"
# Compatibility aliases: the T27 real-data runner (src/models/realdata.py) reads
# ASTRA_FEATURE_CORPUS. AURA_FEATURES_DIR is canonical (T28); the alias is honoured
# so both spellings resolve to the same directory during the Phase-5.1 transition.
FEATURES_DIR_ALIASES = (ENV_FEATURES_DIR, "ASTRA_FEATURE_CORPUS")

# Real corpus coverage (Phase 5.1). Kept here so the pipeline and its quality
# checks agree on the same window without a second source of truth.
REAL_YEARS = (2021, 2022, 2023, 2024, 2025)


def _abspath(path: str) -> str:
    """Normalise a configured path: expand ~ and make it absolute from CWD."""
    return os.path.abspath(os.path.expanduser(path))


@dataclass(frozen=True)
class DataPaths:
    """Resolved, overridable locations for the real-data pipeline."""

    data_root: str
    features_dir: str
    provenance: Dict[str, str]  # field -> "default" | "env:AURA_DATA_ROOT" etc.

    def m1_csv(self, year: int) -> str:
        return os.path.join(self.data_root, f"{year}.csv")

    def canonical_csv(self) -> str:
        """The delivered single-file canonical corpus (if present)."""
        return os.path.join(self.data_root, "xauusd_m1_real.csv")

    def ask_csv(self, year: int) -> str:
        return os.path.join(self.data_root, "ask", f"{year}.csv")

    def sample_csv(self) -> str:
        return os.path.join(self.data_root, "sample_first_1000.csv")

    def checksums(self) -> str:
        return os.path.join(self.data_root, "checksums.sha256")

    def metadata(self) -> str:
        return os.path.join(self.data_root, "README.md")

    def quality_report(self) -> str:
        return os.path.join(self.data_root, "QUALITY.md")

    def feature_corpus_dir(self) -> str:
        """Directory holding the T26 FeatureSet corpus (T27 input)."""
        return os.path.join(self.features_dir, "corpus")

    def real_corpus(self) -> str:
        """The committed gzip corpus (T26 output)."""
        return os.path.join(self.feature_corpus_dir(), "real_corpus.json.gz")

    def feature_file(self, name: str) -> str:
        """FeatureSet JSON path for a caller-chosen, immutable snapshot name."""
        return os.path.join(self.features_dir, name)

    def existing_m1_years(self, years=REAL_YEARS) -> List[int]:
        return [y for y in years if os.path.isfile(self.m1_csv(y))]

    def canonical_available(self) -> bool:
        """True when the delivered single-file corpus is present on disk."""
        return os.path.isfile(self.canonical_csv())

    def as_dict(self) -> Dict[str, object]:
        return {
            "repo_root": REPO_ROOT,
            "data_root": self.data_root,
            "features_dir": self.features_dir,
            "provenance": self.provenance,
            "existing_m1_years": self.existing_m1_years(),
        }


def resolve(
    data_root: Optional[str] = None,
    features_dir: Optional[str] = None,
    environ: Optional[Dict[str, str]] = None,
) -> DataPaths:
    """Resolve data locations.

    Precedence for each field: explicit argument > environment variable >
    repo-relative default. A relative override is resolved against the CWD (the
    caller's intent), never silently against the repo.
    """
    env = os.environ if environ is None else environ
    provenance: Dict[str, str] = {}

    if data_root is not None:
        root = _abspath(data_root)
        provenance["data_root"] = "arg"
    elif env.get(ENV_DATA_ROOT):
        root = _abspath(env[ENV_DATA_ROOT])
        provenance["data_root"] = f"env:{ENV_DATA_ROOT}"
    else:
        root = DEFAULT_DATA_ROOT
        provenance["data_root"] = "default"

    if features_dir is not None:
        feats = _abspath(features_dir)
        provenance["features_dir"] = "arg"
    else:
        set_var = next((v for v in FEATURES_DIR_ALIASES if env.get(v)), None)
        if set_var:
            feats = _abspath(env[set_var])
            provenance["features_dir"] = f"env:{set_var}"
        else:
            feats = DEFAULT_FEATURES_DIR
            provenance["features_dir"] = "default"

    return DataPaths(data_root=root, features_dir=feats, provenance=provenance)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Resolve AURA/ASTRA data paths (T28).")
    parser.add_argument("--root", default=None, help=f"override ({ENV_DATA_ROOT})")
    parser.add_argument("--features", default=None, help=f"override ({ENV_FEATURES_DIR})")
    args = parser.parse_args(argv)

    paths = resolve(data_root=args.root, features_dir=args.features)
    print(json.dumps(paths.as_dict(), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
