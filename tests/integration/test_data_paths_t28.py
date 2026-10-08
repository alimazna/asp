#!/usr/bin/env python3
"""T28 - tests for the configurable data-path surface (scripts/data_paths.py).

Proves: (a) the default is repo-relative and exists-independent of CWD; (b) the
environment override wins over the default and is tagged; (c) an explicit argument
wins over the environment; (d) a relative override resolves against the caller's
CWD, not the repo; (e) corpus-layout helpers join to the overridden root; (f) no
absolute path is hardcoded outside the computed repo root.

Runs offline in a temp dir; touches nothing in the repo. Exit 0 = all checks pass.
"""

from __future__ import annotations

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
import data_paths  # noqa: E402

_results = []


def check(name, ok, detail=""):
    _results.append(ok)
    print(("[PASS] " if ok else "[FAIL] ") + name + (f" - {detail}" if detail and not ok else ""))


def main() -> int:
    # (a) default is repo-relative, not CWD-relative
    d = data_paths.resolve(environ={})
    check("default data_root is repo-relative",
          d.data_root == os.path.join(REPO, "research", "data", "xauusd_m1"), d.data_root)
    check("default features_dir is repo-relative",
          d.features_dir == os.path.join(REPO, "research", "features_real"), d.features_dir)
    check("default provenance tagged", d.provenance["data_root"] == "default")

    # default must not depend on CWD
    cwd = os.getcwd()
    try:
        os.chdir(tempfile.gettempdir())
        d2 = data_paths.resolve(environ={})
        check("default independent of CWD", d2.data_root == d.data_root, d2.data_root)
    finally:
        os.chdir(cwd)

    # (b) env override wins over default and is tagged
    env = {data_paths.ENV_DATA_ROOT: "/tmp/corpusX",
           data_paths.ENV_FEATURES_DIR: "/tmp/featX"}
    de = data_paths.resolve(environ=env)
    check("env overrides data_root", de.data_root == os.path.abspath("/tmp/corpusX"), de.data_root)
    check("env tag recorded",
          de.provenance["data_root"] == f"env:{data_paths.ENV_DATA_ROOT}")
    check("env overrides features_dir",
          de.features_dir == os.path.abspath("/tmp/featX"), de.features_dir)

    # (c) explicit argument beats env
    da = data_paths.resolve(data_root="/tmp/argRoot", environ=env)
    check("arg beats env", da.data_root == os.path.abspath("/tmp/argRoot"), da.data_root)
    check("arg provenance tagged", da.provenance["data_root"] == "arg")

    # (d) relative override resolves against CWD
    rel = data_paths.resolve(data_root="corpus", environ={})
    check("relative override resolves against CWD",
          rel.data_root == os.path.abspath("corpus"), rel.data_root)

    # (e) helpers join to the overridden root
    check("m1_csv joins root", de.m1_csv(2023) == os.path.join(de.data_root, "2023.csv"), de.m1_csv(2023))
    check("ask_csv joins ask/",
          de.ask_csv(2023) == os.path.join(de.data_root, "ask", "2023.csv"))
    check("sample_csv joins sample/",
          de.sample_csv(2023) == os.path.join(de.data_root, "sample", "2023.head.csv"))
    check("quality_report at root",
          de.quality_report() == os.path.join(de.data_root, "QUALITY.md"))
    check("feature_file joins features_dir",
          de.feature_file("m15.json") == os.path.join(de.features_dir, "m15.json"))

    # (f) no hardcoded corpus path outside the computed repo root: default anchors
    #     on REPO regardless of install layout
    check("defaults anchored on computed REPO",
          data_paths.DEFAULT_DATA_ROOT.startswith(REPO) and
          data_paths.DEFAULT_FEATURES_DIR.startswith(REPO))

    # existing_m1_years tolerates a corpus that is not present (Lead's fetch pending)
    check("existing_m1_years is [] for an absent corpus", de.existing_m1_years() == [])

    passed = sum(1 for ok in _results if ok)
    total = len(_results)
    print(f"\n{passed}/{total} checks passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
