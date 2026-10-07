# Audit Report — T12 (Baseline control check)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-D (self-owned audit; reviewed by Lead)
- **Date:** 2026-10-07 21:07 UTC
- **Repo HEAD at audit:** a4a7ca3dcef75bf31fe58391a80b8c15d24bcf1e (`main`)
- **Verdict:** **BLOCKED — CANNOT RUN.** Baseline controls are absent from the
  repository, its full history, and all accessible branches.

---

## Claim

The Lead assigned T12 ("Baseline control check") to Agent-D (commit `a4a7ca3`,
`coordination/deepseek/comm.md` 21:05 UTC). The task intent, per `tasks.md`
Q3 and `state.md`, is to confirm that the baseline control strategies
(`base9`, `baseold`) reproduce their documented prior control numbers.

**Claim to verify:** that the baseline controls and their prior numbers are
available to be checked.

**Finding:** the claim cannot be verified because the artefacts do not exist.
This is a report of absence, not a pass or a fail of the controls themselves.

---

## Evidence inspected

All commands run read-only from `/workspace/project/asp` at HEAD
`a4a7ca3`. No file was created, modified, or deleted by the search commands
(the only writes in this session are this report, the agent-d coordination
files, and the T12 row of `tasks.md`).

### 1. Working tree — no research layer

```
$ ls -la research/
ls: cannot access 'research/': No such file or directory
```

The only `research` path in the tree is `src/research/`, which holds the C++
self-learning modules (RSH-0001..0020: `ContextLearning`, `ExperimentLedger`,
`KnowledgeStore`, `Sandbox`, …). These are the *research backend plane* of the
AURA runtime; they are **not** the `research/astra_3month_mtf/` experimental
layer the mission references, and they contain no `base9`/`baseold` control.

### 2. Text search — baseline symbols appear only as statements of absence

```
$ grep -rniIE "base9|baseold|candRA|astra_3month|EXP-001[89]|EXP-0020|baseline control|control strateg" --exclude-dir=.git .
```

Every hit is a coordination file *referring to the missing layer*, never a
definition or a number:

| File | Line | What it says |
|---|---|---|
| coordination/MISSION.md | 49 | rule: baseline (`base9`, `baseold`) is READ-ONLY |
| coordination/agent-a/info.md | 13 | forbidden-files note |
| coordination/agent-b/info.md | 13 | forbidden-files note |
| coordination/agent-c/info.md | 14 | forbidden-files note |
| coordination/deepseek/info.md | 14,24,29,31 | notes the layer is not present |
| coordination/state.md | 43,45,46,52 | "base9: NOT_RUN", "baseold: NOT_RUN" |
| coordination/tasks.md | 22,26 | Q1, Q3 open questions |

No file contains a `base9`/`baseold` definition, implementation, or control
number.

### 3. Full git history — never added in any commit

```
$ git --no-pager log --all --pretty=format: --name-only --diff-filter=A \
    | sort -u | grep -iE "research/astra|astra_3month|exp-00|base9|baseold|candRA|controls"
(no such paths ever added)

$ git --no-pager log --all --oneline | grep -iE "base9|baseold|baseline|research/astra|exp-00"
(no such commits)
```

Total history is 17 commits on a single branch. No path matching the research
layer or the controls was ever added, in any commit, on any ref.

### 4. Branches and tags

```
$ git --no-pager branch -a
* main
  remotes/origin/HEAD -> origin/main
  remotes/origin/main
$ git --no-pager tag
(no tags)
```

One branch (`main`), no tags.

### 5. Remote — single branch, zero code hits

```
$ curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
    https://api.github.com/repos/alimazna/asp/branches
['main']
```

GitHub code search, scoped to the repository, returns zero hits for each
control symbol:

```
base9+repo:alimazna/asp           => total_count: 0
baseold+repo:alimazna/asp         => total_count: 0
astra_3month_mtf+repo:alimazna/asp => total_count: 0
candRA+repo:alimazna/asp          => total_count: 0
EXP-0019+repo:alimazna/asp        => total_count: 0
```

### 6. Documentation and archive — no control definition

`docs/`, `project-control/`, `README.md`, `ai-prompts/` and `docs/archive/`
were searched for baseline/control definitions. The only "baseline" material is
**architectural**, not a control strategy:

- `docs/archive/XAUUSD_SOVEREIGN_MASTER_UNIFIED_PROJECT_v3.0.original.md`
  uses "baseline" to mean *the preserved canonical system reference* and, in the
  research-experiment template (§47), a *field name* in an experiment record
  (`BASELINE`). Neither defines `base9`/`baseold`.
- `project-control/TASK_MANIFEST.yaml:2296` contains the acceptance string
  "Historical baseline preserved" — a governance rule, not a strategy.

No file anywhere defines what `base9` or `baseold` compute, over what data, or
what numbers they previously produced.

---

## Verification steps

1. Cloned `alimazna/asp` fresh with the token and recorded HEAD
   `a4a7ca3` on `main` (Step 1).
2. Read `MISSION.md`, `README.md`, `state.md`, `tasks.md`, the Lead's
   `comm.md`/`info.md`, and `heartbeat/README.md` to establish what T12 means
   (Step 2). T12 = confirm the baseline controls reproduce their prior numbers.
3. Searched the working tree for the research layer and every control symbol
   (evidence §1–§2). Not present.
4. Searched the entire git history for any commit or path that ever introduced
   the layer or the controls (evidence §3). None.
5. Enumerated all local and remote branches and tags (evidence §4). One branch.
6. Queried the GitHub API for remote branches and repository-scoped code
   search (evidence §5). One branch; zero hits.
7. Searched the project documentation and the archived V3 master for a control
   definition (evidence §6). None found.
8. Concluded: the precondition for T12 (the controls exist) is not met.

---

## Result

**BLOCKED — CANNOT RUN.**

- `base9`: no definition, no implementation, no prior numbers found.
- `baseold`: no definition, no implementation, no prior numbers found.
- `research/astra_3month_mtf/`: absent from the tree, history, and remotes.

This is consistent with the Lead's own recorded finding in
`coordination/deepseek/info.md` and `coordination/state.md`. Agent-D
independently confirms it.

No baseline control result is reported, because none can be produced without
fabrication. Per the mission rules and the orientation directive, no numbers
were invented and no placeholder control files were created.

---

## Notes for the Lead

**Why this blocks.** T12's acceptance condition — "baseline controls
reproduce" — is unverifiable while the controls do not exist. Sprint 1's gate
(`state.md`) is stated as "T12 confirms baseline controls reproduce", so the
Sprint 1 gate cannot be satisfied as written. This is a *missing-precondition*
block, not a defect in the controls.

**Recommendation (two options, Lead decides — Agent-D does not choose for the
Lead):**

- **(a) Rebuild the controls from the backend.** Define `base9` (and
  `baseold`) concretely against the existing deterministic engines
  (`src/features/`, `src/structure/`, `src/regime/`, `src/signals/`) and the
  closed-bar data path, freeze the definition, then run the control and record
  its numbers as the reference. This makes the baseline reproducible but
  **does not recover the prior layer** — any "prior control numbers" remain
  unrecoverable, so the new numbers become the reference, not a confirmation.
- **(b) Proceed without controls and document the gap.** Continue Sprint 1
  feature work (T01/T02), and carry T12 as an explicit, documented gap. Any
  later result is then compared against a baseline that is defined
  *after the fact*, which must be recorded honestly (RULE E: keep the
  negative/absent result).

**Caveat on either option.** The mission's success criteria (ECE < 0.05, Brier
vs 0.25) and RULE B (3 cost tiers) currently have no implementation in the
repository either (see the cross-checks below). Rebuilding controls without a
cost-tier model would itself risk a RULE A violation. The Lead should treat the
research-layer absence and the cost-tier absence as one coupled gap.

**Cross-checks recorded (context, not part of T12):**

- `research/astra_3month_mtf/` absent (this report).
- No `ECE` / `Brier` / `reliability` / `isotonic` / `platt` anywhere in
  `src/` or `tests/`; `ProbabilityEngine` is structurally `UNCALIBRATED`.
- No cost-tier model anywhere in `src/` (RULE B unimplemented).
- `DecisionPipelineConfig.historyBars = 200` in code vs the mission's "latest
  9 closed candles" trigger window — no 9-candle window implementation found.

**Status change requested:** T12 stays **BLOCKED** (not DONE, not ACTIVE). The
T12 row in `tasks.md` is set to BLOCKED with this reason. Agent-D does not mark
T12 DONE.

**Hand-off:** awaiting the Lead's decision between (a) and (b) before any
baseline work proceeds.
