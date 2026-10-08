# SOLO AUDIT — AURA / ASTRA (Phase 6.0 Consolidation)

> **Sole agent:** solo (OpenHands). **Date:** 2026-10-08 12:08 UTC.
> **Repo:** `alimazna/asp` @ `main`, HEAD `1c3959a`, fresh clone.
> **Mode:** read-only audit first; then continuation. Baseline READ-ONLY;
> production PROTECTED; no live trading; no lookahead; RULE A/B/C/D/E unchanged.
> Everything is labelled **PROOF-OF-CONCEPT — single window**.

This report reconstructs the mission from Git history and the audit trail, states
what is genuinely done, and — critically — records one **post-close material
finding** that the team's own FINAL_REPORT does not yet carry.

---

## Section 0 — Executive summary

- The prior team (Lead DeepSeek + Agent-A/B/C/D) **closed the mission** at 11:05 UTC
  and delivered `coordination/FINAL_REPORT.md`. Almost every task is DONE.
- **The backend builds and all tests pass** (independently verified below).
- **One material gap remained and is now closed:** the designated auditor Agent-D
  filed a **post-close addendum** (`1c3959a`) proving the published headline
  calibration rests on training data the pipeline itself flags `INCOMPLETE`. This
  was **not** in `FINAL_REPORT.md`, and `coordination/tasks.md` still showed
  **T29 = ACTIVE**. The solo agent reproduced the finding independently and folded
  the disclosure into the report (§4b), corrected T29, fixed a one-line packaging
  gap (F-SOLO-1), and landed a `--valid-only` disclosure filter (see §8).
- The addendum is **correct**; the solo agent reproduced its decisive number
  independently (valid-only ECE **0.106959** → `report_and_pivot`).
- **Mission verdict is unchanged** (SCORE, no edge); the report is now accurate
  about *why* the headline is not evidential.

---

## Section 1 — What is DONE

All rows verified against `coordination/tasks.md`, `AUDIT_REPORTS/`, and HEAD.

| Task | Owner (historical) | Evidence (audit / tests) | Commit |
|---|---|---|---|
| T01 Feature extraction | Agent-A | `AUDIT-T01-feature-extraction.md` PASS (causality fix) | early |
| T02 Feature tests | Agent-A | `AUDIT-T02-feature-tests.md` PASS (RULE A, 180-pair sweep) | early |
| T03 Logistic baseline | Agent-B | `AUDIT-T03-logistic-baseline.md` PASS (119 tests) | early |
| T04 XGBoost/GBT + calibration | Agent-B | `AUDIT-T04-gbt.md` PASS (deep-tree fix; 178 tests) | early |
| T05 Calibration metrics | Agent-B | `AUDIT-T05-calibration.md` PASS (F1 fixed; 162 tests) | `6e8bd15`+ |
| T06 MT5 bridge | Agent-C | `AUDIT-T06-mt5-bridge.md` PASS | early |
| T07 Python bundling | Agent-C | `AUDIT-T07-python-bundling.md` PASS (see F-SOLO-1) | `7e7a752` |
| T09 Probability API | Agent-C | `AUDIT-T09-probability-api.md` PASS (caveat C-1 closed) | early |
| T10 Leakage audit | Agent-D | `AUDIT-T10-leakage.md` PASS | early |
| T11 Calibration audit | Agent-D | `AUDIT-T11-calibration.md` PASS (methodology) | early |
| T13 End-to-end evidential | Agent-C | `AUDIT-T13-evidential.md` + real-host 96/96 PASS | `2cffe42` |
| T14 Feature bounds/NaN guards | Agent-A | `AUDIT-T14-feature-bounds.md` PASS | early |
| T15 Decision model (horizon+SL/TP) | Lead+Agent-B | `AUDIT-T15-decision-model.md` PASS (F15-1 fixed) | `e26534f` |
| T16 Analysis API | Agent-C | `AUDIT-T16-analysis-api.md` PASS | early |
| T17 Freeze API v1 | Agent-C | `AUDIT-T17-api-freeze.md` + `AUDIT-T17-T19-reaudit.md` PASS | tag `api-v1.0` @ `ada0e9f` |
| T18 Frontend handoff guide | Lead | `AUDIT-T18-frontend-guide.md` PASS (F18-1..4 fixed) | `6b9a78e`-area |
| T19 Mock data generator | Agent-C | `AUDIT-T19-mock-data.md` PASS (39/39) | `ada0e9f` |
| T20 Cost tiers (RULE B) | Agent-B | `AUDIT-T20-cost-tiers.md` PASS (E04 closed) | `src/models/costs.py` |
| T21 Integration causality test | Agent-A | `AUDIT-T21-integration-causality.md` PASS | early |
| T22 API schema fixtures | Agent-A | `AUDIT-T22-fixtures.md` PASS (F22-4b) | `0fc7083` |
| T23 Frozen-contract checker | Agent-B | `AUDIT-T23-contract-checker.md` PASS (F23-1 fixed) | `d8d5304`-area |
| T24 T13 e2e harness | Agent-A | `AUDIT-T24-e2e-harness.md` PASS (88/88) | `6b9a78e`-area |
| T25 Real XAUUSD acquisition | Lead | `AUDIT-T29-realdata.md` Part 1 PASS | `research/data/...` |
| T26 M1→FeatureSet harness | Agent-A | `AUDIT-T26-interim.md` PASS | `real_corpus.json.gz` |
| T27 Real-data calibration (POC) | Agent-B | `t27_poc_report.json` + `T27_POC_NOTE.md`; reproduced | `840560c` |
| T28 Configurable data path | Agent-C | `test_data_paths_t28.py` 24/24 PASS | `research/features_real/README.md` |
| T29 Real-data audit | Lead (substitute) | `AUDIT-T27-realdata-2026-10-08.md` PASS | `6b9a78e` |
| T30 Contract shape-drift guard | Agent-C | `AUDIT-T30-interim.md` PASS (signed `89685c1`) | `89685c1` |
| T31 FINAL_REPORT + close-out | Lead | `coordination/FINAL_REPORT.md` (9 sections) | `45c976c` |

**Done: 29 of 31** (T12 DEFERRED by human; T29 technically DONE-as-substitute but
**stale in tasks.md** and superseded by the addendum — see §2/§3).

---

## Section 2 — What is IN PROGRESS / stale

| Task | Last owner | Last commit | State |
|---|---|---|---|
| **T29** | Agent-D → Lead substitute | addendum at `1c3959a` (11:28) | **tasks.md still says `ACTIVE`.** In reality: Part 1 PASS, 2a PASS, 2b PASS, **2c APPROVED with F-T27-1**. Needs a status decision, not more work. |
| T12 | Agent-D | — | DEFERRED by human (baseline controls unavailable). Not blocked. |

Nothing else is in progress. The mission was declared closed at 11:05 UTC.

---

## Section 3 — What is BLOCKED

Genuinely blocked items: **none** on the backend. One **documentation** blocker:

- **F-SOLO-2 — FINAL_REPORT omits the F-T27-1 disclosure.** The published
  `FINAL_REPORT.md` §4 states the headline (single split, Brier 0.24995, ECE 0.0018)
  but does **not** disclose that the **development partition (all 3,997 rows) is
  100% `INCOMPLETE` warm-up sets**, nor the valid-only sensitivity (ECE 0.107 →
  `report_and_pivot`). §6 lists "warm-up: 4,173 of 6,670 feature sets are
  INCOMPLETE" but never connects it to the training split.
  - **Why it matters:** RULE C/RULE E honesty — the headline "probability" verdict
    is an artifact of training on flagged-INCOMPLETE rows.
  - **Needed to unblock:** add the disclosure to FINAL_REPORT §4/§6 (I do this in
    Step 5 — no code change, no re-run needed).

Minor, non-blocking:
- **E03** (packaging contract C-1/C-2/C-3) OPEN — T08 (Windows packaging) HELD.
- **F-SOLO-1** (new, below) — bundling manifest omits `mt5_csv_feed.py`.

---

## Section 4 — What is NOT STARTED

| Task | Owner (historical) | Est. effort |
|---|---|---|
| T08 Windows packaging | Agent-C | Blocked on E03; not on the critical path |
| T12 Baseline control check | Agent-D | DEFERRED — needs the `base9`/`baseold` control corpus (absent) |

There is no un-started task essential to the mission's completion bar (§10.7).

---

## Section 5 — The Real Data

### Primary corpus (the POC verdict)
- **Path:** `research/data/xauusd_m1/xauusd_m1_real.csv` (operator MT5 export),
  converted to `research/features_real/corpus/real_corpus.json.gz`.
- **Coverage:** **100,008 M1 bars**, 2026-06-24 11:08 .. 2026-10-08 10:30 (broker
  time). Single ~3.5-month window.
- **Quality:** PASS — 0 duplicates, 0 OHLC violations, 0 NaN, 0 unexpected gaps
  (`QUALITY.md`). One honest WARN: price range 3942..4697 outside the directive
  band — real, reported, not repaired.
- **Feature sets:** 6,670 total / **2,497 valid** (4,173 `INCOMPLETE` warm-up).
- **T27 run?** **Yes** (POC). Headline (full corpus): dev/val/oos 3997/1333/1339,
  OOS Brier 0.249945, skill +0.000218, ECE 0.00178 → verdict *probability*.

### Appendix corpus (not the verdict)
- `research/data/xauusd_m1/2021..2025.csv.gz` (Dukascopy, 1,695,651 M1 bars,
  2021-01-03..2025-12-30, BID+ASK) → `real_corpus_2021_2025.json.gz`
  (113,083 sets / 107,403 valid). Decision-grade year split: OOS Brier 0.2497,
  skill +0.0012, ECE 0.0015. Same honest negative.

### Independent re-verification (this audit)
I reproduced the T27 number and the decisive sensitivity with the real code path
(`src.models.realdata.run_real_calibration`, `--partition-mode fraction`, `l2=1e-6`):

| Corpus | dev/val/oos | OOS Brier | skill | **ECE** | verdict |
|---|---|---|---|---|---|
| FULL (6,670) | 3997/1333/1339 | 0.249945 | +0.000218 | **0.00178** | probability |
| VALID-ONLY (2,497) | 1493/505/498 | 0.261646 | −0.046586 | **0.106959** | **report_and_pivot** |

And directly from timestamps: **development partition = 0 of 3,997 valid (100%
INCOMPLETE)**; validation 1,158 of 1,333 valid; OOS 1,339 of 1,339 valid. This
**confirms the Agent-D addendum (F-T27-1) exactly.**

### Independent data-quality re-verification (raw CSV, this audit)

The roster's independent auditor (Agent-D) was STALE for the entire MT5 window, so
T25's data claims were only *substitute*-audited by the Lead. I re-derived the hard
checks **directly from the raw CSVs**, independent of `QUALITY.md`:

**MT5 primary corpus** — `xauusd_m1_real.csv`:

| Check | Reported (`QUALITY.md`) | Re-derived |
|---|---|---|
| Bars | 100,008 | **100,008** |
| Coverage | 2026-06-24 11:08 .. 2026-10-08 10:30 | **identical** |
| Duplicates / non-monotonic | 0 | **0** |
| Off-grid timestamps | 0 | **0** |
| NaN OHLCV | 0 | **0** |
| OHLC violations | 0 | **0** |
| Price range | 3942.48..4696.73 (WARN) | **3942.48..4696.73** |
| Session gaps | 76 (16 weekend) | 78 by a raw `>1min` rule |

The gap count differs by **2** only because the report uses a documented
session/weekend-threshold definition; the report explicitly states the unexpected-
gap count is **0**. No defect; the *material* claims all reproduce exactly.

**Dukascopy appendix corpus** — `2021..2025.csv.gz`: re-derived row counts match
`metadata_dukascopy_2021_2025.json` **exactly** (344,687 / 341,226 / 342,204 /
331,293 / 336,241 = **1,695,651**), with **0** non-monotonic, **0** OHLC
violations, **0** NaN, price range 1614.71..4549.72 (real multi-regime gold).

**Conclusion:** the data foundation is **independently confirmed**. The POC verdict
rests on real, clean data; no correction needed.

---

## Section 6 — The Backend Build

- `find . -name CMakeLists.txt -maxdepth 3` → **`./CMakeLists.txt`** (one file).
- **Build:** PASS. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then
  `cmake --build build -j4` → **0 errors, 0 warnings** (`-Wall -Wextra`). Installed
  `cmake 4.4.4` via pip (the base image lacked cmake).
- **C++ tests:** `ctest` → **19/19 PASS**.
- **Python suites (independently run):**
  - models: **305 tests OK**
  - integration: `test_e2e_frozen_v1` **88/88 PASS**, `test_contract_t16` **36/36**,
    `test_api_fixtures` PASS, `test_mock_api_t19` **39/39**, `test_mock_shape_t30`
    **19/19**, `test_data_paths_t28` **24/24**, `test_e2e_real_host_t13` **52/52**,
    `test_bridge_t06` **25/25**.
  - `test_bundling_t07`: **17/18** — one FAIL, investigated (F-SOLO-1 below).

**Environment note:** the base image has no `cmake`; the prior team's DAILY_SUMMARY
recorded "C++ tests compiled manually". With cmake installed, the full build graph
is green.

### F-SOLO-1 (new, minor) — bundling manifest omits a bridge file
`tests/integration/test_bundling_t07.py` asserts *no bridge source file is omitted
from the manifest*. It FAILS:
`undeclared: ['mt5_csv_feed.py']`.

- `bridge/mt5_python/mt5_csv_feed.py` (added by Agent-C in T13, commit `4932b71`)
  is a **real bridge module** — it is the CSV replay feed the **packaged bridge's
  `MetaTrader5` shim** (`tests/integration/fake_mt5/MetaTrader5.py`) imports by name
  to run the real bridge + real host over real gold data.
- `packaging/bundle_manifest.json` `bridge_files` lists 6 files but not this one.
- **Impact:** a release bundle staged from the manifest would omit a module the
  packaged bridge imports at runtime → the bridge fails to start in the T13 replay
  configuration. Low-severity for the POC (the shim path is the evidential harness),
  but a genuine packaging defect and a red test.
- **Fix:** add `"mt5_csv_feed.py"` to `bridge_files`. In-zone (Agent-C's
  `packaging/`), one line, does not touch production runtime code.
  **DONE (Step 5)** — `test_bundling_t07` now **18/18**.

---

## Section 7 — What Remains to Finish

1. ~~**Fold the F-T27-1 disclosure into `coordination/FINAL_REPORT.md`** (§4b + §5 +
   §6): state that the development partition is 100% INCOMPLETE and record the
   valid-only sensitivity (ECE 0.107 → `report_and_pivot`).~~ **DONE (Step 5).**
2. ~~**Correct `coordination/tasks.md`:** T29 `ACTIVE` → `DONE`.~~ **DONE (Step 5).**
3. ~~**Fix F-SOLO-1:** add `mt5_csv_feed.py` to `packaging/bundle_manifest.json`.**~~
   **DONE (Step 5)** — `test_bundling_t07` now 18/18.
4. ~~**Land the `realdata.py` valid-row disclosure + filter (F-T27-1).**~~
   **DONE (Step 5)** — `--valid-only` / `valid_filter`, counts in report + summary,
   3 new tests; models suite 308 OK.
5. **Record the F-T27-1 addendum review** in the coordination trail. **DONE.**

*(Optional, out of scope)* E03 packaging-contract items C-1/C-3 to unblock T08.

No model change and no data change was needed: the verdict is already correct and
honestly negative; the work was disclosure + one packaging line + one filter.

---

## Section 8 — Continuation performed (Step 5)

| # | Action | Evidence |
|---|---|---|
| 1 | `FINAL_REPORT.md` §4b added (F-T27-1); §5 verdict and §6 limitation updated; evidence table refreshed (305 models, T28 24/24, ctest 19/19) | commit below |
| 2 | `tasks.md` T29 `ACTIVE` → `DONE` (addendum 2c recorded) | commit below |
| 3 | `packaging/bundle_manifest.json` declares `mt5_csv_feed.py` | `test_bundling_t07` **18/18** |
| 4 | `src/models/realdata.py`: `valid_filter` / `--valid-only`; report + summary now record `n_valid_sets` / `n_invalid_sets` / `valid_only` and disclose INCOMPLETE rows | `tests/models/test_realdata.py` +3 tests; models **308 OK** |

**Re-verification after the changes:** models suite **308 OK**; `test_bundling_t07`
**18/18**; default (unfiltered) real-corpus run **unchanged** (Brier 0.249945,
ECE 0.00178, verdict `probability`) and now carries the disclosure note;
`--valid-only` reproduces **ECE 0.106959 → `report_and_pivot`** exactly matching
Agent-B's filed `t27_poc_validonly_report.json`.

### Frontend handoff + API freeze (verified, no change needed)

- **Frozen contract:** `docs/architecture/API_V1_SCHEMA.json` — **15 endpoints**,
  tag **`api-v1.0`** present (`ada0e9f`). Post-tag schema commits (`3d01b98`,
  `d53da25`, `e2cc9d7`, T30) are **additive only**, which the contract's own rule
  permits; the tag stands.
- **Additivity mechanically checked** (`git diff api-v1.0 HEAD`): **273 added
  lines / 16 changed lines**, and every one of the 16 is a *replacement that keeps
  its constraint* — no route, field, or `const` was removed. Safety constants are
  intact (`live_trading_authorised: {"const": false}`,
  `transport: {"const": "http_loopback"}`, `loopback_only: {"const": true}`).
  One honest nuance: two previously-**open** sub-objects (`quality`, `context`)
  were **tightened** from `{"type": "object"}` to declared field shapes. That is
  schema-narrowing, not loosening — and it declares exactly what the real host
  serves (36/36 host contract + 88/88 e2e), so no consumer breaks; no existing
  route or field was dropped.
- **Machine-checked both sides:** `test_contract_t16.py` validates the **real host**
  (36/36); `test_mock_api_t19.py` validates the **mock** (39/39);
  `test_e2e_frozen_v1.py` drives the whole surface end-to-end (88/88);
  `test_api_fixtures.py` PASS.
- **Handoff guide:** `docs/frontend/FRONTEND_HANDOFF_GUIDE.md` (271 lines) + the
  self-contained `docs/frontend/ASTRA_FRONTEND_HANDOFF.md` (362 lines). The display
  rule is pinned correctly: `signal.probability_calibrated` is the single source of
  truth, `meta.score_is_probability` is always `false` in v1, and `null`/`UNKNOWN`
  renders as *unavailable*. Consistent with the F-T27-1 verdict (surface a **score**).
- **Conclusion:** handoff and freeze are **intact**; no further action.

---

## Section 9 — Recommended Next Action

**Fold the F-T27-1 disclosure into `FINAL_REPORT.md` and close T29 in `tasks.md`,
then fix the one-line bundling manifest gap (F-SOLO-1).** The engineering is
finished and green; the only thing standing between this repo and a fully honest
close-out is that the published report does not yet state that the headline
"probability" was trained on a **100% INCOMPLETE** development partition, while the
2,497 valid sets the directive names yield ECE 0.107 → `report_and_pivot`. The
verdict (SCORE, no edge) does **not** change — the report only becomes accurate
about *why* the number is not evidential. This is documentation plus a one-line
manifest fix, not a modelling task.

<!-- AI agent (OpenHands/solo) on behalf of the operator -->