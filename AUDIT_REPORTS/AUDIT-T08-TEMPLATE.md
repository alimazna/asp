# Audit Report — T08 (Windows packaging) — WINDOWS LAUNCH

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** <fill on audit>
- **Repo HEAD at audit:** <fill>
- **Verdict:** PASS | FAIL | NEEDS WORK

---

## Claim

<What Agent-C claimed. Quote their "ready for audit" comm.md entry verbatim,
with timestamp and commit hash. State acceptance criteria: packaged app starts
by double-click without CMD; paths resolve from executable root; bundled bridge
starts automatically.>

---

## Evidence inspected

- **Commit(s):** <hash(es) claimed>
- **Files:** `src/platform/windows/*`, `CMakeLists.txt`, `src/api/RuntimeManifest.json`
- **Test output:** <exact command(s) run and raw output — Agent-D reruns them>
- **Raw commands + output:** <paste below>

```
<paste exact commands and output>
```

---

## Verification steps

1. **CWD independence.** Confirm paths resolve from the executable location, not
   the current working directory. Run the host from an unrelated CWD.
2. **No CMD required.** Confirm startup does not depend on PATH setup or a
   developer shell.
3. **Bundled bridge auto-start.** Confirm the host locates and launches the
   bundled bridge as a supervised child.
4. **Actionable failure.** Confirm a missing dependency produces an actionable
   error, not a silent failure.
5. **Packaging coherence.** Confirm `RuntimeManifest.json` declares api `v1`,
   mode `SHADOW`, `live_trading_authorised: false`, `requires_manual_cmd: false`.
6. **Windows-specific evidence.** State plainly whether the Windows double-click
   path was *executed* or only *compiled*. Compilation is not execution.
7. **Reproduce independently.** Run the available startup tests; do not trust
   the claim.

---

## Result

PASS | FAIL | NEEDS WORK

<One-line justification. Note explicitly if the Windows path could not be
executed in this environment — that is a documented limitation, not a PASS.>

---

## Notes

<Whether Windows execution evidence exists or is absent; whether the Linux
toolchain was used instead; what remains unverified.>
