#!/usr/bin/env python3
"""Install the generated AYZR assets into the Flutter app.

Two different ownership rules apply:

  * `ayzr-android/assets/**`  — owned by Agent D (this agent). Written directly
    by `generate_assets.py`.
  * `ayzr-android/android/app/src/main/res/**` and `ayzr-android/pubspec.yaml`
    — owned by Agent B. This script performs the hand-off, but it never
    overwrites silently: it shows a plan first and requires `--apply`.

What it does:

  1. copies `assets/android-res/**` into the Flutter module's
     `android/app/src/main/res/`, creating density directories as needed
  2. reports which Flutter asset paths still need declaring in `pubspec.yaml`
     (it does not edit that file — that is Agent B's call)

Usage:
    python3 install_android_res.py                 # dry run, prints the plan
    python3 install_android_res.py --apply         # perform the copy
    python3 install_android_res.py --apply --res DIR
"""

import argparse
import filecmp
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
PROJECT = os.path.dirname(ASSETS)                      # ayzr-android/
DEFAULT_RES = os.path.join(PROJECT, "android", "app", "src", "main", "res")

# Flutter asset entries the app needs. Kept as a report, not an edit, because
# pubspec.yaml belongs to Agent B.
PUBSPEC_ENTRIES = [
    "assets/icons/",
    "assets/images/",
    "assets/graphics/png/",
    "assets/fonts/",
]

# Not Android resources: the build scripts and the font source.
SKIP_DIRS = {"tools", "fonts"}


def plan(src_root, dst_root):
    """Return (copies, identical, conflicts) as lists of relative paths."""
    copies, identical, conflicts = [], [], []
    for dirpath, _dirnames, filenames in os.walk(src_root):
        rel_dir = os.path.relpath(dirpath, src_root)
        top = rel_dir.split(os.sep)[0]
        if top in SKIP_DIRS:
            continue
        for fn in sorted(filenames):
            rel = os.path.join(rel_dir, fn) if rel_dir != "." else fn
            src = os.path.join(src_root, rel)
            dst = os.path.join(dst_root, rel)
            if not os.path.exists(dst):
                copies.append(rel)
            elif filecmp.cmp(src, dst, shallow=False):
                identical.append(rel)
            else:
                conflicts.append(rel)
    return copies, identical, conflicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true",
                    help="actually copy (default is a dry run)")
    ap.add_argument("--res", default=DEFAULT_RES,
                    help="target Android res directory")
    ap.add_argument("--overwrite", action="store_true",
                    help="also replace differing existing files (default: skip "
                         "them, since the Flutter template owns some of them)")
    args = ap.parse_args()

    src_root = os.path.join(ASSETS, "android-res")
    if not os.path.isdir(src_root):
        print(f"no generated android-res at {src_root}; run generate_assets.py")
        return 1

    copies, identical, conflicts = plan(src_root, args.res)

    print(f"source : {src_root}")
    print(f"target : {args.res}")
    print()
    print(f"  new files to copy      : {len(copies)}")
    print(f"  already identical      : {len(identical)}")
    print(f"  differing (conflicts)  : {len(conflicts)}")
    for rel in conflicts:
        print(f"    CONFLICT {rel}")

    print("\nFlutter asset entries that must be declared in pubspec.yaml:")
    for entry in PUBSPEC_ENTRIES:
        print(f"    {entry}")

    if not args.apply:
        print("\ndry run — re-run with --apply to copy")
        return 0

    targets = list(copies)
    if args.overwrite:
        targets += conflicts
    written = 0
    for rel in targets:
        src = os.path.join(src_root, rel)
        dst = os.path.join(args.res, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        written += 1
    print(f"\ncopied {written} file(s) into {args.res}")
    if conflicts and not args.overwrite:
        print(f"left {len(conflicts)} differing file(s) untouched; pass "
              f"--overwrite to replace them (they are Agent B's Flutter "
              f"template defaults)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
