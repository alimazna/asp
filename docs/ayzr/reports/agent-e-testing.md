# Agent E — Testing & Security — Report

Date: 2026-10-27 (UTC). Repository: `/workspace/project/asp`, branch `main`,
HEAD `9ebfe62` plus the uncommitted AYZR working tree (news module, API routes,
Android app). No commit, push or deploy was performed.

## Files created or changed

Created:
- `tests/AyzrFailureInjectionTests.cpp` — 18-case failure-injection + auth-boundary
  suite (new; discovered automatically by the CMake test glob).
- `docs/ayzr/TESTING.md` — build/run instructions, exact totals, failure-injection
  matrix, findings.
- `docs/ayzr/SECURITY.md` — auth boundaries, secret handling, input validation,
  error hygiene, Android review, findings.
- `docs/ayzr/reports/agent-e-testing.md` — this report.

Changed: none. No source file, build file, contract or frontend file was
modified. `src/api/AstraApiServer.cpp` was temporarily mutated once to prove the
auth test has teeth and was then restored byte-identical
(`md5 5308116787e79c3abc186b2b3caeee1e`, verified with `md5sum` and
`git diff --stat`).

Not touched (out of scope): `astra-frontend/`, `windows-sender/`.

## Commands run (representative)

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j
cd build-linux && ctest --output-on-failure
cd build-linux && for t in *Tests; do ./"$t"; done          # harness totals
./AyzrFailureInjectionTests                                  # 18 cases
python3 -m unittest discover -s tests/models -p "test_*.py"  # 308 tests
for f in tests/integration/test_*.py; do python3 "$f"; done
# live probes: curl against astra_server on ephemeral ports with three tokens
# mutation proof: edit auth check -> rebuild -> test FAILS -> restore -> PASS
```

## Exact test results

| Suite | Result |
|-------|--------|
| CTest | **30/30 passed, 0 failed** (48.25 s) |
| C++ harness cases (30 binaries) | **251, 0 failed** |
| `AyzrFailureInjectionTests` (new) | **18, 0 failed** |
| `SingleInstanceTests` | 10/10 assertions (own printer) |
| `tests/models` (unittest) | **308 passed, 0 failed** |
| `tests/integration/*.py` | 6 PASS; 2 SKIP (no `build/`); 1 script 1 pre-existing FAIL |

No test in the new suite skipped (`grep -c SKIP` = 0). The suite passed on four
consecutive runs, so it is not flaky.

Mutation proof of teeth: weakening the `news/ingest` auth check to also accept
the client token made `ayzr_news_ingest_write_requires_sender_credential` fail
(1 failed, 1 assertion); the source was then restored and all 18 cases passed
again.

## Findings

**F-E-1 — Stale-feed masking in the Python bridge (PRE-EXISTING, OPEN).**
`bridge/mt5_python/mt5_client.py::_capture_clock_offset` derives the broker
offset from the newest forming M1 bar (the same stale bar freshness is meant to
judge) and accepts it while `|offset| ≤ 18 h`. A feed stale by up to 18 h
therefore reports `freshness: FRESH`, `quality: VALID`. Reproduced with the
committed `test_bridge_t06.py` harness:

| Injected staleness | Reported offset | Freshness | Quality |
|---|---|---|---|
| 0–17 h | −N (= staleness) | `FRESH` | `VALID` |
| 19 h / 24 h | 0 (clamped) | rejected `MARKET_DATA_STALE` | `UNKNOWN` |

This makes `test_bridge_t06.py::scenario_stale` fail (24/25 checks) in the
committed repository state, independent of the AYZR working tree. It is exactly
the honesty failure the rules forbid (a stale reading served as decision-grade).
Not fixed — outside Agent E's write scope and needs a design decision; suggested
resolution is in `docs/ayzr/TESTING.md` (bound the offset band and cross-check
against an independent clock reference; do not widen the clamp).

**F-E-2 / F-E-5 — Test isolation on hard-coded port 8799 (environmental).**
`PythonBridgeContractTests` binds fixed port 8799; a concurrent listener makes it
report false failures (observed once while another process held 8799). Test-only;
fix is to allocate an ephemeral port.

**F-E-3 — `news/ingest` moved from client-token to sender-token auth (RESOLVED).**
The working tree changed this during the session. It is the correct boundary
(news ingest is a write; the client token is embedded in the Android app).
Verified: client/admin → 401, sender → 200. Flagged as a breaking change for any
existing client-token publisher.

**F-E-4** — same root cause as F-E-1, recorded from the security angle in
`docs/ayzr/SECURITY.md`.

No finding was a secret disclosure, an unauthenticated privileged route, or an
internal-detail leak; all error bodies passed the no-leak assertion.

## Verification honesty

- Totals above are from commands actually executed in this session; the working
  tree was being edited concurrently by another agent, so the numbers reflect the
  tree at the stated revision and may differ after further concurrent edits.
- Two integration scripts SKIP because no `build/` directory (with
  `aura_backend_host`) exists; this is their designed behaviour, not a pass.
- The one failing check (F-E-1) is reported as a failure, not worked around.
