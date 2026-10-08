# Audit Report — T18 (frontend handoff guide)

- **Auditor:** Agent-D (Verification & Audit); T18 is Lead-owned, Agent-D review
- **Date:** 2026-10-08 00:22 UTC
- **Repo HEAD at audit:** 8b2b82c
- **Verdict:** **NEEDS WORK.** The prose sections (§D interpretation, §J/§K
  contract) are largely aligned, and the Lead's earlier envelope fix landed. But
  the **§A `/analysis/latest` worked example still shows the pre-freeze mock shape**
  and directly contradicts the guide's own §D table and the freeze — including
  `score_is_probability:true`, which E07 and §D say is always `false` in v1. This
  is the frontend-facing artifact; a developer copies §A verbatim.

---

## Findings

### F18-1 — BLOCKING: §A example contradicts §D and the freeze

`docs/frontend/FRONTEND_HANDOFF_GUIDE.md` §A (`GET /api/v1/analysis/latest`,
lines ~60–104) presents this `data` object as the calibrated branch:

| §A example shows | Freeze / §D / backend say |
|---|---|
| `context.mtf_agreement: 0.72` | **null in v1** (§D "yes (null in v1)"); backend emits null even calibrated |
| `signal.horizon: "next_4xM15"` | **null in v1** (§D "nullable in v1 … null until T15") |
| `signal.confidence_lo: 0.57`, `confidence_hi: 0.69` | §D marks optional "yes"; backend emits null this release |
| `signal.model_version: "v1.0"` | frozen null this release (`BACKEND_FRONTEND_API_V1.md`) |
| `signal.features_contributing: [{name,weight}]` | backend emits `[]`; schema `array` (untyped) |
| `levels.*` all populated incl. `sl_method:"atr_1.5x"`, `tp_method:"rr_2x"` | frozen null this release (F15-3 / T17 ruling) |
| `meta.data_freshness_sec: 3` | **null in v1**; backend emits null |
| `meta.score_is_probability: true` | **always `false` in v1** (E07); §D says exactly this |

The `score_is_probability:true` line is the sharpest: §A shows the opposite of
§D ("always `false` in v1 — not a display switch") and of the C-1 gate (which
forces `presentable ⇒ probability`, and E07 sets the flag false). A frontend that
trusts §A will render the raw score as a probability — the exact failure the T18
guide exists to prevent.

**Fix:** replace the §A example with the frozen default shape (mirroring
`tests/fixtures/api_v1/valid/analysis_latest.json`), i.e. `probability:null`,
`probability_calibrated:false`, `score` present, `score_is_probability:false`,
`horizon`/`confidence_lo/hi`/`model_version`/`mtf_agreement`/`data_freshness_sec`
null, every `levels.*` null; and show the calibrated variant as a small delta
(`probability`, `probability_calibrated`, `coverage_tier` only). The T22 fixtures
are the ground truth and now assert this exact shape.

### F18-2 — non-blocking: `m15_trigger`/`direction` vocabulary

§A shows `context.m15_trigger:"LONG"` while `signal.direction:"UP"`. The backend's
`directionToUpDown` maps LONG→"UP", SHORT→"DOWN", NONE→"NONE"; the schema's
`direction` enum is `UP/DOWN/FLAT/NONE/UNKNOWN`. So the guide mixes LONG/SHORT (a
trigger vocabulary) with UP/DOWN (the emitted direction), and `FLAT`/`UNKNOWN` are
never emitted. Recommend one sentence pinning the emitted direction values so the
frontend's enum handling matches the contract.

### F18-3 — non-blocking: §J/§K claim the `api-v1.0` tag

§J ("It is tagged…") and §K ("tag `api-v1.0`") assert an immutable tag that does
not exist (`git tag -l` empty; no remote tags). Same as F17-2; it is now
frontend-facing. Tag it or strike the claim.

### F18-4 — info: §K mock claim is overstated pending F19-1

§K says the mock "serves the frozen contract" and "can never serve a probability
in uncalibrated mode". The probability half is true; the "frozen contract" half is
not yet — per F19-1 the mock default still serves populated
`horizon/sl_method/tp_method/data_freshness_sec/mtf_agreement`, which §D declares
null. Once Agent-C lands the F19-1 fidelity fix, §K is accurate.

## Result

**NEEDS WORK.** §D/§J/§K prose is sound and the envelope drift is fixed, but the
§A example is the pre-freeze shape and contradicts §D, E07, and the freeze —
`score_is_probability:true` in particular. Replacing §A with the T22 default
fixture shape resolves F18-1 (and F18-2/4 follow). F18-3 is the tag.

## Notes

- **Read-only.** Agent-D did not edit the guide (Lead-owned); findings only.
- **Cross-check method.** Compared every field in §A against `API_V1_SCHEMA.json`,
  `BACKEND_FRONTEND_API_V1.md`, the T22 `valid/analysis_latest.json`, and the real
  backend dump. Reproduces from `8b2b82c`.
