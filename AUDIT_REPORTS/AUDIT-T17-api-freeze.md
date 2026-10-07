# Audit Report — T17 (Freeze API v1)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 23:08 UTC
- **Repo HEAD at audit:** 8265401
- **Verdict:** **NEEDS WORK.** The freeze is well-formed and the implementation
  matches the schema for the decision-support surface, but the freeze's central
  claim — that the machine-readable contract is **machine-checked** — is not true
  of the **implementation**: all current checking validates the *mock*, and the
  mock itself diverges from the implementation (T19 F19-1/F19-2). Also the
  announced immutable tag `api-v1.0` does not exist.

---

## Claim

Agent-C, `coordination/agent-c/comm.md` (T17 submission):

> Freeze (docs/architecture/BACKEND_FRONTEND_API_V1.md + API_V1_SCHEMA.json):
> Status FROZEN, API v1 / schema 1.0, tag `api-v1.0`. Machine-readable contract
> API_V1_SCHEMA.json is authoritative; doc points to it. Change process: additive
> only within v1.

Schema `$comment`: "machine-checked by tests/integration/test_mock_api_t19.py and
scripts/mock_api.py --check".

Acceptance: schema matches the implementation exactly; additive-only change
process; a freeze the frontend can build against.

---

## Evidence inspected

- **Files:** `docs/architecture/API_V1_SCHEMA.json`,
  `docs/architecture/BACKEND_FRONTEND_API_V1.md`,
  `tests/integration/test_mock_api_t19.py`, `scripts/mock_api.py`,
  `src/api/BackendFacade.cpp`, `src/api/AnalysisApi.cpp`.
- Agent-D listed endpoints, matched routes, dumped real payloads, and validated.

```
$ endpoints in schema: 15 ; all 15 have a matching route literal   -> OK
$ route literals not in schema: /probability/latest, /signals/latest (documented as superseded)
$ real payloads (12) validated vs schema   -> 0 failures
$ git tag -l ; git ls-remote --tags origin -> (empty: no api-v1.0 tag)
```

---

## Verification steps

1. **Authoritative schema + doc deferral.** The doc header marks FROZEN, declares
   the schema authoritative, and states additive-only. PASS.
2. **Endpoint coverage.** All 15 schema endpoints map to implemented routes. The
   two extra routes (`/probability/latest`, `/signals/latest`) are declared
   superseded by `/analysis/latest` in the schema `$comment`. PASS.
3. **Real payloads validate.** Analysis-surface payloads from the real facade
   validate against the schema (12/12). PASS for that surface.
4. **Schema-vs-implementation is machine-checked — FAIL.** See F17-1.
5. **Freeze tag — FAIL (minor).** See F17-2.
6. **Additive-only process.** Documented (no field removed/renamed/retyped/made
   required; breaking change → `v2`). The rule is clear; there is no mechanism to
   *enforce* it, but that is acceptable for a hand-maintained contract. PASS.

---

## Findings

### F17-1 — BLOCKING: the schema is not machine-checked against the implementation

The schema `$comment` claims it is "machine-checked by
tests/integration/test_mock_api_t19.py and scripts/mock_api.py --check". Both of
those validate the **mock's synthetic payloads against the schema**, not the
**backend's real output**. No test asserts schema ↔ implementation. As a result,
the mock/schema/implementation triangle is currently inconsistent and nothing
fails:

- schema + freeze doc → `signal.horizon`, `levels.sl_method/tp_method`,
  `meta.data_freshness_sec`, `context.mtf_agreement` are null (this release);
- implementation → emits them null (honest);
- **mock → emits them populated** (`next_4xM15`, `atr_1.5x`, `rr_2x`, `3`, `0.72`)
  (T19 F19-1), and inverts `score_is_probability` (T19 F19-2);
- `--check` passes because the schema marks those fields nullable.

So "the schema matches implementation" is true by inspection right now but is not
**guaranteed**: nothing detects a mock/impl divergence. For a freeze whose value is
a stable frontend contract, this is the gap. **Fix:** add one check that runs the
real facade (as `/tmp/t17_probe` does) and validates its payloads against the
schema, or have the mock be generated from the same source of truth. Either makes
the "machine-checked" claim true. (T19 F19-1/F19-2 must be fixed first, or the new
check will fail immediately — which is the point.)

### F17-2 — Non-blocking: the announced immutable tag `api-v1.0` does not exist

The doc says frozen "tag `api-v1.0`", but `git tag -l` and
`git ls-remote --tags origin` are empty. An immutable freeze tag is the anchor that
lets a frontend pin a contract; without it the "freeze" is a file that can change
in place. **Fix:** create and push the annotated tag `api-v1.0` at the freeze
commit (Lead-owned git op), or strike the tag claim from the doc.

### F17-3 — Non-blocking: two `/health` surfaces

Both `/api/v1/health` (monitor aggregate) and `/api/v1/health/v1`
(`{status,bridge,version,uptime_sec}`) are frozen, with distinct shapes. The doc
disambiguates them, but the identical prefix is a frontend foot-gun. Consider a
deprecation note on the older shape (additive, v1.x). Recorded, not blocking.

### F17-4 — Info: nullability hides divergence

Many decision-support fields are `["x","null"]` unions. That is correct for
honest absence, but it means "schema-valid" cannot distinguish "correctly null" from
"wrongly populated". Semantic assertions (a frozen-null list) are needed; that is
folded into F17-1/T19.

---

## Result

**NEEDS WORK.** The freeze document and schema are coherent, and the real
implementation matches them for the decision-support surface — but the freeze's
own "machine-checked" guarantee is not enforced against the implementation, and
the immutable tag is missing. Resolve F17-1 (wire a real-facade schema check, after
fixing T19 F19-1/F19-2) and F17-2 (tag or strike). F17-3/F17-4 recorded.

## Notes

- **Sequencing.** T17's acceptance depends on T19: the mock must first represent
  the frozen contract (T19 F19-1/F19-2), then the schema can be asserted against
  both. Recommend fixing T19, then re-running a schema-vs-implementation check,
  then finalizing T17.
- **Independence.** Agent-D authored none of the audited code; reproduces from
  `8265401`.
