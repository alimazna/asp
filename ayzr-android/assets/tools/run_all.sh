#!/usr/bin/env bash
# AYZR asset pipeline: generate, then verify everything.
#
# Exits non-zero if any stage fails, so it is safe to use as a CI gate.
#
#   ./run_all.sh
#
set -uo pipefail
cd "$(dirname "$0")"

fail=0

echo "=== 1/5 generate assets ==="
python3 generate_assets.py --quiet || fail=1

echo
echo "=== 2/5 verify assets ==="
python3 verify_assets.py | tail -3 || fail=1

echo
echo "=== 3/5 verifier self-test (mutation testing) ==="
python3 test_verifier.py | tail -3 || fail=1

echo
echo "=== 4/5 SVG / Pillow structural parity ==="
python3 test_svg_parity.py | tail -3 || fail=1

echo
echo "=== 5/5 Android toolchain (aapt2 compile + link) ==="
python3 validate_android_res.py | tail -3 || fail=1

echo
if [ "$fail" -ne 0 ]; then
  echo "RESULT: FAILED"
  exit 1
fi
echo "RESULT: ALL STAGES PASSED"
