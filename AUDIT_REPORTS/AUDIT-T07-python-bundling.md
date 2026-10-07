# Audit Report — T07 (Python bundling)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 21:26 UTC
- **Repo HEAD at audit:** 7e7a752d6378ad3a97b329e64b0c9e620cf4df40 (commit under audit)
- **Verdict:** **PASS — with a caveat on completeness (F1).** The deliverables are
  correct and independently reproduced; T07 may move to DONE at the Lead's
  confirmation. F1 is not a defect in Agent-C's work — it is a scope limit on
  what T07 can claim, caused by the pre-existing production contradiction C-1.

---

## Claim

Agent-C, `coordination/agent-c/comm.md` 21:40 UTC, "T07 Python bundling complete;
2 frozen-layer contradictions found" (`Reply required: yes`):

> T07 delivered:
>   - packaging/bundle_manifest.json  (single source of truth for installed layout)
>   - scripts/bundle.py               (stage + verify the bundle; no CWD dependence)
>   - packaging/requirements-runtime.txt
>   - packaging/README.md
>   - tests/integration/test_bundling_t07.py  (17/17 checks pass)
>   - No production src/ changes.
> CONTRADICTIONS (reported, not fixed — both touch protected src/): C-1 (python
> runtime path) and C-2 (pandas).

Commit under audit: `7e7a752` "agent-c: T07 Python bundling" (2026-10-07
21:20:28 +0000). Acceptance: stage the bundled Python runtime + bridge into the
layout BundleLocator/PackagingConfig/RuntimeManifest expect; document; verify;
no production src/ changes.

---

## Evidence inspected

- **Commit:** `7e7a752d6378ad3a97b329e64b0c9e620cf4df40`
- **Files:** `packaging/{bundle_manifest.json,requirements-runtime.txt,README.md}`,
  `scripts/bundle.py`, `tests/integration/test_bundling_t07.py` (+ agent-c
  coordination files, `coordination/tasks.md`).
- **Agent-D reran everything** and probed the C-1/C-2 contradictions directly
  against production source.

```
$ python3 tests/integration/test_bundling_t07.py
Parity: packaging manifest vs C++ runtime constants   (6 PASS)
Parity: packaging manifest vs RuntimeManifest.json    (4 PASS)
Parity: bridge_files cover the real bridge directory   (2 PASS)
Staging: build and verify a bundle                    (5 PASS)
17/17 checks passed

$ cd /tmp && python3 /workspace/project/asp/scripts/bundle.py --out /tmp/astra_out
bundle staged and verified OK
# tree: resources/{bridge/mt5_python,python}, config, data, logs, bundle_report.json
# (staged correctly from a foreign CWD -> CWD-independent, as claimed)

$ grep -n pythonRelativePath src/platform/windows/PackagingConfig.h
26:    std::string pythonRelativePath = "runtime/python/python.exe";
$ grep -n pythonRuntimeDir src/platform/windows/PathResolver.cpp
85:    out.pythonRuntimeDir = join(out.resourceDir, "python");
$ grep -n pandas src/platform/windows/PackagingConfig.cpp
69:        {"pandas", ">=2.0", true},
$ grep -rniE "import pandas|pandas" bridge/ src/api/      -> (no matches)
```

---

## Verification steps

1. **Reran the T07 test.** 17/17 pass.
2. **Independent staging run from `/tmp`.** `bundle.py` resolved the repo from
   `__file__` and staged a correct tree with no CWD dependence. Confirmed.
3. **Manifest parity.** `bundle_manifest.json` agrees with
   `PathResolver.cpp` (`resources/python`, `resources/bridge/mt5_python`) and
   with `src/api/RuntimeManifest.json` ports (8791 / 8790). Confirmed by reading
   both sides, not just the test.
4. **bridge_files completeness.** Every declared file exists; no real bridge
   source file is omitted. Confirmed by listing `bridge/mt5_python/`.
5. **Forbidden-artifact exclusion.** Injected a `__pycache__/x.pyc` into a staged
   bundle; `verify()` caught it. Confirmed.
6. **No production src/ changes.** `git show --name-only 7e7a752` shows only
   `packaging/`, `scripts/`, `tests/integration/`, and coordination files. No
   `src/` change. Confirmed.
7. **C-1 (python path) — reproduced.** See below.
8. **C-2 (pandas) — reproduced.** See below.

---

## C-1 — Python runtime path contradiction (reproduced; pre-existing, not T07's)

Agent-C's report is accurate. Agent-D read the production code end to end:

- `PathResolver.cpp:85` -> `pythonRuntimeDir = resources/python`.
- `BundleLocator.cpp` `locate()` searches **only** `paths_.pythonRuntimeDir`
  (`resources/python/python.exe`, `resources/python/bin/python3`, …) and
  `resources/python-embed`. It never searches `resources/runtime/python`.
- `PackagingConfig.h:26` -> `pythonRelativePath = "runtime/python/python.exe"`;
  `PackagingConfig.cpp:61` -> `interpreterPath() = join(resourceDir,
  pythonRelativePath)` = `resources/runtime/python/python.exe`.
- `StartupCoordinator.cpp` launches `report.bundle.pythonExecutable` (the
  **locator's** result), so the declared `pythonRelativePath` is not on the
  startup path today. The two paths do not collide in the current launch flow,
  but the declared path is a dead pointer the locator never searches.

**Scope limit (F1):** `bundle.py` deliberately does **not** place any
interpreter under `resources/python/` (the staged dir is empty). Consequently
`BundleLocator::locate()` would still fail on a staged bundle until an actual
interpreter distribution is placed there. So T07's manifest/layout parity is
correct and useful, but it does **not** make the bundle runtime-complete — no
Python runtime payload exists to stage (it is a binary distribution, out of
scope for this repo). T07's claim is therefore "layout parity + stager", not
"self-contained runtime". That distinction should be recorded.

## C-2 — pandas contradiction (reproduced; pre-existing, not T07's)

- `PackagingConfig.cpp:69` declares `{"pandas", ">=2.0", true}` (required).
- Neither `bridge/` nor `src/api/` imports pandas; `bridge/mt5_python/
  requirements.txt` does not list it.
- Reproduced: `grep -rniE "import pandas|pandas" bridge/ src/api/` -> no matches.

**Additional minor drift Agent-D found (C-3).** The numpy pin disagrees across
three files: `PackagingConfig.cpp` says `>=1.24`,
`bridge/mt5_python/requirements.txt` says `>=1.23`, `packaging/
requirements-runtime.txt` says `>=1.24`. Cosmetic, but it is a fourth
manifest-vs-manifest drift and belongs in the same reconciliation the Lead
escalated.

---

## Result

**PASS** on the T07 acceptance criteria (independently reproduced: 17/17 test,
CWD-independent staging, manifest parity, no src/ changes). F1 is a documented
scope limit, not a defect. T07 may go to **DONE** at the Lead's confirmation.

C-1 and C-2 are **pre-existing production contradictions**, correctly reported
and correctly left unfixed. They are already escalated to @human by the Lead
(`deepseek/comm.md` 21:22 UTC). Agent-D concurs and adds C-3 (numpy pin drift).

## Notes

- **No fabrication by the auditor.** All findings reproduced from the repo; the
  T07 test was rerun and `bundle.py` was executed independently.
- **Evidence limits.** No Windows host and no bundled interpreter binary are
  available; the Windows `python.exe` resolution path and a real interpreter
  launch were **not** exercised. This is a genuine limitation and is why F1 is
  flagged rather than silently accepted.
- **Zone.** T07 wrote to `packaging/`, `scripts/`, `tests/integration/`.
  `tests/integration/` was ratified to Agent-C by the Lead at 21:20 UTC, so this
  is now in-zone. `scripts/` and `packaging/` are consistent with the Backend &
  Live Integration role; no objection raised.
- **Recommendation.** Accept T07 as DONE (layout/stager parity), and record
  plainly that a runtime-complete bundle still requires an interpreter payload
  under `resources/python/` once C-1 is reconciled.
