# Audit Report — Frozen-null (RULE C) violation in `analysis_history.json`, and semantic-checker scope gap

- **Auditor:** Agent-D (Verification & Audit)
- **Date:** 2026-10-08 07:20 UTC
- **Repo HEAD:** 15c1c77
- **Severity:** **material** — a shipped valid fixture violates the "unavailable is
  not zero" / frozen-null contract, and no suite catches it.
- **Zones:** fixtures (T22, Agent-A), semantic checker scope (T23, Agent-B),
  schema (Lead). Agent-D reports only; makes no change.

---

## Finding

`tests/fixtures/api_v1/valid/analysis_history.json`, `data[0].signal.model_version`
= `"logistic-t03"`.

`BACKEND_FRONTEND_API_V1.md` ("Unavailable is not zero") states `signal.model_version`
is **`null` until the decision model/calibration freeze** it — it is in the
unconditional frozen-null set. The T22 fixture-side checker's own docstring lists
it as such ("unconditional frozen nulls — null this release regardless of
calibration (horizon, confidence_lo/hi, **model_version**, …)").

Independent probe (replicating the fixture-side invariant function):
```
history[0] invariant violations: ['signal.model_version non-null']
mock history model_version:     None     (correct — frozen null)
fixture history model_version:  logistic-t03   (violates)
```

So the fixture **violates the frozen-null contract**, and it is **stale against the
mock** (the mock's `analysis_obj` emits `model_version: None`; the fixture is a
hand-kept copy that drifted).

## Why every suite is green

1. **Fixture-side checker (`test_api_fixtures.py`) is scoped to analysis/latest.**
   `invariant_violations` is applied to `valid/analysis_latest.json`,
   the calibrated fixture, and the two `semantic/*` fixtures — **not** to
   `analysis_history.json`. The fixture's history entry is never run through it
   (its 1 line: `history[0] invariant violations: ['signal.model_version non-null']`).
2. **Schema cannot catch it.** `GET /api/v1/analysis/history` declares
   `element_required` (`timestamp, symbol, context, signal, levels, meta`) but **no
   `element_properties`**, so the schema validates neither types nor frozen nulls
   of history entries — and `"logistic-t03"` would be a valid string anyway.
3. **T23 semantic layer is `analysis/latest`-only.** `contract_checker.
   analysis_contract_violations` defaults to `ANALYSIS_ENDPOINT`
   (`GET /api/v1/analysis/latest`); `frozen_violations` is documented as
   "`analysis/latest` is the only surface with frozen nulls," so history is not
   checked.
4. **T24/T13 key-path equality is structural, not value-level,** so a populated
   `model_version` compares as a matching key path.

Net: a frozen-null/RULE-C-relevant violation sits in a **valid** fixture, and the
whole green suite hides it.

## Recommendation

- **Fixture (Agent-A, T22 zone):** set `history[0].signal.model_version` to `null`
  to match the contract and the mock; re-check the other history entries (there is
  one entry today).
- **Checker scope (Agent-B, T23 zone):** extend the semantic enforcement to
  `analysis/history` entries (the frozen-null set applies per-entry), so a
  populated `model_version` there fails. `frozen_violations` already takes a `data`
  object; running it per history element is small.
- **Schema (Lead):** declare `element_properties` for `analysis/history` so types
  (and the `context/signal/levels/meta` sub-objects) are not a blind spot — this
  overlaps the freeze-drift report (`AUDIT-CONTRACT-drift-host-vs-schema.md`).

## Verification notes

Reproduced at 15c1c77 inline (uncommitted): read the fixture, ran the fixture-side
invariant function, and compared to `mock_api.build_payloads(...)["GET
/api/v1/analysis/history"]`. No source touched by Agent-D.

---

# Addendum A — F-HIST-1 re-audit (FIXED)

- **Auditor:** Agent-D
- **Date:** 2026-10-08 07:25 UTC
- **Repo HEAD:** 3b2b35e
- **Verdict:** **FIXED.** Both halves closed and independently verified.

## Verified

- **Fixture (Agent-A, 9051452):** `valid/analysis_history.json` regenerated —
  `signal.model_version` is now `null`, `features_contributing` emptied, `context`
  normalised to the canonical mock entry, `meta.disclaimer` corrected. Matches
  `scripts/mock_api.build_payloads(...)`.
- **Guard (Agent-A, 9051452):** `test_api_fixtures.py` now runs
  `invariant_violations` on **every** history entry (52/52).
- **Checker scope (Agent-B, 3b2b35e):** `contract_checker.history_violations()` +
  `analysis_contract_violations(endpoint=HISTORY_ENDPOINT)` enforce the frozen-null
  set **per entry**, with entry-indexed messages. 252 model tests green.

## Independent teeth probe (not just a green run)

```
teeth (populated model_version):
  ['invariant: data[0]: signal.model_version is non-null (frozen null this release)']
fixture history clean:  clean
```
The extension is not vacuous — a populated `model_version` in a history entry is
now rejected, and the corrected fixture passes.

## Full suite at 3b2b35e

fixtures 52/52 · t16 36/36 · mock 39/39 · e2e mock(T24) 88/88 · e2e real-host(T13)
37/37 · models 252 OK · ctest 18/18.

## Residual

- The **shape drift** (`AUDIT-CONTRACT-drift-host-vs-schema.md`, 8/15 host routes +
  mock-side) is **not** closed by this fix and was not explicitly ruled on in
  cycle 31 — it remains open. `analysis/history` still declares no
  `element_properties`, so history *types* remain a blind spot even though the
  **frozen-null values** are now enforced by the checker. Recommend keeping the
  drift item open pending the Lead's authority ruling.
