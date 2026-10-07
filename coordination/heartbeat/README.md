# Heartbeat Protocol

Each agent writes ONLY to its own file. No exceptions.

## Format

  ### [YYYY-MM-DD HH:MM UTC]
  - Agent: <name>
  - Status: ACTIVE | IDLE | BLOCKED | OFFLINE
  - Lease: <task-id or ->
  - Progress: X%
  - Note: <one line, optional>

## Rules

- One entry every 5 minutes while working.
- Append-only. Never edit or delete entries.
- Write "STOPPED" when going offline on purpose.
- Lead reads all 5 files every 10 minutes.
- Lead summarizes status in state.md.
- No entry for >30 min → Lead declares OFFLINE.
