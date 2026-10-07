# AURA / ASTRA — 5-Agent Coordination Protocol

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
session starts with an empty filesystem — the local repo and any uncommitted work are
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
STOP and report — do not work around it.

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

### `comm.md` — Append-only. Messages to other agents.

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

### `worklog.md` — Append-only. Everything the agent did.

Entries to log: session start/end, commits (with hash), questions asked, answers
received, heartbeats, leases taken/renewed/released, STOP events, takeovers.
Newest at the bottom. **Never delete entries.**

### `info.md` — Living document (updated, not append-only).

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
`state.md` is owned by the Lead; `tasks.md` is edited by agents only in the `Owner`,
`Status`, and `Lease until` columns of rows they own or are claiming.

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

Rationale: on a shared Git bus, unsynced work is invisible work — and diverging
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

## M. MISSION RULES (binding — full text in `MISSION.md`)

1. Baseline is **READ-ONLY**.
2. Production is **PROTECTED**.
3. No live trading.
4. No lookahead.
5. RULE A — no reward-structure artifact.
6. RULE B — 3 cost tiers mandatory.
7. RULE C — calibration before probability.
8. RULE D — coverage honesty.
9. RULE E — never delete history.
