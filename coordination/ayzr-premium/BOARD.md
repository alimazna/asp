# AYZR Premium Evolution — Team Board

Owner: principal agent (this conversation). Branch: `feat/ayzr-premium-evolution`.
Baseline backup: `backup/ayzr-before-premium-evolution-20261102-093104`
@ `b1722e5ba277a42bbdd2a15260f20520ed25b06a` (verified on GitHub).

## Environment capability disclosure (verified, not assumed)

| Capability | Status | Evidence |
|---|---|---|
| Multi-agent delegation (OpenHands conversations) | **UNAVAILABLE** | `api.all-hands.dev` unreachable (curl 000); local agent-server on :8001 refused (000); production runtime `/api/conversations` → **401**. |
| Git worktrees / branches | AVAILABLE | used for the backup branch |
| Node 24.21 / npm 11.21 | AVAILABLE | `node --version` |
| Python 3.13 + Pillow | AVAILABLE | installed this session |
| C++17 toolchain + cmake | AVAILABLE | 49/49 test binaries built |
| Playwright | NOT INSTALLED | `import playwright` fails |

Consequence: the work is executed as **staged specialist roles played by the
principal agent**, each with its own memory file under `memory/agents/`, not as
separate running conversations. This is disclosed rather than simulated.

## Roles and file ownership

| # | Role | Owns | Must not touch |
|---|---|---|---|
| 1 | Chief Architect / Director | `coordination/ayzr-premium/**`, `docs/ayzr/**` | engine math |
| 2 | Git / Infra / Dev Env | `.github/**`, `docker/**`, tooling installs | app logic |
| 3 | Quant Research & Validation | `research/ayzr_quant_lab/**` | `src/**` |
| 4 | Strategy Engines | `src/ayzr/engines/**` | `src/runtime/**`, `src/scoring/**` (ASTRA) |
| 5 | Central Orchestrator | `src/ayzr/ecosystem/CentralOrchestrator.*`, `DecisionLedger.*` | engine math |
| 6 | BiQuote / Market Data | `tools/ayzr_agents/**`, data contracts | ASTRA |
| 7 | UI/UX Design Director | `docs/ayzr/VISUAL_DESIGN.md`, design tokens | backend |
| 8 | Premium Web Frontend | `tools/ayzr_observatory/**` | backend |
| 9 | Web Integration & Security | `tools/ayzr_observatory/serve*.py` | engine math |
| 10 | Android Flutter | `ayzr-android/**` | backend |
| 11 | QA Automation | `tests/**`, `tools/**/tests/**` | product code |
| 12 | Reliability / Perf / Security Audit | `docs/ayzr/backend-hardening/**` | product code |

## Absolute constraints (from SHARED_MEMORY.md §0)

- XAUUSD only. **SHADOW ONLY** — no order execution, ever.
- **ASTRA CORE frozen.** Fingerprint
  `aee61fa9225b4fc6a3ba6dd7a2d82640227ec07179cd592e42b93e526889c480` (15 cases)
  must stay identical. Gate: `python3 research/ayzr_quant_lab/tests/test_astra_protection.py`.
- Never invent prices/candles/news/indicators/probabilities. Unavailable ⇒ `N/A`.
- No live LLM calls in tests. `ASTRA_LLM_PROVIDER=mock`.
- Never commit secrets. The previously-exposed LLM7 key is compromised: do not use.

## Baseline (measured 2026-11-02, before any change)

- ASTRA CORE protection gate: **PASS — ASTRA CORE unchanged**.
- C++ suite: **49/49 binaries pass, 0 fail**.
- Quant lab: 5/5 pass when run with `PYTHONPATH=.` (`test_lab_registry.py` needs it).
- Reference image: `docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg` (1280×698).
  The literal name in the brief ("AYZR Arabic Trading Intelligence Dashboard")
  does **not** exist in the repo; this is the only brand reference present.
- Android screens: **14** real `lib/ui/screens/*.dart` files (documents disagreed).

## Phase board

| Phase | Role | Status | Tests | Blockers |
|---|---|---|---|---|
| 0 Backup checkpoint | 2 | DONE | remote sha == local sha | none |
| 1 Team/env | 1,2 | DONE | tooling probed | no multi-agent platform |
| 2 Full audit | 1 | DONE | baseline green | none |
| 3 Arch map | 1 | DONE | see docs/ayzr/** | none |
| 4 Visual design | 7 | DONE | 3/3 asset tests | no generative model (disclosed) |
| 5 Design system | 7,8 | DONE | design-system.css | none |
| 6 Web redesign | 8 | DONE | live browser check | none |
| 7 Android | 10 | DONE | 153/153 + APK built | no Java for release signing |
| 8 Engines | 4,11 | DONE | 50/50 C++ | none |
| 9 Orchestrator | 5,11 | DONE | ledger live | none |
| 10 BiQuote/data | 6 | DONE | real feed ingested | none |
| 11 Session/regime | 3,11 | PARTIAL | closure fix done | none |
| 12 Quant lab | 3 | DONE | 5/5 (PYTHONPATH=.) | none |
| 13 Ledger/replay | 5,11 | DONE | ledger reads live | none |
| 14 E2E | 11 | PARTIAL | web E2E done | device test unavailable |
| 15 Security/perf | 9,12 | PARTIAL | proxy read-only verified | prod creds not present |
| 16 Device/browser | 10,8 | PARTIAL | browser OK; no emulator | no Android device |
| 17 Acceptance | 1 | IN PROGRESS | | |

Status vocabulary: PASS / PARTIAL / FAIL / BLOCKED / NOT TESTED.

## Session log — 2026-11-02

### T3 Web observatory redesign → DONE
`tools/ayzr_observatory/{index.html,assets/design-system.css,assets/app.js}`.
Replaced the single-file gold UI (711 lines) with a modular blue-forward
institutional design: sidebar, six views, layered navy surfaces, gold demoted to
one restrained accent. **Verified live in a real browser** against the running
backend with real BiQuote bars:
- Overview: XAUUSD 3,899.62, −2.52 (−0.06%), published M15 opportunity
  (entry 3,899.62 / stop 3,910.25 / targets 3,885.45, 3,878.36).
- Analysis: 9 timeframes; ASTRA regime=اتجاه هابط, direction=هابط, score 81.6,
  R:R 1.67, gate مجتازة.
- History: persisted decision ledger (قرار منشور) + activity log.
Commit `903d104`.

### T4 Visual-design workflow → DONE (with disclosed limit)
`tools/ayzr_assets/generate_assets.py` (pure Pillow, offline, deterministic).
Environment has **no** ImageMagick, **no** SVG rasteriser, **no** generative
model — disclosed in `docs/ayzr/VISUAL_DESIGN.md §1`. Five assets emitted;
3/3 Node tests pass (emission, exact dimensions, byte-for-byte determinism).
Commit `16b41f1`.

### T4 Android build → DONE
`flutter test` → **153 passed, 4 skipped, 0 failed** (needs
`LD_LIBRARY_PATH=/workspace/tools/libshim` for sqflite's libsqlite3).
`flutter build apk --debug` → `app-debug.apk` (101.6 MB), package
`com.ayzr.ayzr_app`, label `أيزر`, 357 Arabic strings in `strings_ar.dart`.
Release signing blocked: no JDK in the image.


### T5 LLM7 live path + e2e lifecycle → DONE (local)

**libcurl gap closed.** The first live analysis reported
`outbound HTTP transport not compiled (libcurl missing)`. `libcurl4` was present
but `libcurl4-openssl-dev` was not, so CMake's `find_package(CURL)` failed and
`ASTRA_HAVE_CURL` stayed undefined. After installing the header, CMake found
`CURL 8.14.1`, the build defined `ASTRA_HAVE_CURL=1`, and `ldd` showed
`libcurl.so.4`. **Local toolchain only** — `Dockerfile` already ships both
packages, so Railway was never affected.

**Root cause of the failing analysis was the timeout, not the transport.**
`nemotron-3-nano:30b` is a reasoning model: it burns most of its token budget
before emitting content. Measured live: a 120-word gold analysis took **75.6 s**
and 11,264 completion tokens; a `max_tokens=800` request returned **empty**
content with `finish_reason=length`. The default `LLM7_READ_TIMEOUT_MS=45000`
therefore timed out every real analysis. Default raised to `120000` in
`AstraServerMain.cpp`; measured table added to `docs/ayzr/DEPLOYMENT.md §LLM7`.

**Quota handling verified honest.** A burst produced a real `429`
(`rate_limit_error`, `retry_after: 1`) → classified `RateLimited`, no cooldown.
The reasoning-model token burn later hit the provider's daily budget
(`retry_after ≈ 89030 s`) → `provider daily quota exhausted; cooling down`,
`chain_summary: all_quota_exhausted`. No fabricated analysis was served.

**End-to-end lifecycle** (mock provider, real server, `/tmp/e2e_test.py`):
news ingest `stored_new` → duplicate `duplicate_content` (content dedup) →
10 M15 bars `ingested: 10` → identical batch `already_stored` ×10 (idempotent) →
read back `bars` count 5 → analysis `status: ok / direction: bullish` in Arabic →
no-auth ingest **401** → bad OHLC candle `rejected`. Field names confirmed
against the running server: `source_id` + `candle.time` on write; `tf`/`limit`
and a `bars` array on read.

Commits `b3387af`, `5d2ae65`. **Still not done:** production deployment (no
Railway/Cloudflare credentials here) and device/emulator install.
