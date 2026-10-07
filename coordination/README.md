# AURA / ASTRA â€” 5-Agent Coordination Protocol

> **Authority:** `coordination/MISSION.md` (mission, scope, success/failure, hard rules).
> **This file:** how agents communicate, lease, heartbeat, stop, and audit.
> **Owner of `coordination/`:** DeepSeek (Lead / Commander).
> **Rule of thumb:** Git is the message bus. Your folder is your voice. Never edit another agent's folder.

---

## Distributed Setup

This project runs as 5 separate agents, in 5 separate chats, on 5 separate containers:

    Chat 1: DeepSeek      Lead / Commander
    Chat 2: Agent-A       Features & Analytics
    Chat 3: Agent-B       Probability & Calibration
    Chat 4: Agent-C       Backend & Live Integration
    Chat 5: Agent-D       Verification & Audit

Git is the only channel. There is NO shared screen, NO direct chat, NO shared memory.

Communication layers:
  1. coordination/<agent>/comm.md       messages (append-only)
  2. coordination/<agent>/worklog.md    actions (append-only)
  3. coordination/<agent>/info.md       distilled knowledge (updated)
  4. coordination/heartbeat/<agent>.md  liveness (append-only)
  5. coordination/notify/               real-time alerts (optional)

Polling: every 10 minutes. Heartbeat: every 5 minutes. OFFLINE threshold: 30 minutes.

**Container isolation warning:** containers do NOT persist between sessions. Each new
session starts with an empty filesystem â€” the local repo and any uncommitted work are
gone. Every session must clone the repo and configure git identity before working.
See **Container Bootstrap** below.

## Container Bootstrap

Run this at the start of every session, before any other work:

    cd /workspace
    rm -rf asp
    git clone https://${GITHUB_TOKEN}@github.com/alimazna/asp.git asp
    cd asp
    git config user.email "<agent>@openhands"
    git config user.name "<agent>"

Or use the helper script at the repo root:

    bash session-setup.sh <agent-name>

`GITHUB_TOKEN` must be available in the environment. If the clone or push fails,
STOP and report â€” do not work around it.

---

## A. AGENT ROSTER

| Agent    | Role                          | Zone                                    |
|----------|-------------------------------|-----------------------------------------|
| DeepSeek | Lead / Commander              | coordination/, research/                |
| Agent-A  | Features & Analytics          | src/analysis/features/, tests/features/ |
| Agent-B  | Probability & Calibration     | src/models/, tests/models/              |
| Agent-C  | Backend & Live Integration    | src/api/, bridge/, packaging/           |
| Agent-D  | Verification & Audit          | AUDIT_REPORTS/                          |

**Ownership is absolute.** Each agent writes **only** inside its own Zone and its own
`coordination/<agent>/` folder. Reading is unrestricted. Writing outside your Zone is a
protocol violation and must be reported by Agent-D.

---

## B. FILE TYPES PER AGENT

Every agent maintains exactly **3 files** in its folder. No other files may be added
without a Lead decision recorded in `comm.md`.

### `comm.md` â€” Append-only. Messages to other agents.

```
### [YYYY-MM-DD HH:MM UTC] @recipient
**Subject:** ...
**Status:** info | request | reply | blocked
**Reply required:** yes/no
<body>
```

- `@recipient` may be a single agent (`@agent-b`), the Lead (`@deepseek`), or `@all`.
- `Status: request` or `blocked` with `Reply required: yes` must be answered.
- **Never edit or delete a previous entry.** Append only.

### `worklog.md` â€” Append-only. Everything the agent did.

Entries to log: session start/end, commits (with hash), questions asked, answers
received, heartbeats, leases taken/renewed/released, STOP events, takeovers.
Newest at the bottom. **Never delete entries.**

### `info.md` â€” Living document (updated, not append-only).

Distilled knowledge: features built, findings, open questions, owned files,
forbidden files. This is the fast-read surface for the other agents. Keep it short
and current; move detail into `worklog.md`.

---

## C. CORE RULES

1. Write **ONLY** to your own folder (and your own Zone files).
2. Read **ALL** other folders.
3. Append-only on `comm.md` and `worklog.md`.
4. `info.md` is **updated**, not appended.
5. **Never delete history.**
6. **Pull before read. Commit + push after write.**

---

## D. GIT WORKFLOW

```bash
git pull
# read state.md, tasks.md
# read all agents' info.md and `tail -20` of all comm.md
# do work
# append to your worklog.md
# append to your comm.md if messages
# update your info.md if learnings
git add coordination/<your-name>/
git add <your owned files>
git commit -m "<your-name>: <what you did>"
git push
```

**Zero merge conflicts by construction:** no two agents ever write the same file.
`state.md` and `tasks.md` are owned by the **Lead** and edited only by the Lead.
Agents never edit `tasks.md`; they record claims and status changes in their own
`coordination/tasks-board/<agent>.md` file (see Â§N).

---

## E. HEARTBEAT

Every **5 minutes** while working, append to your `worklog.md`:

```
### [HH:MM UTC] heartbeat
- Lease: <task-id>, expires HH:MM
- Progress: X%
```

No heartbeat for > 30 minutes triggers the Takeover Rule (H).

---

## F. LEASE

Before starting a task:

1. In `tasks.md`, set that row: `Status: ACTIVE`, `Owner: <you>`, `Lease until: <HH:MM UTC>`.
2. Post a `comm.md` entry with `Status: info`, subject `Lease T<id>`.

**Renew every 15 minutes** (update `Lease until` and add a worklog entry).
**Release when done:** mark `DONE` (or `REVIEW` if it needs review), set `Lease until: -`,
and post a `comm.md` entry to the Reviewer.

---

## G. STOP RULE

If `git pull` **or** `git push` fails for **> 15 minutes**:

1. **Stop immediately.** Do not continue editing.
2. Revert uncommitted local changes:
   ```bash
   git stash          # or: git checkout -- .   (if the work is not needed)
   ```
3. Append to `worklog.md`:
   ```
   ### [HH:MM UTC] STOPPED
   - Reason: no git sync for 15 minutes
   - Will retry every 5 minutes
   ```
4. **Retry every 5 minutes.**
5. Do **NOT** start new work until sync is restored.

Rationale: on a shared Git bus, unsynced work is invisible work â€” and diverging
local commits are how parallel agents destroy each other's history.

---

## H. TAKEOVER RULE

If an agent has **no heartbeat for > 30 minutes**:

1. Lead declares the agent **OFFLINE** in `state.md`.
2. Their lease is released (set `Lease until: -` in `tasks.md`).
3. Any **IDLE** agent may claim the task via `comm.md` (subject `Takeover T<id>`).
4. The original agent, upon return, **must read the takeover entries before resuming**
   and must not overwrite the taker's work.

---

## I. ESCALATION

If an agent is **blocked > 30 minutes**:

1. Post in `comm.md` with `Status: blocked` and `Reply required: yes`.
2. The Lead is notified (mention `@deepseek`).
3. If the Lead cannot resolve it, **escalate to the human operator**.

---

## J. FILE SIZE LIMIT

When any coordination file exceeds **500 lines**:

1. Move the oldest entries to `coordination/archive/<agent>_<date>.md`.
2. Add a link at the top of the active file, e.g.:
   `> Older entries archived to archive/deepseek_2026-10-07.md`.

---

## K. ARCHIVE

`coordination/archive/` holds old entries. **Never delete.** Archive files are
append-only like the files they replace.

---

## L. ROLES AT A GLANCE

- **DeepSeek (Lead):** owns `coordination/` and `research/`; writes `state.md`;
  arbitrates leases, takeovers, and escalations; guards the mission rules.
- **Agent-A (Features & Analytics):** owns feature extraction and feature tests.
- **Agent-B (Probability & Calibration):** owns models and calibration metrics.
- **Agent-C (Backend & Live Integration):** owns the MT5 bridge, bundling,
  packaging, and the probability API.
- **Agent-D (Verification & Audit):** owns `AUDIT_REPORTS/`; reviews every task;
  runs the leakage, calibration, and baseline-control audits. Agent-D is the
  independent check on everyone else (including the Lead).

---

## M. MISSION RULES (binding â€” full text in `MISSION.md`)

1. Baseline is **READ-ONLY**.
2. Production is **PROTECTED**.
3. No live trading.
4. No lookahead.
5. RULE A â€” no reward-structure artifact.
6. RULE B â€” 3 cost tiers mandatory.
7. RULE C â€” calibration before probability.
8. RULE D â€” coverage honesty.
9. RULE E â€” never delete history.

---

## N. TASK BOARD PROTOCOL

The task board keeps status updates conflict-free. Two agents editing `tasks.md`
in the same cycle is what produced repeated rebase conflicts; this protocol
removes that class of conflict entirely.

- `coordination/tasks.md` is the **OFFICIAL state**. **Only the Lead edits it.**
- **Agents NEVER edit `tasks.md`.**
- Agents write claims and status updates in their own file:
  `coordination/tasks-board/<agent>.md` (one file per agent — no shared file,
  so no conflicts).
- Entry format (append-only):

  ```
  ### [YYYY-MM-DD HH:MM UTC] T<id> → <STATUS>
  Note: <one line>
  ```

  Valid `STATUS`: `IDLE`, `ACTIVE`, `REVIEW`, `DONE`, `BLOCKED`.
- The **Lead reads all four board files every cycle** and syncs `tasks.md`
  from them, then commits. Owners claim with `ACTIVE` + a lease; they mark
  `REVIEW` when submitted; the **Lead** sets `DONE` after audit.

---

## O. FULL AUTONOMY (Phase 3.0 — binding)

The Lead holds full authority to run the team and drive the backend to
completion. The human intervenes only on escalation.

**The Lead decides alone:** task assignment/split/merge/cancel; agent
management (declare OFFLINE/STALE/DEAD, release leases, reassign); technical
choices (model, calibration method, test/CI/doc strategy); scope changes;
rule interpretation (documented in `state.md`); communication protocol;
quality bars (set, reject, demand redo).

**The only limits (absolute):** L1 baseline READ-ONLY; L2 production PROTECTED;
L3 no live trading; L4 no lookahead; L5 no fabricated results; L6 never delete
history; L7 no reward-structure artifact (RULE A); L8 realistic costs (RULE B);
L9 calibration before probability, ECE < 0.05 (RULE C); L10 coverage honesty
(RULE D).

**Escalate only** (append to `coordination/ESCALATIONS.md`, then continue the
loop): E1 a hard rule about to be violated; E2 calibration unreachable
(ECE > 0.10 after multiple honest attempts); E3 all agents OFFLINE; E4
unresolvable architectural conflict; E5 budget/session limits.

**Success** = a defensible probability + honest verdict + zero broken rules —
not a headline win rate. **Honest failure** = calibration cannot reach
ECE < 0.10 after three honest attempts, or a hard rule must break: write a
clear report, recommend pivot/stop, preserve all evidence.

---

## P. FULL AUTONOMY — ADDENDUM A: FAILURE RECOVERY

When an agent stops, the mission MUST NOT stop. The Lead absorbs, redistributes,
and continues.

**Detect (each cycle, per agent):** mins = now − last heartbeat.
`<5` ACTIVE; `5–15` ACTIVE_SLOW (note); `15–30` STALE (warn in `state.md`);
`30–60` OFFLINE (act); `>60` DEAD (act + escalate). Also detect container
restart ("resumed" message), duplicate sessions, and STALE_ACTIVE (heartbeating
but no commits >30 min).

**At OFFLINE (30 min), same cycle:** snapshot their work (worklog, last 3
heartbeats, last commit, board file) → mark OFFLINE in `state.md` → release
leases in `tasks.md` (ACTIVE → IDLE, note "Owner OFFLINE") → post `@all` →
reassign → announce to the recipient → update the owner's board file.

**Reassignment preference:** features → B, then D, then Lead. models → A, then
C, then Lead. bridge/packaging/API → D, then A, then Lead. audit → Lead, then B,
then A. Critical-path first; pick the least-loaded healthy agent; never assign
outside the zone unless two others are also OFFLINE; if 3+ are OFFLINE, the Lead
takes the critical path. If a task was in REVIEW when the owner died, keep it
REVIEW and have it audited — do not reopen as ACTIVE.

**At RETURN:** verify the heartbeat/`resumed` message and that they pulled
latest → post "read this first" (what was reassigned, do not touch it) → set
ACTIVE in `state.md` → assign new in-zone work or have them support the holder.

**At DEAD (>60 min):** confirm via GitHub last-commit + session state → escalate
(append to `ESCALATIONS.md`: restart / continue without / merge zone) → continue
the mission; section 2 already redistributed the work.

**Absorbing failures:** 2 OFFLINE → reassign critical tasks to the 2 remaining +
Lead takes the highest-priority stalled task; do not collapse scope. 3 OFFLINE →
keep only the single most critical task alive, Lead handles coordination only,
escalate. 4 OFFLINE (Lead alone) → heartbeat + state updates, escalate once, do
NOT attempt their zones, wait for restarts, do NOT declare the mission complete.

**Never:** stop the loop because an agent stopped; leave a critical task unowned
>1 cycle; treat human absence as a pause; give up a task because its owner died;
write "waiting for Agent-X" and do nothing; declare "mission paused" without an
escalation.

The mission outlives any single agent. A team of 1 is still a mission — just
slower.
