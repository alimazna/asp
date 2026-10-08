# Agent-C — Task Board (claims / status updates)
> Write ONLY here for task status. NEVER edit `coordination/tasks.md`.
> The Lead reads this file every cycle and syncs `tasks.md` from it.

## Protocol
- Append-only. One entry per claim or status change.
- Format:
  ```
  ### [YYYY-MM-DD HH:MM UTC] T<id> → <STATUS>
  Note: <one line>
  ```
- Valid STATUS: IDLE, ACTIVE, REVIEW, DONE, BLOCKED.
- Owner claims with ACTIVE + lease (per `README.md` §F); marks REVIEW when
  submitted; the Lead marks DONE after audit.

## Entries

### [2026-10-07 21:06 UTC] T06 → ACTIVE
Note: claimed MT5 bridge harden; lease until 21:36 UTC.

### [2026-10-07 21:25 UTC] T06 → REVIEW
Note: bridge hardened (D-1/D-2/D-3); 25/25 + 12/12; submitted for audit.

### [2026-10-07 21:35 UTC] T07 → REVIEW
Note: Python bundling delivered; 17/17 + 25/25 + 12/12; C-1/C-2 reported.

### [2026-10-07 22:17 UTC] T09 → REVIEW
Note: Probability API (src/api/ProbabilityApi.{h,cpp}) + /api/v1/probability/latest;
RULE C gate (calibrated=false / null probability unless calibrated AND audited);
10/10 new tests, CTest 13/13, no warnings; submitted for Agent-D audit.

### [2026-10-07 23:05 UTC] T16/T17/T19 → REVIEW
Note: T16 analysis API (nulls honest, SHARED RULE C gate with /probability);
T17 freeze v1 (BACKEND_FRONTEND_API_V1.md + API_V1_SCHEMA.json, tag api-v1.0);
T19 mock (scripts/mock_api.py + test 39/39, --check 0 failures). CTest 14/14.
T18 input: six guide corrections sent to Lead.

### [2026-10-07 23:08 UTC] T16/T17/T19 → REVIEW (Lead)
Note: audits requested from Agent-D; T18 guide corrected per Agent-C input.

### [2026-10-08 00:12 UTC] T16 → DONE (Lead)
Note: Agent-D PASS — shared RULE C gate verified; honest nulls; envelope/schema
valid. T17 → ACTIVE (F17-1 impl-vs-schema check + F17-2 tag). T19 → ACTIVE
(F19-1 frozen-null fidelity + F19-2 score_is_probability always false).

### [2026-10-07 22:35 UTC] T16/T17/T19 -> REVIEW
Note: Phase 4.0 surface delivered. Analysis API (RULE C gate shared with /probability), API v1 frozen (schema json + doc), mock server. Evidence: ProbabilityApiTests 10/10, AnalysisApiTests 10/10, CTest 14/14, T19 39/39. T18 input (6 corrections) sent to Lead. Submitted for Agent-D audit.

### [2026-10-07 22:45 UTC] C-1 follow-up -> REVIEW
Note: durable RULE C gate. applyCalibrationAudit(path) binds to T11 artifact; synthetic PASS stays closed. Host wired. ProbabilityApiTests 14/14, CTest 18/18.

### [2026-10-08 06:11 UTC] T17/T19 -> BLOCKED (Lead; owner OFFLINE)
Note: Agent-C silent since 2026-10-07 23:10 UTC (~7h) — OFFLINE (E08). Leases released.
NOT abandoned; resumes on return. Open items waiting: F17-0 (durable-gate substring),
F17-1 (two-layer impl-vs-schema), F17-2 (tag); F19-1 (frozen-null fidelity), F19-2
(score_is_probability always false); F22-4b-v (validator finite guard); D-1.

### [2026-10-08 06:22 UTC] T17 -> REVIEW (Agent-C returned; contract test)
Note: test_contract_t16.py drives the real host vs API_V1_SCHEMA.json; found 2 real
freeze-vs-impl divergences (shadow/* arrays; quality object), corrected schema+mock.
D-1 tracked (snapshot UNOBSERVED quality string vs object). SIDE-EFFECT: the schema
correction invalidated 4 Agent-A fixtures (shadow_positions/outcomes count-object;
quality string) -> 4/51 fixture checks FAIL. Agent-C to refresh mock-derived fixtures
in-zone (same change that moved the schema), then submit T17 to Agent-D.

### [2026-10-08 06:20 UTC] T17/T19 → ACTIVE (resumed; partial fixes landed)
Note: container returned after ~7h. Landed F17-0 (leading-verdict RULE C gate),
F19-1/F19-2/E07 (mock fidelity + score_is_probability always false), F22-4b-v
(validator NaN guard), schema match for shadow arrays + snapshot quality object,
and the real-facade impl-vs-schema contract test (F17-1 base). Evidence: mock
--check 0 failures, T19 39/39, CTest 18/18 (ProbabilityApiTests 16/16).
Commits: 2f65b27, acc74f7. Open: F17-1 full (consume T22 fixtures + T23 checker),
F17-2 (tag), D-1 (snapshot quality type, tracked known defect).


### [2026-10-08 06:35 UTC] T17 → REVIEW / T19 → REVIEW
Note: all findings closed at ada0e9f. F17-0 (leading-verdict gate), D-1 (snapshot
quality object, fixed in-zone), 4 T22 fixtures refreshed (51/51), F17-1 full (T23
checker consumed, 36/36), F17-2 (tag api-v1.0 pushed), F19-1/F19-2 (E07, mock
fidelity + score_is_probability false), F22-4b-v (math.isfinite). Evidence: CTest
18/18, T19 39/39, T22 51/51, contract 36/36, mock --check 0 failures.


### [2026-10-08 06:30 UTC] T17/T19 -> DONE (Lead; Agent-D PASS + Lead verified)
Note: verified 51/51 fixtures, 246 models, mock --check 0 failures, tag api-v1.0
@ ada0e9f. All findings closed. @agent-c: stand by for T13 (harness first; real-data
PASS gated on E05).

### [2026-10-08 06:25 UTC] RESUMED after session restart
Note: re-attached; T17/T19 re-audited PASS by Agent-D. Zone green at HEAD. Now
T17/T19 = DONE (Lead 06:30). Standing by for T13 (harness first), per Lead.


### [2026-10-08 06:55 UTC] T13 CLAIMED -> REVIEW (real-host slice)
Harness-first prerequisite met (T24 PASS). Slice (a) landed test-only, additive:
`tests/integration/test_e2e_real_host_t13.py` drives the **real binary**
`build/aura_backend_host` over a real loopback socket, validates all 15 frozen
routes via the shared T23 `contract_checker`, asserts RULE C/E06/E07 on
/analysis/latest, and asserts the honest DEGRADED/shadow_only/not-ready posture.
**37/37 checks, 0 failed** — closes F24-1's real-host gap (T24 only drove the mock).
Slices (b) resource staging and (c) real-data runbook remain E05-gated/Lead-ruled;
no production `src/` touched. Submitted to Agent-D for audit.


### [2026-10-08 07:35 UTC] T25/T28 — Stage 1 of my assigned slice DONE -> REVIEW
Claimed T28 (my board row). Landed the configurable data-path surface, stdlib-only,
in my zone:
- `scripts/data_paths.py` — canonical resolver. Env vars: `AURA_DATA_ROOT`
  (default `<repo>/research/data/xauusd_m1`), `AURA_FEATURES_DIR` (default
  `<repo>/research/features_real`). Defaults are repo-relative (computed from
  `__file__`), never CWD; override precedence arg > env > default; provenance
  tagged; helpers for the Lead's corpus layout (`<year>.csv`, `ask/`, `sample/`,
  `checksums.sha256`, `metadata.json`, `QUALITY.md`); `REAL_YEARS=2021..2025`.
- `tests/integration/test_data_paths_t28.py` — **17/17**, offline, CWD-independent.
No production `src/` touched; frozen API v1 untouched. Consumers: T26/T27.


### [2026-10-08 10:10 UTC] T13 (replay transcript) -> REVIEW
Note: Lead 10:10 directive item delivered. scripts/t13_replay_transcript.py drives the real host over the POC corpus via the bridge replay path; full transcript at research/reports/t13_realdata.md (replay->features->model->API, 12 routes, RULE C gate closed, 9/9 TF decision-grade, SHORT/REDUCED risk proposal). POC label; no src/ touched; 0 strays. Submitted for Agent-D audit.
