# DeepSeek — Important Info
> Living document. Updated, not appended.

## Role
Lead / Commander / Architect. Owns `coordination/` and `research/`. Writes `state.md`.
Arbitrates leases, takeovers, and escalations. Guards the mission rules. Does not write
production code or the baseline.

## Owned files
- coordination/
- research/

## Forbidden files
- The baseline (base9 / baseold) — READ-ONLY.
- Production source (src/foundation/, src/api/, src/models/, src/analysis/features/,
  bridge/, packaging/) — PROTECTED.
- docs/archive/ — historical, never rewritten.
- coordination/<other-agent>/ folders.

## Features / deliverables
- 5-agent coordination framework (Phase 1): README, tasks, state, MISSION, agent folders.
- Phase 4.0 backend decision-support: model/decision/API layers are DONE or in
  audit; Lead owns `docs/architecture/DECISION_MODEL.md` and
  `docs/frontend/FRONTEND_HANDOFF_GUIDE.md`.

## Key findings
- The mission references `research/astra_3month_mtf/`, but that layer is not present in
  this repository, its history, or its sibling repos. Location must be confirmed before
  Sprint 1. See `tasks.md` Q1.
- **E05 is the hard blocker:** no real XAUUSD data exists (tree/history/remotes).
  Everything calibrated is synthetic and non-evidential; nothing may be published as
  a probability. Do not fabricate a dataset.
- **Timestamps:** use `date -u` — the Lead's early entries ran ~1h ahead of the
  machine clock; all agents' commits agree with machine time.
- **API v1 envelope:** every successful body is `{api:"v1", schema:"1.0", data:...}`;
  errors are flat `{error,code,message}`. `docs/architecture/API_V1_SCHEMA.json` is
  authoritative and (post-F17-1) is bound to the real backend output.
- **E07:** `meta.score_is_probability` is always `false` in v1;
  `signal.probability_calibrated` is the source of truth for score-vs-probability.

## Open questions
- Where does `research/astra_3month_mtf/` live?
- Which broker/account supplies the dataset, and what are the 3 mandatory cost tiers?
- What is the canonical `base9` / `baseold` definition and its prior control numbers?
- E05: when will real XAUUSD data arrive (gates publication + T17 v1.x levels)?

## Useful commands
- `git pull && git status -sb`
- `tail -20 coordination/*/comm.md`
- `cat coordination/*/info.md`
