#!/usr/bin/env python3
"""Validate the installed Android resources with the real Android toolchain.

`verify_assets.py` checks the pixels and the XML text. This script goes further
and runs the actual Android resource compiler (`aapt2` from the Android SDK),
which is the same tool Gradle uses. It proves the resources are not merely
well-formed but genuinely *compilable*: every `@drawable`/`@mipmap`/`@color`
reference resolves, every density qualifier is understood, and the whole set
links into an APK.

This is a read-only validation. It writes only into a temp directory.

Usage:
    python3 validate_android_res.py
    python3 validate_android_res.py --res DIR --aapt2 /path/to/aapt2
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
PROJECT = os.path.dirname(ASSETS)
DEFAULT_RES = os.path.join(PROJECT, "android", "app", "src", "main", "res")

SDK_CANDIDATES = [
    "/opt/android-sdk",
    os.environ.get("ANDROID_HOME", ""),
    os.environ.get("ANDROID_SDK_ROOT", ""),
]

MINIMAL_MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.ayzr.rescheck">
    <uses-sdk android:minSdkVersion="21" android:targetSdkVersion="34"/>
    <application android:label="AYZR res check"/>
</manifest>
"""


def find_aapt2(explicit):
    if explicit:
        return explicit
    for sdk in SDK_CANDIDATES:
        if not sdk or not os.path.isdir(sdk):
            continue
        bt_dir = os.path.join(sdk, "build-tools")
        if not os.path.isdir(bt_dir):
            continue
        for bt in sorted(os.listdir(bt_dir), reverse=True):
            cand = os.path.join(bt_dir, bt, "aapt2")
            if os.path.exists(cand):
                return cand
    return None


def find_android_jar(sdk):
    plat = os.path.join(sdk, "platforms")
    if not os.path.isdir(plat):
        return None
    for p in sorted(os.listdir(plat), reverse=True):
        jar = os.path.join(plat, p, "android.jar")
        if os.path.exists(jar):
            return jar
    return None


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default=DEFAULT_RES)
    ap.add_argument("--aapt2", default=None)
    args = ap.parse_args()

    aapt2 = find_aapt2(args.aapt2)
    if not aapt2:
        print("aapt2 not found — skipping toolchain validation "
              "(install Android SDK build-tools or pass --aapt2)")
        return 0
    if not os.path.isdir(args.res):
        print(f"resource dir not found: {args.res}")
        return 1

    sdk = os.path.dirname(os.path.dirname(os.path.dirname(aapt2)))
    android_jar = find_android_jar(sdk)
    print(f"aapt2       : {aapt2}")
    print(f"android.jar : {android_jar}")
    print(f"res dir     : {args.res}\n")

    work = tempfile.mkdtemp(prefix="ayzr-aapt2-")
    failures = []
    try:
        flat = os.path.join(work, "flat")
        os.makedirs(flat)

        # 1. Compile every resource. A malformed PNG/XML or an unknown density
        #    qualifier fails here.
        proc = run([aapt2, "compile", "--dir", args.res, "-o", flat])
        compiled = sorted(glob.glob(os.path.join(flat, "*.flat")))
        print(f"compile: exit={proc.returncode}  "
              f"{len(compiled)} resource(s) compiled")
        if proc.returncode != 0 or not compiled:
            print(proc.stdout)
            print(proc.stderr)
            failures.append("aapt2 compile failed")
        if proc.stderr.strip():
            print("compile stderr:", proc.stderr.strip()[:2000])
            failures.append("aapt2 compile emitted errors")

        # 2. Link them with a synthetic manifest. Unresolved references are
        #    hard errors at this stage.
        apk = os.path.join(work, "res.apk")
        manifest = os.path.join(work, "AndroidManifest.xml")
        with open(manifest, "w", encoding="utf-8") as fh:
            fh.write(MINIMAL_MANIFEST)

        cmd = [aapt2, "link", "-o", apk, "--manifest", manifest]
        if android_jar:
            cmd += ["-I", android_jar]
        cmd += compiled
        proc = run(cmd)
        size = os.path.getsize(apk) if os.path.exists(apk) else 0
        print(f"link   : exit={proc.returncode}  apk={size} bytes")
        if proc.returncode != 0 or not os.path.exists(apk):
            print(proc.stdout)
            print(proc.stderr)
            failures.append("aapt2 link failed")

        # 3. Assert the resources actually landed, with the expected density
        #    configurations.
        if os.path.exists(apk):
            dump = run([aapt2, "dump", "resources", apk]).stdout
            expected = {
                "mipmap/ic_launcher": ["mdpi", "hdpi", "xhdpi", "xxhdpi",
                                       "xxxhdpi", "anydpi-v26"],
                "mipmap/ic_launcher_foreground": ["mdpi", "hdpi", "xhdpi",
                                                  "xxhdpi", "xxxhdpi"],
                "mipmap/ic_launcher_monochrome": ["mdpi", "hdpi", "xhdpi",
                                                  "xxhdpi", "xxxhdpi"],
                "mipmap/ic_launcher_background": ["mdpi", "hdpi", "xhdpi",
                                                  "xxhdpi", "xxxhdpi"],
                "drawable/ic_stat_ayzr": ["mdpi", "hdpi", "xhdpi", "xxhdpi",
                                          "xxxhdpi"],
                "drawable/ayzr_splash_mark": ["xxhdpi", "xxxhdpi"],
                "color/ayzr_gold": [],
                "color/ic_launcher_background": [],
            }
            for name, densities in expected.items():
                if f"{name}\n" not in dump and f" {name}\n" not in dump:
                    failures.append(f"{name} missing from linked APK")
                    print(f"  MISSING {name}")
                    continue
                missing = [d for d in densities if f"({d})" not in dump]
                for d in missing:
                    failures.append(f"{name} missing density {d}")
                if missing:
                    print(f"  PARTIAL {name} missing {missing}")
                else:
                    print(f"  OK      {name} "
                          f"({len(densities)} density config(s))")

        if failures:
            print("\nfailures:")
            for f in failures:
                print(f"  - {f}")
            return 1
        print("\nAndroid toolchain validation PASSED "
              "(aapt2 compile + link + resource dump)")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
