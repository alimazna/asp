# AYZR Premium Evolution — session report (2026-11-02)

Branch `feat/ayzr-premium-evolution` (from `feat/ayzr-ecosystem`). PR
[#22](https://github.com/alimazna/asp/pull/22). Everything below was run in this
environment; nothing is inferred from intent.

## 1. What was asked, what was delivered

| Task | Status | Evidence |
|---|---|---|
| Premium web observatory redesign (Arabic RTL) | DONE | live browser check, real backend data |
| Visual design & asset pipeline | DONE (limited) | 3/3 tests; no generative model available |
| Flutter/Android APK build validation | DONE | 153 pass; debug + release APK built |
| `docs/ayzr/VISUAL_DESIGN.md` | DONE | 354 → 375 lines, §8 added |

## 2. Test totals (measured this session)

| Suite | Command | Result |
|---|---|---|
| C++ binaries | `for t in build-ayzr/*Tests; do ./"$t"; done` | **50 pass / 0 fail** |
| — AI & news subset | `./build-ayzr/{Llm*,News*,AyzrNewsImpact}Tests` | 6/6 pass |
| Flutter | `LD_LIBRARY_PATH=/workspace/tools/libshim flutter test` | **153 pass / 4 skip / 0 fail** |
| Observatory security | `python3 tools/ayzr_observatory/tests/test_observatory_security.py` | **22 pass / 0 fail** |
| Asset pipeline | `node --test tools/ayzr_assets/tests/*.test.mjs` | **3 pass / 0 fail** |
| ASTRA CORE gate | `PYTHONPATH=. python3 research/ayzr_quant_lab/tests/test_astra_protection.py` | **PASS — unchanged** |

`git diff <baseline> HEAD -- src/runtime/ src/scoring/ src/core/` is **empty**:
the engine is byte-identical to the frozen baseline.

## 3. APK artifacts

| Artifact | Size | Notes |
|---|---|---|
| `ayzr-android/build/app/outputs/flutter-apk/app-debug.apk` | 101.6 MB | |
| `ayzr-android/build/app/outputs/flutter-apk/app-release.apk` | 23.1 MB | signed v1+v2, minified + tree-shaken |

`aapt dump badging`: `com.ayzr.ayzr_app`, `versionName=1.0.0`, `minSdk=21`,
`targetSdk=34`, `INTERNET` permission only. `apksigner verify`: **Verifies**,
v1 true, v2 true.

The release build was proven with a **throwaway keystore in `/tmp`**
(`/tmp/ayzr-test.jks`, deleted afterwards along with
`ayzr-android/android/key.properties`). `android/key.properties` is git-ignored
(`ayzr-android/android/.gitignore:11`), so no key material was committed. The
build refuses to run without a real keystore rather than debug-signing a release
— verified by the failure message it emits.

## 4. Live backend checks (real process, real data)

Server: `build-ayzr/astra_server --host 127.0.0.1 --port 8081`.

**Candle ingest** (`POST /api/v1/candles/ingest`):

| Request | Result |
|---|---|
| no `Authorization` | **401** `unauthorized` |
| valid candle + sender bearer | `state: stored_new`, `ingested: 1` |
| same candle again | `duplicates: 1`, `accepted: 0` — idempotent |
| `high < low` (OHLC order violated) | `state: rejected`, reason `candle OHLC ordering violated` |

**Proxy boundaries** (`tools/ayzr_observatory/serve.py`):

| Probe | Result |
|---|---|
| `POST/PUT/DELETE/PATCH /api/v1/candles/ingest` | **405** |
| `GET /api/v1/admin/candles` | **403** (not on the read allowlist) |
| `GET /../../etc/passwd` | **404** |
| `GET /api/v1/candles?tf=M15&limit=5` | **200** |

## 5. LLM7 — real integration findings

The key supplied for this session was used **once**, from a file at
`/tmp/.llm7tok` (mode 600, outside the repository). It was never written into
the repo, a log, or a commit. It must be treated as exposed in conversation and
rotated.

### 5.1 The build had no outbound HTTP client

The first live analysis returned:

```
status: unavailable
status_reason: provider unavailable: outbound HTTP transport not compiled (libcurl missing)
```

`libcurl4` was present but `libcurl4-openssl-dev` was not, so CMake's
`find_package(CURL)` failed and `ASTRA_HAVE_CURL` was never defined. After
`apt-get install libcurl4-openssl-dev`, `find_package(CURL)` reported
`found version 8.14.1`, the build defined `ASTRA_HAVE_CURL=1`, and
`ldd build-ayzr/astra_server` showed `libcurl.so.4`. **This was a local
toolchain gap only** — `Dockerfile` already installs both packages, so Railway
was never affected. Documented in `docs/ayzr/DEPLOYMENT.md`.

### 5.2 The model is a reasoning model; the default timeout is too short

Measured directly against `https://api.llm7.io/v1`:

| prompt | `max_tokens` | completion tokens | wall time | content |
|---|---|---|---|---|
| "say hello" | unset | 584 | — | non-empty |
| 120-word gold analysis | unset | 11,264 | **75.6 s** | full Arabic answer |
| 3-key JSON | 800 | 800 (`finish_reason=length`) | 7.7 s | **empty** |

`nemotron-3-nano:30b` spends most of its budget reasoning before emitting
content. With the default `LLM7_READ_TIMEOUT_MS=45000` a real analysis times out
(`provider unavailable: Timeout was reached`); with a small `max_tokens` the
response arrives empty. Both were observed, not assumed. The full table and the
recommended settings (`LLM7_READ_TIMEOUT_MS>=120000`, leave `max_tokens` unset)
are now in `docs/ayzr/DEPLOYMENT.md`.

### 5.3 Rate limiting and quota are handled honestly

A burst of requests produced a genuine `429`:

```json
{"error":{"message":"Rate limit exceeded. Retry after 1 seconds.",
          "type":"rate_limit_error","code":"rate_limit_exceeded","retry_after":1}}
```

`isQuotaExhaustedBody()` matches none of its markers in that body, so it is
classified `RateLimited` (retryable, no cooldown) — correct. Later, after the
reasoning-model token burn, the provider's own reset window was recorded as
`retry_after ≈ 89030 s` (24.7 h) and the monitor reported
`provider daily quota exhausted; cooling down` with
`chain_summary: all_quota_exhausted`. The API served the honest Arabic
limitation text instead of a fabricated analysis.

### 5.4 What the live analysis produced

- Article ingest: `stored_new`, `article_id: https://example.com/fed-cut`.
- Analysis schema fully populated (Arabic summary, facts, implications,
  supporting/opposing evidence, tone, gold channel, direction, levels,
  ASTRA-agreement, limitations) — the model returned
  `status: invalid_model_output` with `invalid direction: ''` on the first
  schema attempt, i.e. the defensive parser rejected unparseable output rather
  than accepting it. That is the designed behaviour for a model with no JSON
  mode.
- The bounded repair turn exists in `NewsAnalysisService.cpp` and is
  documented as verified against this exact model; it did not complete here
  because the provider hit its daily budget during probing.

**Conclusion:** the LLM7 path is wired end-to-end and honest. A production
analysis needs `LLM7_READ_TIMEOUT_MS>=120000` and a paid/quota-sufficient
balance. No live LLM7 call is made by any test.

## 6. Web observatory

Redesign verified live in a browser against the running backend:
overview `XAUUSD 3,899.62 / −2.52 / −0.06%` with the published M15 opportunity
(entry 3,899.62, stop 3,910.25, targets 3,885.45 / 3,878.36); analysis view
showing ASTRA `اتجاه هابط`, direction `هابط`, score 81.6, R:R 1.67 across the
nine timeframes.

A regression introduced by the redesign was found and fixed: splitting the UI
into `index.html` + `assets/app.js` silently narrowed the page-level security
suite (it read only `index.html`), dropping four rules from 22 to 16 passing.
The suite now loads `index.html` **plus every `assets/*.js`**, and `app.js`
builds URLs from `window.location.origin`. **22/22 pass.**

## 7. Visual design

`tools/ayzr_assets/generate_assets.py` — pure Pillow, offline, deterministic.
The environment has **no** ImageMagick, **no** SVG rasteriser and **no**
generative model, so the assets are drawn from explicit coordinates rather than
generated. Five assets emitted; regeneration is byte-for-byte identical (test
asserts it). Capability boundary stated plainly in `docs/ayzr/VISUAL_DESIGN.md`
§1 and §8.

## 8. Not done, and not claimed

- **No production deployment.** No Railway, Cloudflare or LLM7 credential is
  configured for deployment in this environment.
- **No device or emulator run.** No Android device is attached. The APKs were
  built and statically verified only.
- **No live LLM7 call in tests.** All tests use `MockLlmProvider`.
- **No multi-agent platform.** `api.all-hands.dev` returned 401 and the local
  agent-server refused; work was staged as specialist roles, disclosed in
  `coordination/ayzr-premium/BOARD.md`.
- **The supplied LLM7 key is compromised by disclosure** and should be rotated.

## 9. Commits

| Commit | Subject |
|---|---|
| `2a3f6d7` | fix(ayzr): treat scheduled market closures as expected, not data defects |
| `903d104` | feat(web): premium blue-forward observatory redesign (Arabic RTL) |
| `16b41f1` | feat(visual): deterministic Pillow asset pipeline + brand assets |
| `8dcc73b` | docs(ayzr): record verified phase results and the web asset generator |
| `47358a2` | fix(web): cover the split UI in security tests and pin same-origin fetches |
