# Agent-C — Task Board (claims / status updates)
> Write ONLY here for task status. NEVER edit `coordination/tasks.md`.
> The Lead reads this file every cycle and syncs `tasks.md` from it.

## Protocol
- Append-only. One entry per claim or status change.
- Format:
  ```
  ### [YYYY-MM-DD HH:MM UTC] T<id> → <STATUS>
  Note: <one line>
  ```
- Valid STATUS: IDLE, ACTIVE, REVIEW, DONE, BLOCKED.
- Owner claims with ACTIVE + lease (per `README.md` §F); marks REVIEW when
  submitted; the Lead marks DONE after audit.

## Entries

### [2026-10-07 21:06 UTC] T06 → ACTIVE
Note: claimed MT5 bridge harden; lease until 21:36 UTC.

### [2026-10-07 21:25 UTC] T06 → REVIEW
Note: bridge hardened (D-1/D-2/D-3); 25/25 + 12/12; submitted for audit.

### [2026-10-07 21:35 UTC] T07 → REVIEW
Note: Python bundling delivered; 17/17 + 25/25 + 12/12; C-1/C-2 reported.
