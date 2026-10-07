# API v1 fixtures (T22)

Canonical request/response payloads for the frozen ASTRA frontend contract.

**Source of truth:** every fixture here is *derived from*
`docs/architecture/API_V1_SCHEMA.json`. A fixture that disagrees with the schema
is a bug in the fixture, not in the schema. `tests/integration/test_api_fixtures.py`
enforces that: each `valid/` payload must pass the schema validator and each
`invalid/` payload must be rejected.

## Layout

| Dir | Meaning | Expected check |
|-----|---------|----------------|
| `valid/` | Schema-conformant, enveloped payloads, one per frozen endpoint | pass envelope + data validation |
| `invalid/` | One deliberate *structural* defect each (missing key, wrong `const`, out-of-range value, bare object) | fail validation |
| `semantic/` | Structurally schema-valid but violates a contract invariant the schema cannot express | pass validation **and** trip an invariant |
| `errors/` | Flat error bodies `{error, code, message}` for 503 / 404 / 405 | match `error_schema` |

## Envelope

Every success body is `{"api": "v1", "schema": "1.0", "data": ...}`. A bare
object is not a response.

## Frozen-null contract (v1, uncalibrated)

`valid/analysis_latest.json` is the **default** shape the backend actually
emits while no calibrated model is live (E05):

- `signal.probability` is `null`, `signal.probability_calibrated` is `false`,
  `signal.score` is **present**;
- `signal.horizon`, `signal.confidence_lo`, `signal.confidence_hi`,
  `signal.model_version` are `null`, and `signal.features_contributing` is `[]`
  (F22-1 — the pipeline sources no model version or contributions this release;
  `BACKEND_FRONTEND_API_V1.md` lists `model_version` among the frozen nulls);
- `levels.*` (`entry`, `stop_loss`, `take_profit`, `reward_risk`,
  `suggested_risk_pct`, `sl_method`, `tp_method`) are all `null`;
- `meta.score_is_probability` is `false` (E07 — the surfaced value is a raw
  score, never a probability);
- `context.mtf_agreement`, `meta.data_freshness_sec`, `data.timestamp` are `null`.

Live (not frozen) fields: `symbol`, `timestamp`, `meta.degraded` are real
runtime state, not frozen nulls — the impl-vs-schema check must compare them
loosely (F22-3).

`valid/analysis_latest_calibrated.json` shows the calibrated branch: `probability`
in `[0,1]`, `probability_calibrated` `true`. `meta.score_is_probability` stays
`false` in both branches; use `signal.probability_calibrated` as the single
source of truth for probability-vs-score.

## Why `semantic/` is separate

The schema types `score_is_probability` as `boolean` and `levels.*` as
`["number","null"]`. It cannot express "must be `false`/`null` when
`probability` is `null`". Encoding those as `invalid/` would falsely claim the
schema validator catches them; keeping them in `semantic/` with an explicit
invariant check is the honest split.

`test_api_fixtures.py:invariant_violations` implements the **full** E06 invariant
set: while `probability` is null, `horizon`, `confidence_lo`, `confidence_hi`,
`model_version`, every `levels.*`, `meta.data_freshness_sec`, and
`context.mtf_agreement` must be null and `score_is_probability` false; a non-null
`probability` requires `probability_calibrated:true`. Agent-C's F17-1 check
should import this helper rather than re-implement it (F22-2).

## Consumers

- Agent-C F17-1 impl-vs-schema check: validate real `BackendFacade` output
  against the schema and, for the uncalibrated shape, against the invariants in
  `test_api_fixtures.py`.
- T13 end-to-end integration: use these payloads instead of hand-written bodies.
- T18 frontend handoff: these are the concrete shapes the frontend must render,
  including the null path.
