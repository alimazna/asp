# Audit Report — T24 (T13 end-to-end harness vs frozen v1)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner:** Agent-A (Features & Analytics)
- **Date:** 2026-10-08 06:50 UTC
- **Repo HEAD at audit:** 176f5d7
- **Verdict:** **PASS.** The harness drives the frozen v1 contract end to end over
  loopback HTTP, has real teeth, and states its synthetic-only limitation
  honestly. One non-blocking observation (F24-1: it drives the mock, not the
  production host).

---

## Claim

Agent-A, T24: `tests/integration/test_e2e_frozen_v1.py` — a T13 end-to-end harness
that drives the canonical synthetic backend (`scripts/mock_api.py`) over loopback
and checks the whole frozen surface: every route 200 + schema-valid via Agent-B's
shared `contract_checker`, RULE C/E06/E07 on `/analysis/latest`, fixture↔live
structural consistency, semantic-fixture teeth, invalid-fixture rejection, error
shape (fixtures + live 404/405), query handling, and the calibrated branch. 88/88.

## Verification steps

```
$ python3 tests/integration/test_e2e_frozen_v1.py   -> 88 checks, 0 failed, RESULT: PASS
```
Reproduced exactly at 176f5d7. Cross-checked at the same tree:
```
$ python3 tests/integration/test_api_fixtures.py    -> 51/51
$ python3 tests/integration/test_contract_t16.py    -> 36/36 (real host)
$ python3 tests/integration/test_mock_api_t19.py    -> 39/39
$ PYTHONPATH=. unittest discover -s tests/models    -> 248 OK
$ ctest                                             -> 18/18
```

### Lead's three audit questions

1. **Does it really drive the frozen contract end-to-end (host/mock), not just
   replay fixtures?** — **Yes (mock path).** `Server` spawns
   `scripts/mock_api.py` on a free port, waits for it, and every check issues a
   real HTTP request (`urllib`) to `127.0.0.1`; responses are validated against
   `API_V1_SCHEMA.json` through `contract_checker.validate_envelope` — a *live*
   read, not fixture replay. Fixtures are used only as a *consistency reference*
   in step 3, which is the correct way to bind the T22 corpus to the live
   backend. See F24-1 re the production binary.
2. **Fixture-corpus / calibrated-branch meaningful?** — **Yes.** Step 3 compares
   each `valid/` fixture's key-path skeleton to the live payload of its route
   (all 15 routes; full-list equality for `/timeframes`); step 4 proves the
   `semantic/` fixtures are schema-valid yet rejected by the shared checker
   (teeth); step 5 proves `invalid/` stay rejected; step 8 exercises the
   calibrated render branch and asserts `probability` populated +
   `probability_calibrated:true` + frozen-nulls still clean.
3. **Synthetic-only limitation stated honestly?** — **Yes.** The docstring states
   "The real-data PASS is gated on E05 … this harness exercises the synthetic/mock
   path fully, exactly as the Lead's cycle 29 ruling allows," and the checks are
   labelled `e2e` (synthetic) rather than evidential. No overclaim.

### Independent teeth check on the structural comparator
`keypaths()` — the function that binds fixture to live shape — detects all three
drift classes (probed inline):
```
  identical trees            -> equal
  extra key in live          -> detected
  missing key in live        -> detected
  object retyped to string   -> detected
```
So step 3 is not vacuous.

## Findings

### F24-1 — non-blocking: T24 drives the mock, not the production host
`MOCK = scripts/mock_api.py`; `Server` never launches `build/aura_backend_host`.
The production binary's schema conformance is covered by Agent-C's
`tests/integration/test_contract_t16.py` (real socket + real host, 36/36 with an
empty `KNOWN_DEFECTS` allow-list), so the surface is covered — but by a *separate*
script. If a future cycle changes the host without changing the mock, T24 stays
green. Recommendation (optional): add a one-line host/mock note to the harness
docstring pointing at `test_contract_t16.py` so readers do not mistake T24 for the
host test. Not blocking; T24's title is scoped to the synthetic path by its own
docstring.

### F24-2 — informational: integration tests are not registered with ctest
`CMakeLists.txt` registers only the C++ tests; the `tests/integration/*.py` scripts
(T16, T19, T24, fixtures) are run manually. This matches the existing repo pattern
and is how the Lead verified them, so it is not a defect — noting it so no one
assumes `ctest` alone covers the contract tests.

## Result

**PASS.** T24 genuinely exercises the frozen v1 contract end-to-end on the
synthetic path, with anti-vacuity teeth on both the semantic layer and the
structural comparator, and an honest E05 caveat. 88/88 reproduced. Recommend
T24 → DONE. F24-1/F24-2 are documentation/coverage notes, not blockers.

## Notes

- **Independence.** Agent-D authored none of the harness; `keypaths` and the run
  were reproduced from 176f5d7; no source edited by Agent-D.
- **Mission state.** With T24 the only remaining backend deliverable, and E05
  (real XAUUSD data) the sole blocker to an evidential PASS, this is consistent
  with the Lead's cycle-30 statement.
