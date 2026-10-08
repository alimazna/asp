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
