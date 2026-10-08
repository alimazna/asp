# Audit Report — T26 (interim): real-data feature harness (synthetic path)

- **Auditor:** Agent-D
- **Owner:** Agent-A (Features & Analytics)
- **Date:** 2026-10-08 08:35 UTC
- **Repo HEAD:** e2cc9d7
- **Verdict:** **INTERIM — synthetic path PASS; real-data run pending T25 corpus.**
  The Lead's T26 directive explicitly gates the evidential result on the T25 corpus;
  this audits what can be audited today (the harness logic on a synthetic series) and
  states what remains.

---

## What T26 is

Real M1 bars -> **C++ analytical feature engine** (`build/aura_feature_dump`) ->
frozen `FeatureSet` JSON consumed by `src/models/features.parse_feature_set`.
`research/features_real/run_features.py` does calendar aggregation M1->nine TFs and
decision instants (M15 close); **no Python-side feature recompute**.

## Verified (synthetic path)

Rebuilt the C++ tree (`cmake --build build`) — `aura_feature_dump` was not present in
this checkout, and the harness silently **skips** its 4 real checks when the binary is
absent. With the binary built: `tests/features/test_real_data_harness.py` **10/10**.
That covers, per the harness docstring and my run:

1. one validated FeatureSet per decision instant;
2. all nine streams + cross share the single decision instant (F2 — causality);
3. no-lookahead: first-K decisions byte-identical with/without later bars (F1);
4. determinism: two runs byte-identical.

These are the right invariants for a no-lookahead feature path, and they pass on the
synthetic series.

## Cannot yet verify (why this is interim)

- **Aggregation fidelity vs Dukascopy's own higher-TF bars** — Agent-A flags this
  himself as residual risk #1. It needs the real corpus to compare.
- **The real-data sample numbers** (`research/features_real/sample_<year>.json`) are
  emitted only when the T25 corpus lands. Nothing to re-derive yet.
- **The synthetic self-test is declared "a logic fixture, not evidence about gold"**
  — correctly honest; I will not treat it as evidence either.

## Observations

- **Silent-skip risk:** the harness `@unittest.skipUnless(isfile(DUMP))` means a CI
  without the built binary reports "OK (skipped=4)" — green without exercising the
  engine. Not wrong (honest skip), but a reviewer could over-read it. Recommend the
  T29 real-data run assert the binary is present, not skip.
- `barsAvailable` is a lower bound (bounded 300-bar window >= engine's 200-bar
  context); Agent-A states values are unaffected and verified equal to the unbounded
  run. I will re-verify this only insofar as it affects the real sample.

## Next

When T25's corpus + `QUALITY.md` land I will: reproduce the quality report
(`tools/quality_check.py`), run `run_features.py` on the real bars, and independently
re-derive a sample of feature values from the raw M1 (no trust in the pipeline) as
part of **T29**. No source touched.
