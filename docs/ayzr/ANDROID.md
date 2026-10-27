# AYZR — Android App (Arabic, RTL, read-only)

Owner: **Agent B**. Scope: `ayzr-android/**` and this document. The backend
(C++), the existing `astra-frontend/`, and the Windows MT5 Sender are out of
scope and were not modified.

AYZR is a **shadow-only** XAUUSD (gold) market-intelligence client. It never
executes an order. It reads a documented, authenticated subset of the backend
API and presents it in Arabic, right-to-left.

## 1. What it is

- Flutter 3.24.5 / Dart 3.5.4, Android only.
- Four sections, all user-facing text Arabic:
  - **الرئيسية** (Home) — system health, market state, latest-signal summary.
  - **التحليل** (Analysis) — context, signal, suggested levels, metadata.
  - **الأخبار** (News) — article list with a gold-only filter + detail view.
  - **السجل** (History) — persisted analyses, newest first.
- Dark-navy surfaces with restrained gold accents (`lib/core/theme.dart`).
- RTL layout: the whole shell is wrapped in `Directionality(textDirection: rtl)`
  and the app locale is `ar`.
- Arabic typeface: Cairo (OFL), bundled from `assets/fonts/`.

## 2. Backend integration (real, authenticated)

Base URL default: `https://astra-server-production.up.railway.app`
(override with `--dart-define=AYZR_BASE_URL=...` or in-app **الإعدادات**).

Every request carries `Authorization: Bearer <client token>`. The token is the
backend's **read-only client credential** (`ASTRA_CLIENT_TOKEN` →
`AstraApiServerConfig::clientTokens`). It is:

- supplied at runtime (in-app settings, stored in `shared_preferences`), or via
  `--dart-define=AYZR_CLIENT_TOKEN=...`;
- **never** hardcoded in source and **never** a privileged secret — it cannot
  ingest candles, sync, or reach the admin surface.

Routes called (all GET, all client-token gated in
`src/api/AstraApiServer.cpp`):

| Route | Purpose |
|-------|---------|
| `/api/v1/health/v1` | liveness/status (`ok`/`degraded`/`offline`) |
| `/api/v1/analysis/latest` | context + signal + levels + meta |
| `/api/v1/news?limit=&gold_only=` | article list with analysis status |
| `/api/v1/news/analyze?id=` | one article + its Arabic analysis |
| `/api/v1/history?limit=` | persisted analyses |

Ingestion (`/candles/ingest`), `/sync/latest` and `/admin/candles` are
deliberately **not** implemented in the client. A leaked read token cannot write.

Responses are enveloped as `{"api":"v1","schema":"1.0","data":...}`; the client
unwraps `data` and rejects anything that is not an envelope. Backend errors use
the flat `{error,code,message}` object and are mapped to typed failures.

## 3. Honesty rules carried into the client

- **RULE C (probability vs score).** `signal.probability` is shown as a
  probability **only** when `signal.probability_calibrated` is `true`. Otherwise
  the raw `signal.score` is shown, labelled **الدرجة** (score) with an explicit
  "هذه درجة غير معايرة وليست احتمالاً" caveat. A score is never titled
  "الاحتمال". This is enforced in `lib/data/models/analysis.dart` (probability is
  only read from `probability`, never from `score`) and asserted by tests.
- **Never fabricate.** Every absent/`null`/`UNKNOWN` field renders as
  **غير متاح** (unavailable), never `0`, never blank. See `Fmt` in
  `lib/ui/widgets/info_widgets.dart`.
- **Distinct states.** loading / empty / offline / stale / degraded / error /
  unauthorized are separate Arabic states (`lib/ui/widgets/state_panel.dart`).
  Empty is never shown as an error, and a connectivity failure is never shown as
  "no data".
- **Stale is annotated, not hidden.** When `meta.data_freshness_sec > 3600`, or
  the backend reports degraded, a banner appears above the data.
- **Shadow-only notice** is permanently visible on the home screen.
- **News analysis gate.** Only `status == "ok"` presents a proposal/levels; any
  other status shows the reason instead.

## 4. Layout of the code

```
ayzr-android/
  lib/
    main.dart                     app entry (loads settings, runs AyzrApp)
    core/
      app_config.dart             base URL + client token, endpoint builder
      strings_ar.dart             every Arabic string, centralized
      theme.dart                  dark-navy + gold tokens, Cairo font
    data/
      api_result.dart             ApiResult<T> + typed ApiFailure
      api_client.dart             authenticated client for the 5 routes
      models/analysis.dart        analysis surface models (RULE C aware)
      models/news.dart            news + history models
      models/health.dart          health model
    state/
      app_state.dart              ChangeNotifier: sections, phases, flags
      settings_store.dart         persists URL + token on-device
    ui/
      app_shell.dart              RTL shell, bottom nav, MaterialApp
      screens/                    home, analysis, news, news_detail,
                                  history, settings
      widgets/                    state panels, banners, info rows, chips
  test/                           see §6
  assets/                         owned by Agent D (icons, fonts, graphics)
```

## 5. Running

```bash
export PATH=$PATH:/opt/flutter/bin
export ANDROID_HOME=/opt/android-sdk
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64

cd ayzr-android
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
```

The debug APK is written to
`ayzr-android/build/app/outputs/flutter-apk/app-debug.apk`.

To run against a specific backend without editing settings:

```bash
flutter build apk --debug \
  --dart-define=AYZR_BASE_URL=https://astra-server-production.up.railway.app \
  --dart-define=AYZR_CLIENT_TOKEN=<read-only client token>
```

> The client token is a read-only credential. Prefer entering it in the in-app
> settings on the device rather than committing it into a build script.

## 6. Tests

Widget/unit tests use a `MockClient` (from `package:http/testing.dart`) so the
**real** parsing, state-transition and widget code paths run without a network.
They assert, among other things, that an uncalibrated signal renders a score and
never a probability, that a network failure renders the offline panel (not an
empty state), and that empty lists render the empty state (not an error).

See `docs/ayzr/reports/agent-b.md` for the exact pass/fail totals from the last
run.

## 7. Android specifics

- `android/app/build.gradle` pins `compileSdk = 34`,
  `buildToolsVersion = "34.0.0"` (the environment has platform 34 / build-tools
  34.0.0 only; AGP's default 33.0.1 is unavailable offline), and
  `ndkVersion = "27.0.12077973"`. The NDK pin removes a Flutter build warning:
  `shared_preferences_android` declares NDK 27.0.12077973 while Flutter's default
  is 23.1.7779620. NDK releases are backward compatible, so pinning the highest
  required version is the documented fix. NDK 27 was installed with
  `sdkmanager --install "ndk;27.0.12077973"`.
- Gradle wrapper is 8.7 (required for JDK 21; the template default 8.3 supports
  only up to Java 20).
- `AndroidManifest.xml`: `android:supportsRtl="true"`, label **أيزر**, and the
  single `INTERNET` permission. No other permission is requested.
- Launch/normal themes are dark-navy (`values/styles.xml`,
  `values-night/styles.xml`) so there is no white flash before the first frame.
- Launcher icons and splash mark are the generated assets from Agent D
  (`assets/android-res/**`), installed into `android/app/src/main/res/**`.

## 8. Out of scope / not done

- No order execution, no write path, no ingestion, no admin route.
- No backend, `astra-frontend/` or Windows-sender changes.
- Not deployed; no store submission. The APK is a debug build.
