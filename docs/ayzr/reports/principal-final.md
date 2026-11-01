# AYZR — Principal Agent Final Verification Report

Date: 2026-10-27 (UTC). Repository `/workspace/project/asp`, branch `main`,
HEAD `9ebfe62` + the uncommitted AYZR working tree. No commit, push or deploy
was performed.

This report covers the closing verification pass. It separates **verified
facts** (commands run, output observed) from **remaining work**.

## 1. What this pass changed

| Change | File | Why |
|--------|------|-----|
| Concrete example added to the schema-repair turn | `src/news/NewsPrompt.cpp` | Real-model evidence: an inline restatement did not recover a valid object; a worked example of the exact nesting is the highest-signal correction for a model with no JSON mode. |
| `lastMessages()` test seam | `src/news/MockLlmProvider.h` | Lets a test assert the repair replays the conversation (system + user + assistant + user). |
| Conversation-replay test | `tests/NewsIntelligenceTests.cpp` | `news_service_repair_turn_uses_conversational_sequence` pins the turn sequence. |
| libcurl in the container build | `Dockerfile` | Without it the production image compiles with no outbound HTTP transport and every live analysis degrades to `unavailable`. |
| NDK pin | `ayzr-android/android/app/build.gradle` | Removes the Flutter NDK warning; `shared_preferences_android` needs NDK 27.0.12077973. |
| Status helper field fix | `tools/ayzr_agents/create_agents.py` | `--status` read the wrong field and printed `None`; now reads `execution_status`. |
| Docs + memory | `docs/ayzr/BACKEND.md`, `docs/ayzr/TESTING.md`, `docs/ayzr/ANDROID.md`, `AGENTS.md` | Record the repair turn, live provider findings, exact totals, NDK pin. |

## 2. Verified test totals (commands actually run)

| Suite | Command | Result |
|-------|---------|--------|
| C++ CTest | `cd build-linux && ctest` | **30/30 passed** |
| C++ harness binaries | `for t in *Tests; do ./"$t"; done` | **30/30 binaries, 254 cases, 0 failed** |
| `NewsIntelligenceTests` | `./NewsIntelligenceTests` | **32 cases, 0 failed** (was 29; +3 repair) |
| Python models | `python3 -m unittest discover -s tests/models` | **308 passed, 0 failed** |
| Python full tree | `python3 -m pytest tests/` | **318 passed, 4 skipped, 1 error** (pre-existing pytest mis-collection of the `test_segments_overlap` helper) |
| Integration scripts | `for f in tests/integration/test_*.py; do python3 "$f"; done` | **6 PASS, 2 SKIP, 1 pre-existing FAIL** (`test_bridge_t06.py` 24/25 — documented, files unmodified at HEAD) |
| Flutter | `cd ayzr-android && flutter test` | **45 passed, 0 failed** |
| Visual assets | `ayzr-android/assets/tools/run_all.sh` | **361 checks passed, 11/11 mutations caught, aapt2 link OK** |

## 3. End-to-end simulation (mock MT5 sender + mock LLM)

Server: `astra_server` on `127.0.0.1`, `ASTRA_LLM_PROVIDER=mock`, fresh data dir.

```
[1] first send: HTTP 200, ingested=2
[2] replay:     HTTP 200, ingested=0, duplicates=2     <- idempotent
[3] invalid OHLC: HTTP 200, rejected=1                 <- validation
[4] sync:       HTTP 200, points=1
RESULT: PASS
```

Lifecycle checks against the same server:

- news ingest → `stored=1`; `GET /api/v1/news/analyze?id=…` → `status=ok`,
  Arabic fields populated (facts, implications, evidence, direction).
- `GET /api/v1/history?limit=5` → 1 persisted entry with headline + analysis.
- `GET /api/v1/candles?tf=M15&limit=10` → `count=2`, `newest_closed_time` set.
- **Restart durability:** after killing and restarting the server on the same
  data dir, history (1), news (1) and candles (2) all survived.

## 4. Live LLM7 findings (real provider, real token)

Verified 2026-10-27 with a real `LLM7_API_TOKEN` against `https://api.llm7.io/v1`:

- `GET /v1/models` returns 67 models and includes `nemotron-3-nano:30b`
  (and `default`). The token authenticates correctly.
- `POST /v1/chat/completions` for `nemotron-3-nano:30b` returns
  **HTTP 429 `insufficient_quota`**: `"Daily token quota exceeded. Retry after
  60 seconds."`, and on the final probe `"Retry after 69686 seconds."` (~19 h).
  The free tier's daily token budget was exhausted by the session's test calls.
- Earlier in the session (before the quota was spent) the same model returned a
  **200 with valid JSON that violated the schema**: top-level keys folded into a
  `facts` element, giving `invalid_model_output: invalid direction: ''`. This is
  the reason the service now makes a bounded conversational repair turn.
- The adapter classifies 429 as retryable/bounded and the API returns a clean
  `status: "unavailable"` with an Arabic `limitations_ar` line and an empty
  summary — it never fabricates a summary, facts, or a direction. `direction`
  stays `no_trade`.

Observed live degrade (exact API output, quota-exhausted path):

```json
{"status":"unavailable",
 "status_reason":"provider unavailable: provider returned HTTP 429: {\"error\":{\"message\":\"Daily token quota exceeded. Retry after 60 seconds.\",\"type\":\"insufficient_quota\",\"code\":\"quota_exceeded\"}}",
 "summary_ar":"", "facts":[], "direction":"no_trade",
 "provider_id":"llm7", "model_id":"nemotron-3-nano:30b"}
```

Consequence: the conversational repair turn is the **primary** path to a valid
live analysis, not an edge case. Reliable production analysis needs a paid
balance (the account reports a $0 balance) or a much lower call rate.

## 5. APK

- `ayzr-android/build/app/outputs/flutter-apk/app-debug.apk`, ~87 MB,
  599 entries, 5 `classes*.dex`, `lib/{arm64-v8a,armeabi-v7a,x86,x86_64}/libflutter.so`.
- `aapt dump badging`: `package com.ayzr.ayzr_app`, versionName 1.0.0,
  minSdk 21, targetSdk 34, label **أيزر**. `zipalign -c 4` OK, zip integrity OK.
- Built clean after the NDK pin (no NDK warning).
- **Not** installed or exercised on a real device or emulator — no Android
  runtime was available.

## 6. Multi-agent orchestration (verified real)

Local OpenHands agent-server on `127.0.0.1:60000`. Five specialist conversations
created via `tools/ayzr_agents/create_agents.py`; all five report
`execution_status: finished` and each has a persistence directory under
`/workspace/conversations/`:

| Agent | Conversation ID | Status |
|-------|-----------------|--------|
| A — Backend & Database | `cddc487e-bd1c-4afb-b613-ae77473cb0b6` | finished |
| B — Android App | `36cbf0a9-0a91-427f-be51-15c5043e4a09` | finished |
| C — AI News Intelligence | `6671f638-bc54-4607-b561-20cc680f85f3` | finished |
| D — Visual Design | `dc9cdb51-d19e-4716-bb8a-7b8a2ef828a7` | finished |
| E — Testing & Security | `38af1b2d-3d68-4910-9bda-7c3db2ad73fc` | finished |

## 7. Image tooling (verified real, honest scope)

Pillow 13.0.0 + CairoSVG 2.9.1 (CPU-only). **No generative AI synthesis** exists
in this environment — no diffusion model, no GPU, no text-to-image service. All
81 assets are procedurally drawn geometry, verified by 361 checks and a
mutation test (11 injected defects, 11 caught) and by Android's own `aapt2`.

## 8. Remaining work / limitations (not claimed as done)

1. **Live LLM7 analysis under rate limits** — blocked by the free-tier 429; needs
   a paid balance or a slower cadence. The repair turn itself is implemented and
   unit-tested; a clean repeated live-recovery run could not be completed.
2. **Real-device / emulator test** — no Android runtime available.
3. **Production deployment** — no Railway/Cloudflare credentials in this
   environment. `Dockerfile` and `railway.toml` are prepared; deployment is
   **not** performed.
4. **Cloudflare database** — the backend uses SQLite on a Railway volume; no
   Cloudflare database is used (see `CLOUDFLARE_DATABASE.md`).
5. **MT5 Sender EXE** — a separate project; only the receiving API contract is
   prepared (`MT5_SENDER_CONTRACT.md`), exercised with the mock sender.
6. **Pre-existing `test_bridge_t06.py` FAIL** — the stale-feed clock-offset
   weakness; documented with a suggested fix, not applied (outside this scope).

## 9. Verification pass (2026-10-29) — preparation section and calendar ordering

This pass added the home "preparation for the upcoming event" section
(`تحضير للخبر القادم`) with a countdown to the real event timestamp, relative
levels (`expected_move_points` / `stop_distance_points` /
`relative_levels_note_ar`), and AI gold-relevance, and it fixed a real ordering
defect found while validating the countdown against a live server.

### Defect fixed — upcoming events were farthest-first

`EventStore::recent` ordered *every* scope `scheduled_at DESC`. The header
documents "soonest-first for upcoming, newest-first when including past", so the
implementation contradicted its own contract. Against the live server the
`/events?scope=upcoming` list opened on `2026-10-31` while the true nearest event
was `2026-10-29T12:30Z` — the "next news" countdown read row 0 and would have
counted to the wrong event.

Fix: `src/macro/EventStore.cpp` now orders upcoming rows `ASC` and past rows
`DESC`. Pinned by two new cases in `MacroEventPipelineTests`
(`upcoming_events_are_soonest_first`, `past_events_are_newest_first`). The mobile
client also selects the minimum `scheduled_at` itself, so a future server
ordering change cannot silently surface the wrong event.

### Commands actually run

| Suite | Command | Result |
|-------|---------|--------|
| Backend binaries | `for t in build-local/*Tests; do ./"$t"; done` | **40/40 binaries PASS, 0 FAIL** |
| `MacroEventPipelineTests` | `./build-local/MacroEventPipelineTests` | **18 cases, 0 failed** (was 16; +2 ordering) |
| `NewsIntelligenceTests` | `./build-local/NewsIntelligenceTests` | **48 cases, 0 failed** |
| Python models | `python3 -m unittest discover -s tests/models` | **308 passed, 0 failed** |
| Flutter | `cd ayzr-android && flutter test` | **108 passed, 0 failed, 3 skipped** |
| Flutter analyze | `flutter analyze` | **No issues found** |
| Flutter live integration | `flutter test --tags live` vs a local server | **3 passed** |
| APK (debug) | `ANDROID_HOME=/workspace/tools/android-sdk JAVA_HOME=/workspace/jdk flutter build apk --debug` | `app-debug.apk` (~91 MB) |
| APK (release) | `ANDROID_HOME=/workspace/tools/android-sdk JAVA_HOME=/workspace/jdk flutter build apk --release` | `app-release.apk` (~22.7 MB) |

### Live checks against the running server

- `/api/v1/events` → 42 events, `sorted == True` (soonest-first), impact mix
  `{low: 27, medium: 6, high: 9}` — the preparation section is impact-agnostic.
- `/api/v1/news/analyze?id=…` → `direction: bullish`, `entry/stop_loss/
  take_profit: null` (no candle reference, so invented prices were dropped),
  `expected_move_points: 300`, `stop_distance_points: 120`,
  `relative_levels_note_ar` present. This is the grounding rule working end to
  end: a price that cannot be grounded is withheld, an honest distance survives.
- The app's own `ApiClient` (via `live_integration_test.dart`) reads the same
  fields from `levels`, matching the server exactly.

### Not claimed

- No device or emulator run: the APK is built and its manifest/permissions
  verified with `aapt`, but it was **not** installed on a real device.
- No deploy, no push. The live checks above ran against a local server on
  `127.0.0.1` with `ASTRA_LLM_PROVIDER=mock`.

## 10. Increment (2026-10-29) — tone / framing / subtext end to end

This increment added a third analysis dimension to the news pipeline: reading the
*voice* of an article (tone, strength, register, sensationalism, novelty,
uncertainty) and the context it assumes rather than states (hidden context,
framing bias). It is a full vertical slice — SQLite schema → store read/write →
API JSON → Flutter model → Arabic UI — with closed vocabularies and honest
`unknown` defaults throughout.

### Backend

| File | Change |
|---|---|
| `src/news/NewsStore.cpp` | 9 additive columns (`tone`, `tone_strength`, `tone_evidence_ar`, `source_register`, `sensationalism`, `novelty`, `uncertainty`, `hidden_context_ar`, `framing_bias_ar`); `kNewAnalysisColumns` probe set; INSERT/upsert SQL and bindings; read column list and row reader extended |
| `src/api/NewsApi.cpp` | `analysisJson` emits all nine fields; `unknown`/empty when absent |
| `src/news/MockLlmProvider.h` | mock JSON carries a representative classification so local tests exercise the whole path |

All nine are added by the same **additive `ADD COLUMN` behind a `hasColumn`
probe**, so the migration is idempotent and a pre-existing row reads back as
`unknown`/empty — never a guess. Tone is kept strictly separate from `direction`
and never feeds the decision chain.

### Flutter

| File | Change |
|---|---|
| `lib/data/models/news.dart` | parses the nine fields; `unknown` defaults; unrecognised values pass through verbatim |
| `lib/core/strings_ar.dart` | Arabic labels for tone, strength, register, sensationalism, novelty, uncertainty, plus section/caveat text |
| `lib/ui/widgets/analysis_view.dart` | tone card (`_ToneChip`, `_SubtextBlock`) — rendered only when a tone was classified, carrying a standing caveat that tone does not change the facts |

### Tests added

- `tests/NewsIntelligenceTests.cpp` — 6 cases (accept a full classification;
  absent fields are honestly unknown; reject out-of-vocabulary values; a tone
  without evidence is downgraded rather than displayed; fields survive the store
  round trip; the migration is idempotent on an old database). **48 → 54 cases.**
- `ayzr-android/test/models_test.dart` — 3 cases (full parse; absent ⇒
  `unknown`/empty; unrecognised tone passed through, not coerced).
- `ayzr-android/test/widget_test.dart` — 3 cases (label + quoted wording render;
  card omitted when nothing was classified; unrecognised tone shows
  **غير محدَّدة**).

### Commands actually run and their results

| Suite | Command | Result |
|-------|---------|--------|
| `NewsIntelligenceTests` | `./build-linux/NewsIntelligenceTests` | **54 cases, 0 failed** |
| `NewsApiIntegrationTests` | `./build-linux/NewsApiIntegrationTests` | **7 cases, 0 failed** |
| `NewsIntelligenceAgentCTests` | `./build-linux/NewsIntelligenceAgentCTests` | **34 cases, 0 failed** |
| All C++ binaries | `for t in build-linux/*Tests; do ./"$t"; done` | **38 PASS, 2 FAIL** — both pre-existing (see below) |
| Windows sender | `python3 -m unittest discover -s tests` | **81 passed** |
| Flutter tests | `cd ayzr-android && flutter test` | **114 passed, 0 failed, 3 skipped** |
| Flutter analyze | `flutter analyze lib/ test/` | **No issues found** |
| APK (debug) | `ANDROID_HOME=/workspace/tools/android-sdk JAVA_HOME=/workspace/jdk flutter build apk --debug` | `app-debug.apk`, 91.2 MB |
| APK (release) | `ANDROID_HOME=/workspace/tools/android-sdk JAVA_HOME=/workspace/jdk flutter build apk --release` | `app-release.apk`, 22.7 MB |

### End-to-end simulation (real server, mock LLM, mock sender)

```
mock_sender.py:  ingested=2, replay duplicates=2, invalid OHLC rejected=1, sync=1  PASS
news ingest:     stored=1 then duplicates=1 (no second row)
news analyze:    status=ok, direction=bullish,
                 tone=positive, tone_strength=moderate, source_register=wire,
                 novelty=new, uncertainty=medium, hidden_context_ar present
news/results:    article_available=true, tone read back from the store
auth:            no token → 401; sender token on a read route → 401
```

### The two failing C++ binaries are pre-existing, not regressions

`AyzrFailureInjectionTests` (18 cases, 1 failed) and `AyzrSecurityTests`
(19 cases, 1 failed) fail **identically on the pre-change binaries** in
`build-local/`, built before this increment. The security case is a *deliberate*
failing assertion — its own comment reads "This fails today (F-D-8)" and it is
guarded by `AYZR_SECURITY_ENFORCE_FIXES=1`. Neither test touches the news store.

One environmental cause was found and fixed: `libcurl4-openssl-dev` was absent
(`CURL_DIR-NOTFOUND`), so the outbound HTTP transport was not compiled and the
real provider could never be reached. After installing it (8.14.1-2+deb13u7) and
reconfiguring, the transport compiles and the live-LLM path is reachable. The two
assertions above are unchanged by that fix. See `docs/ayzr/TESTING.md` F-E-3.

### Not claimed

- Tone fields were exercised with the **mock** provider only; no live LLM7 call
  was made in this increment (no token, and none is embedded anywhere).
- No device or emulator run; APKs are built and their manifests verified with
  `aapt2`, but not installed on a real device.
- No deploy, no push, no commit.
