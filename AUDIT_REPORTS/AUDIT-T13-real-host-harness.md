# Audit Report — T13 slice (a): real-host end-to-end harness

- **Auditor:** Agent-D (Verification & Audit)
- **Owner:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-08 07:05 UTC
- **Repo HEAD:** 3639e81
- **Verdict:** **PASS (with a contract-integrity caveat).**
  `tests/integration/test_e2e_real_host_t13.py` genuinely drives the production
  binary over a real socket and correctly reports the DEGRADED posture. It closes
  F24-1's mock-vs-host gap. Its "matches frozen schema" checks, however, do not
  catch the freeze drift documented in
  `AUDIT-CONTRACT-drift-host-vs-schema.md`.

---

## Claim

Agent-C, T13 slice (a): a real-host e2e harness that drives
`build/aura_backend_host` over loopback, validates every frozen route via the
shared `contract_checker`, asserts RULE C/E06/E07 on `/analysis/latest`, and
asserts the host is honest about DEGRADED/shadow_only/not-ready. 37/37.

## Verification

```
$ python3 tests/integration/test_e2e_real_host_t13.py   -> 37/37 checks passed
```
Reproduced at 3639e81. Cross-checked same tree: T24 88/88 (mock e2e),
T16 36/36 (real host), fixtures 51/51, mock 39/39.

- **Real, not replay — YES.** Launches the binary (`subprocess.Popen`, no
  `--once`), waits, then issues real HTTP requests. Skips cleanly (exit 0) when
  the binary is absent.
- **Honest posture — YES.** Asserts `mode == DEGRADED`, `shadow_only is True`,
  `ready is False` when no bridge is staged (matches my independent read-only run:
  `app root …/build`, `bridge script not found`, calibration audit absent).
- **RULE C/E06/E07 — YES.** `probability null`, `probability_calibrated false`,
  `score_is_probability false`, semantic checker clean.

## Caveat

The "matches frozen schema (real host)" checks pass, but because the schema (and
both validators) only visit *declared* keys, they do not detect that the real host
emits many undeclared fields (8/15 routes). See
`AUDIT-CONTRACT-drift-host-vs-schema.md`. The harness itself is correct; it is the
check's coverage that is incomplete — it proves declared-shape conformance, not
exact-shape conformance.

## Result

**PASS.** Slice (a) is a genuine, honest real-host e2e and the right complement to
T24. Recommend accepting it; consider adding the host-keys ⊆ schema-keys assertion
recommended in the drift report.

## Independence

Agent-D authored none of the harness; `./build/aura_backend_host` run read-only;
no source touched.
