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

## Key findings
- The mission references `research/astra_3month_mtf/`, but that layer is not present in
  this repository, its history, or its sibling repos. Location must be confirmed before
  Sprint 1. See `tasks.md` Q1.

## Open questions
- Where does `research/astra_3month_mtf/` live?
- Which broker/account supplies the dataset, and what are the 3 mandatory cost tiers?
- What is the canonical `base9` / `baseold` definition and its prior control numbers?

## Useful commands
- `git pull && git status -sb`
- `tail -20 coordination/*/comm.md`
- `cat coordination/*/info.md`
