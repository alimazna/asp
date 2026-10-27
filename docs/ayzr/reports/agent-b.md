# Agent B — Android App — Final Report

Scope: the new Android project `ayzr-android/**` and `docs/ayzr/ANDROID.md`.
Backend C++, the existing `astra-frontend/`, and the Windows MT5 Sender were
**not** modified. No deployment, no push, no production data touched.

## Deliverable

Arabic-only, RTL Flutter app for XAUUSD market intelligence (shadow only), with
four sections — الرئيسية / التحليل / الأخبار / السجل — dark-navy + gold theme,
real authenticated backend integration, and full loading/empty/offline/stale/
error/degraded/unauthorized states.

## Files created

Project root: `ayzr-android/`

Dart source (`lib/`):
- `lib/main.dart`
- `lib/core/app_config.dart`
- `lib/core/strings_ar.dart`
- `lib/core/theme.dart`
- `lib/data/api_result.dart`
- `lib/data/api_client.dart`
- `lib/data/models/analysis.dart`
- `lib/data/models/news.dart`
- `lib/data/models/health.dart`
- `lib/state/app_state.dart`
- `lib/state/settings_store.dart`
- `lib/ui/app_shell.dart`
- `lib/ui/screens/home_screen.dart`
- `lib/ui/screens/analysis_screen.dart`
- `lib/ui/screens/news_screen.dart`
- `lib/ui/screens/news_detail_screen.dart`
- `lib/ui/screens/history_screen.dart`
- `lib/ui/screens/settings_screen.dart`
- `lib/ui/widgets/state_panel.dart`
- `lib/ui/widgets/info_widgets.dart`

Tests (`test/`):
- `test/test_helpers.dart`
- `test/models_test.dart`
- `test/api_client_test.dart`
- `test/app_state_test.dart`
- `test/widget_test.dart`

Config / Android:
- `pubspec.yaml` (http, shared_preferences, flutter_localizations, Cairo font)
- `analysis_options.yaml` (Flutter template)
- `android/app/build.gradle` (compileSdk 34, buildToolsVersion 34.0.0)
- `android/gradle/wrapper/gradle-wrapper.properties` (Gradle 8.3 → 8.7 for JDK 21)
- `android/app/src/main/AndroidManifest.xml` (RTL, label أيزر, INTERNET only)
- `android/app/src/main/res/values/styles.xml`, `values-night/styles.xml`
- `android/app/src/main/res/values/colors.xml`
- `android/app/src/main/res/drawable/launch_background.xml`
- Remaining Flutter-generated scaffold (`android/`, `.gitignore`, etc.)

Docs:
- `docs/ayzr/ANDROID.md`
- `docs/ayzr/reports/agent-b.md` (this file)

Installed Agent D's generated assets (their scope) into my Android resources via
their hand-off script `assets/tools/install_android_res.py --apply --overwrite`
(launcher icons, splash mark, `ic_launcher_background`) and declared the bundled
Cairo Arabic font in `pubspec.yaml` / `theme.dart`.

## Commands run

```bash
export PATH=$PATH:/opt/flutter/bin
export ANDROID_HOME=/opt/android-sdk
export ANDROID_SDK_ROOT=/opt/android-sdk
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64

cd /workspace/project/asp/ayzr-android
flutter create --platforms=android --org com.ayzr --project-name ayzr_app ayzr-android
flutter pub get
flutter analyze
flutter test
flutter test test/models_test.dart
flutter test test/api_client_test.dart
flutter test test/app_state_test.dart
flutter test test/widget_test.dart
python3 assets/tools/install_android_res.py --apply --overwrite
flutter build apk --debug
```

## Exact test results

`flutter analyze` → **No issues found!**

`flutter test` → **+45: All tests passed!** (0 failed)

Per file:

| File | Tests | Result |
|------|-------|--------|
| `test/models_test.dart` | 9 | all passed |
| `test/api_client_test.dart` | 13 | all passed |
| `test/app_state_test.dart` | 7 | all passed |
| `test/widget_test.dart` | 16 | all passed |
| **Total** | **45** | **45 passed / 0 failed** |

Tests use a real `MockClient` at the HTTP boundary so the actual parsing, state
machine and widget code run. Key assertions: an uncalibrated signal renders a
score and never a probability (RULE C); a socket failure renders the offline
panel, not an empty state; an empty list renders the empty state, not an error;
401 maps to the configuration prompt; RTL directionality is asserted on the
shell.

## APK

- Path: `/workspace/project/asp/ayzr-android/build/app/outputs/flutter-apk/app-debug.apk`
- Size: 105,448,690 bytes (~100.6 MiB), debug build
- SHA-1 (from `app-debug.apk.sha1`): `5c33e3df3e6cc2772501de1e82433625780f10c7`
- `aapt dump badging`: package `com.ayzr.ayzr_app`, versionName `1.0.0`,
  targetSdk 34, `INTERNET` permission only, label `أيزر`.
- Bundled: `assets/flutter_assets/assets/fonts/Cairo-Variable.ttf`, adaptive
  launcher icons (`mipmap-anydpi-v26/ic_launcher.xml`), splash mark.

## Verified facts / limits

- Real backend integration uses only the client-token-gated read routes
  (`/health/v1`, `/analysis/latest`, `/news`, `/news/analyze`, `/history`).
  No ingest/sync/admin route exists in the client; the token is supplied at
  runtime and never hardcoded (scan of `lib/` found no secrets).
- No backend, `astra-frontend/`, or windows-sender file was changed by this
  agent (`git status --short astra-frontend/` empty).
- The APK is a **debug** build and was not deployed or published.
- No prices, candles, news, indicators, levels or probabilities are invented:
  every unsourced field renders as غير متاح, and tests assert this.

## Build-environment notes (for reproducibility)

- AGP's default build-tools 33.0.1 is absent offline; pinned to 34.0.0.
- Gradle wrapper raised to 8.7 (JDK 21 is incompatible with the 8.3 template).
- `shared_preferences_android` warns it wants NDK 27.0.12077973; the build
  succeeded with the Flutter-default NDK resolution. A release build should pin
  the NDK explicitly.
