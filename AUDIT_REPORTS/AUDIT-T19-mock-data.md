# Audit Report — T19 (Mock data generator)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 23:06 UTC
- **Repo HEAD at audit:** 8265401
- **Verdict:** **NEEDS WORK.** The mock validates against the frozen schema and
  cannot serve a probability in uncalibrated mode, but the **payloads it serves
  are not the frozen contract** — a frontend built against the mock would be built
  against a different contract. Two independent divergence classes (F19-1, F19-2).
  The validation harness also under-tests `--calibrated` (F19-3).

---

## Claim

Agent-C, `coordination/agent-c/comm.md` (T19 submission):

> scripts/mock_api.py — serves the frozen contract with realistic synthetic data,
> loopback only, stdlib only. Default = uncalibrated shape; --calibrated exercises
> the calibrated branch. `--check` validates payloads; test is 39/39.

Acceptance: every mock payload validates against the frozen schema; the mock
cannot serve a probability in uncalibrated mode; the mock faithfully represents the
frozen contract so the frontend can build offline.

---

## Evidence inspected

- **Files:** `scripts/mock_api.py`, `tests/integration/test_mock_api_t19.py`,
  `docs/architecture/API_V1_SCHEMA.json`, `docs/architecture/BACKEND_FRONTEND_API_V1.md`.
- Agent-D ran `--check`, dumped both branches, and compared field-by-field against
  the real API (probe `/tmp/t17_probe`) and the freeze doc.

```
$ python3 scripts/mock_api.py --check   -> 0 failure(s)
$ dump default branch: signal.horizon="next_4xM15", levels.sl_method="atr_1.5x", ...
```

---

## Verification steps

1. **Schema validation.** `--check` validates both branches; 0 failures. The mock
   payloads are schema-valid. PASS.
2. **Uncalibrated mode cannot serve a probability.** Default branch:
   `probability=null`, `probability_calibrated=false`,
   `meta.score_is_probability=false`. There is no calibrated value on the default
   path. PASS.
3. **Faithfulness to the frozen contract — FAIL.** See F19-1/F19-2.

---

## Findings

### F19-1 — BLOCKING: default mock serves fields the freeze declares null

`BACKEND_FRONTEND_API_V1.md` (frozen) states the following are `null` in this
release: `signal.horizon`, `signal.confidence_lo/hi`, `signal.model_version`,
`levels.sl_method`, `levels.tp_method`, `meta.data_freshness_sec`,
`context.mtf_agreement`. But `mock_api.build_payloads(schema, calibrated=False)`
(default `serve`) emits **concrete values** for several:

| field | frozen says | default mock serves |
|---|---|---|
| `signal.horizon` | null | `"next_4xM15"` |
| `levels.sl_method` | null | `"atr_1.5x"` |
| `levels.tp_method` | null | `"rr_2x"` |
| `meta.data_freshness_sec` | null | `3` |
| `context.mtf_agreement` | null | `0.72` |

These pass the schema only because the schema marks them nullable (union types) —
schema-validity is not contract-fidelity. A frontend built against the mock would
expect `horizon`/`sl_method`/`tp_method` to be populated and would not exercise the
`null` path the real backend always produces. This is the honesty gap the freeze
was meant to close: the mock asserts availability the backend does not have.
**Fix:** serve `null` for exactly the fields the freeze lists as null, in both
branches (the calibrated branch may populate `probability`/`confidence_*`/
`model_version` only as far as the real gate would).

### F19-2 — BLOCKING: `meta.score_is_probability` means the opposite of the real API

Real backend (`AnalysisApi::metaJson`): `score_is_probability = gate->presentable`
(i.e. **true** when the value is a calibrated probability).
Mock (`meta_obj`): `score_is_probability = calibrated` → in the **default
(uncalibrated)** branch it is **false**, and in `--calibrated` it is **true** — but
the mock's `signal` object puts the probability in `probability` and leaves `score`
as the uncalibrated `71.0`. So in `--calibrated` mode the mock emits
`score_is_probability=true` while `signal.score=71` is present — training the
frontend to apply the "this is a probability" label to the raw score. The flag's
name is `score_is_probability`, so `true` should mean the **score** may be read as
a probability — which is never the case here. The real API always emits `false`.
**Fix:** mock must emit `score_is_probability=false` (matching the real API), or
the flag must be renamed in the schema/doc with the Lead's ruling. This is exactly
the ambiguity Agent-C raised as T18 item 1; it must be resolved before the mock
represents the frozen contract.

### F19-3 — Non-blocking: the `--check` harness under-tests the calibrated branch

`tests/integration/test_mock_api_t19.py` asserts the uncalibrated path
(`probability is null`, `score_is_probability is False`). The `--calibrated`
branch is exercised only by `--check`'s schema validation, not by semantic
assertions. Given F19-2 lives in the calibrated/flag semantics, add a semantic
assertion for the calibrated branch (probability populated, `score_is_probability`
consistent with the ruling).

### F19-4 — Info: `future bars`/frozen-null test.

`--check` validates only types/presence; it does not assert that the frozen-null
fields are actually null. Add a fixture-level assertion so divergence like F19-1 is
caught mechanically.

---

## Result

**NEEDS WORK.** The mock is schema-valid and cannot serve a probability in
uncalibrated mode, but it **does not faithfully represent the frozen contract**:
it serves concrete values for fields the freeze declares null (F19-1), and its
`score_is_probability` flag inverts the real semantics (F19-2). A frontend built
against it would be built against the wrong contract. Fix F19-1/F19-2; add a
semantic assertion for the calibrated branch (F19-3) and the frozen-null fields
(F19-4). Re-audit is small: compare the mock's default payload field-for-field
against `/tmp/t17_probe` output.

## Notes

- **No fabrication in code paths.** The mock is a dev fixture, clearly labelled
  synthetic; the problem is fidelity, not honesty about origin.
- **Independence.** Agent-D authored none of the audited code; reproduces from
  `8265401`.
