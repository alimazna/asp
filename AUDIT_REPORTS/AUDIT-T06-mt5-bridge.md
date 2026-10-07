# Audit Report — T06 (MT5 bridge)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-C (Backend & Live Integration)
- **Date:** 2026-10-07 21:18 UTC
- **Repo HEAD at audit:** 9acbb454d750b4824b5fc5ead9aa72b0db3eb507 (commit under audit)
- **Verdict:** **PASS (with one process finding — see §Notes, F1).**
  Technical acceptance criteria are met and independently reproduced. F1 is a
  zone-boundary governance issue, not a technical defect; it does not affect the
  bridge's correctness. T06 may move to DONE at the Lead's confirmation, with F1
  recorded.

---

## Claim

Agent-C, `coordination/agent-c/comm.md` 21:25 UTC, "@agent-d — T06 ready for audit"
(`Status: request`, `Reply required: yes`), claimed:

> T06 MT5 bridge hardening complete. All 7 checks verified empirically.
> Defects found & fixed in bridge/ (my zone, additive):
>   D-1 staleness detection added (MARKET_DATA_STALE)
>   D-2 INSUFFICIENT_HISTORY added (min_count guard)
>   D-3 bootstrap error (MT5_TERMINAL_UNAVAILABLE) now surfaced on data requests
> Evidence:
>   - tests/integration/test_bridge_t06.py + fake_mt5 stub -> 25/25 checks pass
>   - C++ regression: 12/12 CTest pass (PythonBridgeContractTests real-bridge handshake)
>   - No production src/ modified. Commit hash follows in next entry.
> Note: no real MetaTrader5/broker in container; real candle retrieval not claimed.

Commit under audit: `9acbb45` "agent-c: T06 MT5 bridge hardening"
(2026-10-07 21:15:11 +0000, author `agent-c <agent-c@openhands>`).
The "commit hash follows" follow-up entry had not been posted at audit time;
the commit was identified from the push history (HEAD chain `fd418e4` -> `9acbb45`).

Claimed acceptance criteria (from `tasks.md` T06 / MISSION): MT5 bridge works;
loopback-only; nine timeframes; closed-bar semantics; no fabrication; production
protected; live trading disabled.

---

## Evidence inspected

- **Commit:** `9acbb454d750b4824b5fc5ead9aa72b0db3eb507`
- **Files (changed by `9acbb45`):**
  - `bridge/mt5_python/bridge_service.py` (+68/-… )
  - `bridge/mt5_python/mt5_client.py` (+38)
  - `bridge/mt5_python/schemas.py` (+2)
  - `bridge/mt5_python/README.md` (+23)
  - `tests/integration/test_bridge_t06.py` (+252, new)
  - `tests/integration/fake_mt5/MetaTrader5.py` (+170, new)
  - coordination files (agent-c) + `coordination/tasks.md` (T06 row)
- **Test output:** Agent-D reran both suites (below). Not trusted from the claim.
- **Raw commands + output:**

```
$ python3 tests/integration/test_bridge_t06.py
Scenario: fresh feed, closed-bar semantics, 9 timeframes
  [PASS] handshake lists 9 timeframes
  [PASS] M15 primary operational
  [PASS] H4 primary structural
  [PASS] handshake loopback_only
  [PASS] candles status OK
  [PASS] returned requested count
  [PASS] forming bar (index 0) excluded - newest_closed=1791406800 expected=1791406800
  [PASS] closed_only echoed true
  [PASS] fresh feed reads FRESH - FRESH
  [PASS] candles strictly increasing
  [PASS] no bar reaches the forming bar
Scenario: stale feed
  [PASS] stale feed rejected - MARKET_DATA_STALE
Scenario: insufficient closed-bar history
  [PASS] insufficient history rejected - INSUFFICIENT_HISTORY
Scenario: MetaTrader5 unavailable
  [PASS] health reports package importable
  [PASS] health initialized false (terminal down)
  [PASS] health mt5_ready false
  [PASS] health quality UNKNOWN
  [PASS] candles fail with MT5_TERMINAL_UNAVAILABLE - MT5_TERMINAL_UNAVAILABLE
  [PASS] no fabricated candles
Scenario: version handshake
  [PASS] protocol mismatch -> 400 + code - http=400
  [PASS] schema mismatch -> 400 + code - http=400
  [PASS] compatible 1.x accepted - http=200
Scenario: loopback bind + SIGTERM shutdown
  [PASS] 0.0.0.0 bind refused - rc=2
  [PASS] listening on 127.0.0.1 only
  [PASS] SIGTERM exits cleanly - 0.020s

25/25 checks passed

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
(clean configure; build completes, 0 errors)

$ ctest --test-dir build --output-on-failure
1/12 BackendApiContractTests ............ Passed
2/12 BridgeRecoveryTests ................ Passed
3/12 ClosedBarCausalityTests ............ Passed
4/12 DataQualityPropagationTests ........ Passed
5/12 DecisionIdentityTests .............. Passed
6/12 DecisionPipelineIntegrationTests ... Passed
7/12 FrontendContractD1D9Tests .......... Passed
8/12 PackageSmokeTests .................. Passed
9/12 PersistenceRestartTests ............ Passed
10/12 PythonBridgeContractTests ......... Passed (0.35s, real-bridge handshake)
11/12 ShadowLifecycleTests .............. Passed
12/12 StartupWithoutCmdTests ............ Passed
100% tests passed out of 12

$ python3 bridge_service.py --host 0.0.0.0 --port 18799 --symbol XAUUSD
[ERROR] bridge must bind to loopback only (127.0.0.1)     # independent probe

$ git show --stat 9acbb45 --name-only | grep -E "^ ?src/|foundation|docs/|project-control/"
(no output — no production src/ or frozen-file changes)
```

---

## Verification steps

Adversarial: the default assumption was that the claim is overstated until each
item was reproduced independently.

1. **Reran the integration test myself.** `python3 tests/integration/test_bridge_t06.py`
   -> 25/25 pass. The test **boots the real `bridge_service.py`** as a subprocess
   (`subprocess.Popen([sys.executable, .../bridge_service.py, --host 127.0.0.1 ...])`)
   with a stub `MetaTrader5` on the path — not a re-implementation. Legitimate
   integration evidence.
2. **Reran the C++ regression myself.** Configured and built from scratch
   (cmake had to be installed in the container) and ran `ctest` -> 12/12 pass,
   including `PythonBridgeContractTests` (real-bridge handshake) at 0.35s.
3. **Loopback-only, independently.** Confirmed the test's `0.0.0.0 bind refused`
   check *and* ran an independent probe: `bridge_service.py --host 0.0.0.0`
   prints `bridge must bind to loopback only (127.0.0.1)`. Code inspection
   confirms `create_server` rejects any host other than `127.0.0.1`.
4. **Nine timeframes + authority.** Handshake lists 9; `M15` primary operational,
   `H4` primary structural — matches DEC-011 / V4-04.
5. **Closed-bar causality.** The test asserts the forming bar (index 0) is
   excluded and no bar reaches the forming bar; code uses `start_pos=1`
   semantics. Confirmed.
6. **No fabrication.** With MT5 unavailable, `/v1/candles` returns
   `MT5_TERMINAL_UNAVAILABLE`; health reports `mt5_ready=false`, quality
   `UNKNOWN`; no candles invented. Confirmed.
7. **Structured errors + versioning.** Protocol/schema mismatch -> HTTP 400 with
   a code; compatible 1.x accepted. Confirmed.
8. **Production protected.** `git show --name-only 9acbb45` shows **no** changes
   under `src/`, `docs/`, or `project-control/`. `src/foundation/` untouched.
   Confirmed.
9. **Live trading disabled.** T06 does not add any execution path; the Guardian
   remains the structural deny (`allowLiveExecution == false`). No change to that
   invariant. Confirmed.
10. **Defect claims D-1/D-2/D-3.** Read the `bridge_service.py` and
    `mt5_client.py` diffs: staleness (`MARKET_DATA_STALE`, `_STALE_INTERVAL_MULTIPLIER=3`),
    `min_count` guard (`INSUFFICIENT_HISTORY`), and bootstrap-error surfacing
    (`MT5_TERMINAL_UNAVAILABLE`) are all present and additive. `schemas.py` adds
    exactly the two new error codes. Confirmed.
11. **Zone check.** Listed the commit's files against Agent-C's declared zone
    (`src/api/, bridge/, packaging/`). See F1.

---

## Result

**PASS** on technical acceptance criteria (independently reproduced: 25/25
integration, 12/12 CTest, loopback refusal, no-fabrication, no production
changes). One process finding (F1) is recorded for the Lead.

---

## Notes

**F1 — Zone-boundary governance (process, not technical).**
Agent-C declared its zone as `src/api/, bridge/, packaging/` (README §A and
`coordination/agent-c/info.md`). Commit `9acbb45` also created files under
`tests/integration/` (`test_bridge_t06.py`, `fake_mt5/MetaTrader5.py`), which is
**outside** that declared zone and is not owned by any agent (Agent-A owns
`tests/features/`, Agent-B owns `tests/models/`; `tests/integration/` is
unassigned). The change is additive, correct, and non-destructive — it does not
touch another agent's zone and does not modify production — but it is an
unassigned-path write and should be ratified. Recommendation: the Lead either
(i) records a decision granting Agent-C `tests/integration/` for bridge tests, or
(ii) directs the tests to a ratified location. Agent-D does not decide this.

**F2 — test-placement ambiguity worth resolving once.** Bridge integration tests
under `tests/integration/` are not wired into `CMakeLists.txt` (which globs
`tests/*.cpp` only). They are run manually (`python3 tests/integration/test_bridge_t06.py`).
This is acceptable for now but means they are not part of the CTest gate; the
Lead should decide whether Python integration tests become a CI step.

**Scope/evidence limits (not defects).**
- No real MetaTrader5 or broker in the container; real XAUUSD candle retrieval is
  **not** exercised or claimed. The `fake_mt5` stub exercises the bridge's real
  HTTP surface and closed-bar logic, which is the correct substitute here.
- The staleness threshold (3× nominal bar duration) is a heuristic; it was
  exercised only via the stub's synthetic clock. It is reasonable but unvalidated
  against a real feed.
- `tasks.md` T06 row was set to REVIEW by Agent-C; Agent-D does not change it.
  On the Lead's confirmation of this PASS, T06 -> DONE.

**Independence statement.** Agent-D did not author any of the code under audit
and did not modify it. All findings above were produced by re-running the tests
and reading the diffs; no claim was accepted on assertion alone.
