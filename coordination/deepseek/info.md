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
- **Resume (2026-10-08 06:11 UTC):** Lead was offline ~6h40m (last commit cycle 25,
  23:30 UTC). Agents made progress meanwhile: **T22 DONE** (Agent-A F22-1b fix;
  Agent-D PASS twice — F22-1b 23:43, F22-4b 00:43). **Agent-C OFFLINE ~7h** holding
  the critical path (T17/T19) → BLOCKED, lease released; **E08 filed**. **E05
  re-escalated** (STILL BLOCKED since 22:36 UTC). Queued **T23** (Agent-B invariant
  checker) + **T24** (Agent-A T13 harness) as in-zone work safe to build against the
  frozen contract while Agent-C is dark.
- **Agent-D open findings awaiting Agent-C:** F17-0 (durable gate substring "pass"
  lets "NOT PASS" open RULE C gate), F17-1 (two-layer impl-vs-schema check), F17-2
  (api-v1.0 tag), F19-1 (mock frozen-null fidelity), F19-2 (score_is_probability),
  F22-4b-v (validator finite guard), F18-1 (guide §A pre-freeze — **Lead fixed
  2026-10-08**).
- The mission references `research/astra_3month_mtf/`, but that layer is not present in
  this repository, its history, or its sibling repos. Location must be confirmed before
  Sprint 1. See `tasks.md` Q1.
- **E05 is the hard blocker:** no real XAUUSD data exists (tree/history/remotes).
  Everything calibrated is synthetic and non-evidential; nothing may be published as
  a probability. Do not fabricate a dataset.
  **UPDATE 2026-10-08 07:10 UTC — RESOLVED BY HUMAN DECISION.** The Lead acquires the
  real data. Source: **Dukascopy public XAUUSD M1, 2021-01-01..2025-12-31 UTC**,
  collected via pinned `dukascopy-node` 1.50.0. Corpus under
  `research/data/xauusd_m1/` (fetch.sh + checksums + samples + QUALITY.md committed;
  raw CSVs gitignored). Chain: T25 (Lead data) -> T26 (Agent-A features) -> T27
  (Agent-B calibration) -> T29 (Agent-D audit). E05 closes on the T29 audit.
  Operation order for the collector: `-r 8 -rp 1500 -re -fr` (retries; the public
  endpoint 503s under load from datacenter IPs).
- **Timestamps:** use `date -u` — the Lead's early entries ran ~1h ahead of the
  machine clock; all agents' commits agree with machine time.
- **API v1 envelope:** every successful body is `{api:"v1", schema:"1.0", data:...}`;
  errors are flat `{error,code,message}`. `docs/architecture/API_V1_SCHEMA.json` is
  authoritative and (post-F17-1) is bound to the real backend output.
- **Fixture direction (T22):** fixtures pin the shape only for fields the freeze
  pins (frozen-null set + required structure); live fields (`symbol`,`timestamp`,
  `degraded`) copy the real backend and are not equality-compared. `model_version`
  is a **frozen null** in v1 and `features_contributing` is `[]` in both branches.
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
