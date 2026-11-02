#!/usr/bin/env bash
# Reproducible AYZR Android APK build.
#
# Produces both APKs from a clean tree and prints their SHA-256 so a build can
# be compared across machines. It asserts the toolchain version it was verified
# against, so a mismatch is reported instead of silently producing a different
# artifact.
#
# Usage:  tool/build_apk.sh [debug|release|both]   (default: both)
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${1:-both}"
EXPECTED_FLUTTER="3.24.5"
EXPECTED_DART="3.5.4"

if ! command -v flutter >/dev/null 2>&1; then
  echo "flutter not found on PATH" >&2
  exit 1
fi

have_flutter="$(flutter --version 2>/dev/null | head -1 | awk '{print $2}')"
if [[ "$have_flutter" != "$EXPECTED_FLUTTER" ]]; then
  echo "warning: flutter $have_flutter != verified $EXPECTED_FLUTTER" >&2
fi
have_dart="$(dart --version 2>&1 | awk '{print $4}')"
if [[ "$have_dart" != "$EXPECTED_DART" ]]; then
  echo "warning: dart $have_dart != verified $EXPECTED_DART" >&2
fi

# The journal tests use sqflite_common_ffi, which dlopen()s the bare name
# "libsqlite3.so"; the system ships only the versioned soname.
SQLITE_LIB_DIR="${SQLITE_LIB_DIR:-/tmp/sqlite-lib}"
if [[ ! -e "$SQLITE_LIB_DIR/libsqlite3.so" ]]; then
  mkdir -p "$SQLITE_LIB_DIR"
  ln -sf "$(ldconfig -p | awk '/libsqlite3\.so\.0 /{print $NF; exit}')" \
    "$SQLITE_LIB_DIR/libsqlite3.so"
fi

echo "== flutter pub get =="
flutter pub get

echo "== flutter analyze =="
flutter analyze

echo "== flutter test =="
LD_LIBRARY_PATH="$SQLITE_LIB_DIR" flutter test

build_one() {
  local kind="$1"
  if [[ "$kind" == "release" && ! -f android/key.properties ]]; then
    echo
    echo "release build skipped: no android/key.properties."
    echo "A release APK must be signed with the owner's real keystore; this"
    echo "script will not generate one or fall back to the debug key."
    echo "Create android/key.properties (see android/key.properties.example"
    echo "and docs/ayzr/ANDROID.md), then re-run: tool/build_apk.sh release"
    return 0
  fi
  echo "== flutter build apk --$kind =="
  flutter build apk "--$kind"
  local apk="build/app/outputs/flutter-apk/app-$kind.apk"
  local size hash payload
  size="$(du -h "$apk" | cut -f1)"
  hash="$(sha256sum "$apk" | cut -d' ' -f1)"
  payload="$(python3 tool/apk_payload_hash.py "$apk")"
  printf '%s\n  path: %s\n  size: %s\n  sha256: %s\n  payload sha256: %s\n' \
    "$kind" "$apk" "$size" "$hash" "$payload"
}

case "$MODE" in
  debug)   build_one debug ;;
  release) build_one release ;;
  both)    build_one debug; build_one release ;;
  *) echo "usage: $0 [debug|release|both]" >&2; exit 2 ;;
esac

echo
echo "Release APKs here are signed with the debug keystore (android/app/build.gradle)."
echo "For a private distribution, add android/key.properties + a real keystore; the"
echo "build then signs with it instead, with no other change. See docs/ayzr/ANDROID.md."
