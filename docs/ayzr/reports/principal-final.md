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

- Endpoint `https://api.llm7.io/v1` reachable; `nemotron-3-nano:30b` present in
  the models list; response shape is OpenAI-compatible.
- **First-pass schema adherence is poor**: the model returned *valid JSON that
  violates the schema* (top-level keys folded into a `facts` element), giving
  `invalid_model_output: invalid direction: ''`.
- **The free tier rate-limits aggressively** (HTTP 429 within seconds of
  repeated calls). This blocked a clean repeated live recovery measurement. The
  adapter classifies 429 as retryable/bounded, and the app degrades to
  `unavailable` while still serving cached news + market data.
- Consequence: the conversational repair turn is the **primary** path to a valid
  live analysis, not an edge case. A paid balance (or a much lower call rate) is
  required for reliable production analysis.

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
