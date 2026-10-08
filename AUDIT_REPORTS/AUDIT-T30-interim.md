# Audit Report — T30 (interim): contract shape drift — host side CLOSED, mock side pending

- **Auditor/reviewer:** Agent-D (T30 reviewer)
- **Owners:** Lead (schema), Agent-C (mock alignment + teeth)
- **Date:** 2026-10-08 08:15 UTC
- **Repo HEAD:** 9a59676
- **Verdict:** **INTERIM — not yet a final PASS.** The class that motivated T30
  ("drift the suites miss") **is genuinely closed for the real host** by a test that
  is red before / green after (independently reproduced). Mock-side alignment and the
  `required` promotion are still pending, so T30 is not DONE.

---

## Independently verified (red-before → green-after, host)

Using Agent-C's `scripts/schema_shape.py` (`undeclared()`), full response bodies,
against the real host (DEGRADED):

```
OLD schema (3d01b98^) -> routes w/ undeclared host keys: 8
   timeframes 6, snapshot 9, context/latest 5, bridge/status 13,
   risk/latest 3, research/status 4, governance/status 3, audit/recent 3
NEW schema (3d01b98)  -> routes w/ undeclared host keys: 0
```

So `test_e2e_real_host_t13.py` now carries a check (`host-keys ⊆ schema-keys`) that
**would have been red before T30(a) and is green after** — exactly the cycle-32
requirement. 52/52 on the real-host harness, reproduced.

## Verified correct

- **Schema extension is genuinely additive.** No existing field was removed, renamed,
  or retyped. Changes: one `$comment` note; object `quality`/`context` gained
  `required`+`properties`; new optional properties added (bridge 13, risk proposal
  trio, research counts, governance pending/history, audit records, shadow/outcome
  and analysis/history element shapes). `api-v1.0` tag unchanged. JSON valid.
- **`mock_api.py --check` still green** (0 failures) — the additive extension does
  not break the mock validator.
- **Item 4 done:** `scripts/host_key_dump.py` now derives `REPO` from `__file__`.
- **My state-dependence note folded in:** the Lead read the element shapes from
  `BackendFacade.cpp`, not the DEGRADED dump.

## Residuals (why not final PASS)

### T30-R1 — mock still red on one field (known, pending schema)
`test_mock_shape_t30.py`: **15/16** — `timeframes[].freshness.last_update` is emitted
by the mock but not declared (schema `freshness` is still a bare `{"type":["object","null"]}`).
Agent-C asked the Lead to declare the host's non-null freshness sub-fields
(`state, is_fresh, last_update, age_millis, max_age_millis`, `BackendFacade.cpp:26-33`).
Until then the mock-shape test is red by design. **Confirmed:** the mock emits
`last_update`; the host's non-null freshness carries 5 fields hidden by the DEGRADED
posture (the state-dependence class again).

### T30-R2 — the teeth catch EXTRA keys, not MISSING declared ones
Both checks assert `payload-keys ⊆ schema-keys` only. The newly-declared host fields
(bridge 13, risk proposal*, research experiments/failures, governance history/pending,
audit records) were **not** added to `data_required`, so:
- the **host**-shape test would stay green even if the host *dropped* a declared field;
- the **mock**-shape test would stay green even though the mock does **not emit** the
  13 bridge fields, the risk proposal trio, or the research/governance/audit arrays
  (verified: mock emits the old minimal set).

Agent-C explicitly deferred this (promote to `data_required` **after** the mock emits
them) and asked for the Lead's go. My recommendation: **promote the always-present
host fields to `data_required` once the mock emits them**, so the mock cannot silently
under-serve a frozen surface. Without it, the *mock* drift class is declared-but-not-enforced.
Note: the new nested `required` arrays (7) are enforced only by the *structural*
validator, which the shape helper does not run — the shape helper checks the leaf set.

### T30-R3 — `quality` and other nested `required` are not exercised by the shape test
The shape helper checks emitted keys, not required-presence; the structural validator
(`mock_api.validate_envelope` / `contract_checker`) does enforce nested `required`.
Both are green today, so this is a coverage note, not a defect: a payload that *omits*
a nested-required key would (and does) get caught by the structural layer, e.g. the
history entry.

## Conclusion

Recommend T30 remain **REVIEW/ACTIVE** until: (a) the Lead declares `freshness`
sub-fields; (b) the mock is aligned to the declared sets; (c) the mock-shape test is
green (16/16) and, ideally, the always-present fields are promoted so the mock cannot
under-serve. When those land, I will re-audit and can sign T30 DONE with the red→green
evidence above as the drift-closure artifact.

## Independence

Agent-D authored none of T30; `schema_shape.undeclared()` invoked read-only; schema
diff and mock `--check` reproduced at 9a59676. No source touched.

---

# Addendum A — T30(b) re-audit at e2cc9d7: mock teeth GREEN, but the frozen set is RED

- **Repo HEAD:** e2cc9d7
- **Verdict:** **NOT DONE.** The T30(b) teeth are correct and genuinely two-sided,
  but the same commit promoted `data_required` and aligned the mock **without
  refreshing the valid fixtures**, so the previously-green frozen suites now fail.

## Verified good (teeth)

- `test_mock_shape_t30.py` **19/19**; `test_e2e_real_host_t13.py` **52/52**;
  `mock_api.py --check` 0 failures; schema JSON valid.
- **Two-sided red→green, independently reproduced:**
  - Extra keys: with the **old** schema the host emitted undeclared keys on 8
    routes → **0** with the new schema (the host harness had no such check before).
  - Missing-required: the **old** mock (a78fb1e) against the new schema is missing
    required keys on 5 routes (bridge 13, risk 3, research 4, governance 3, audit 3);
    the aligned mock emits them → **0**.
- **`data_required` promotion is real:** bridge/status now lists 20 required keys;
  risk/research/governance/audit likewise; `freshness` declares all 5 sub-fields.
- Nested `freshness` shape read from the emitter (not the DEGRADED dump).

## RE-GRESSION — frozen fixtures not yet refreshed (CURRENT RED)

```
test_api_fixtures     52 checks, 5 FAILED   -> RESULT: FAIL
test_e2e_frozen_v1    88 checks, 6 FAILED   -> RESULT: FAIL
```

- `valid/bridge_status.json` missing required `managed_by_application` (+12 others)
  → schema conformance + T24 structural-vs-live both fail.
- `valid/risk_latest.json` missing `proposal_available` (+`proposal`,`proposal_reason`).
- `valid/research_status.json` missing `experiment_count` (+`experiments`,`failure_count`,`failures`).
- `valid/governance_status.json` missing `pending_count` (+`pending`,`history`).
- `valid/audit_recent.json` missing `audit_stream_size` (+`audit_records`,`active_incidents`).
- `valid/timeframes.json` — T24 structural mismatch (mock `freshness` now emits 5
  fields incl. `state/is_fresh/age_millis/max_age_millis`; the fixture has the old shape).

Root cause: the promotion makes these fields **required**, so every fixture carrying
the old minimal shape is now structurally non-conformant. Expected consequence of
promotion, but the frozen set is transiently broken until Agent-A's refresh lands.

## Path to green

Refresh `valid/{bridge_status,risk_latest,research_status,governance_status,
audit_recent}.json` **and** `valid/timeframes.json` (freshness 5-field shape) from the
canonical mock (`build_payloads`) — the same regen pattern as the F-HIST-1 fix. Then
T24/fixtures return green. I will re-run the full suite and can sign T30 DONE when:
mock-shape 19/19 · host 52/52 · fixtures 52+ · T24 88 · T13 37 · models green · ctest 18.

## Note for the Lead

T30 runs in parallel with the T25→T26→T27 critical path, so this red is not on the
data path — but the **main branch's frozen suites are red right now** (e2cc9d7). Any
external consumer pulling `main` expecting green will see FAIL. Worth prioritising the
small fixture refresh to restore a green freeze.

## Independence

Agent-D authored none of T30; all checks reproduced read-only at e2cc9d7 (old mock
loaded from a78fb1e for the red-before probe). No source touched.
