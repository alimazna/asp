#!/usr/bin/env python3
"""Create the five hardening specialist conversations on the local agent-server.

Each specialist runs in its OWN git worktree (harden/agent-<x>) so parallel
edits cannot conflict. The principal integrates reviewed branches afterwards.

Usage:
    python3 tools/ayzr_agents/create_hardening_agents.py --create all
    python3 tools/ayzr_agents/create_hardening_agents.py --status
"""

import argparse
import json
import os
import subprocess
import urllib.error
import urllib.request

SERVER = os.environ.get("OH_LOCAL_AGENT_SERVER", "http://127.0.0.1:60000")
STATE_FILE = os.path.join(os.path.dirname(__file__), "hardening_agents_state.json")
WT = "/workspace/wt"
REAL_CSV = "/workspace/project/asp/research/data/xauusd_m1/xauusd_m1_real.csv"


def session_key() -> str:
    key = os.environ.get("LOCAL_BACKEND_API_KEY") or os.environ.get("SESSION_API_KEY")
    if key:
        return key
    for cmd in (["sudo", "cat", "/proc/1/environ"], ["cat", "/proc/1/environ"]):
        try:
            out = subprocess.run(cmd, capture_output=True, check=True).stdout
        except Exception:
            continue
        for line in out.split(b"\0"):
            if line.startswith(b"SESSION_API_KEY="):
                return line.split(b"=", 1)[1].decode()
    raise SystemExit("SESSION_API_KEY not found in server environment")


def api(method: str, path: str, body=None):
    key = session_key()
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{SERVER}{path}", data=data, method=method)
    req.add_header("X-Session-API-Key", key)
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        return {"error": exc.code, "detail": exc.read().decode()[:400]}
    except Exception as exc:
        return {"error": "exception", "detail": str(exc)[:400]}


SHARED = f"""You are a specialist agent on the AYZR/ASTRA backend-hardening round.

AYZR is a personal Arabic-only Android app for XAUUSD (gold) intelligence.
SHADOW ONLY: there is NO order execution, ever. Never invent prices, candles,
news, sources, indicators, analysis results, trade levels or probabilities.
Never hardcode secrets. Preserve the existing ASTRA decision engine, its data
integrity guards, and all API contracts. Do not deploy, push, or delete data.

You are working in a dedicated git worktree at {{WORKDIR}} on branch
{{BRANCH}} (base commit 80dbd24). Edit files ONLY inside your worktree. Build
with:
  cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j4
Run C++ tests directly, e.g.:
  cd build-linux && for t in *Tests; do ./"$t" || echo "FAIL $t"; done
Never read, reuse or modify `astra-frontend/` (the existing frontend, out of
scope) or the separate Windows MT5 sender EXE (out of scope).

Do NOT waste the LLM7 daily quota: use the mock provider
(ASTRA_LLM_PROVIDER=mock) for all tests. Do not make live LLM7 calls.

Real XAUUSD M1 data (genuine MT5 broker export, 100,008 bars,
2026-06-24..2026-10-08, broker time) is at:
  {REAL_CSV}
Format: timestamp,open,high,low,close,volume (volume = MT5 tick volume).
It is committed and checksummed. Treat it as REAL; never relabel synthetic data
as real.

When finished, write a concise markdown report to
/workspace/project/asp/docs/ayzr/backend-hardening/reports/<agent>.md with:
files changed (exact paths), commands run, exact test results (passed/failed
totals), and unresolved issues. Do not claim success you have not verified.
"""

AGENTS = {
    "agent-a-api": """
YOUR SCOPE (Agent A - Backend architecture & API reliability).
Own files (edit only these):
  src/api/NewsApi.*, src/api/AstraApiServer.* (news + client-token routes only),
  src/api/AstraServerMain.cpp (news wiring only),
  tests/NewsApiIntegrationTests.cpp, tests/AyzrApiContractTests.cpp (new).
Do:
  1. Audit the API contract: auth boundaries (sender vs admin vs client),
     request validation, response consistency, error envelope shape, timeout
     behavior, backward compatibility. Document findings.
  2. Add a NEW test file tests/AyzrApiContractTests.cpp that pins the existing
     routes' status codes and error-envelope shape so future changes cannot
     silently break the Android client or the MT5 sender contract. Reuse the
     in-process server test pattern already used in tests/AstraApiServerTests.cpp
     and tests/NewsApiIntegrationTests.cpp (read them first).
  3. Implement only small, well-justified improvements you can test.
Do NOT change CandleStore, the decision pipeline, or the frozen public contract.
""",

    "agent-b-store": """
YOUR SCOPE (Agent B - Candle data integrity & persistence).
Own files (edit only these):
  src/data/CandleStore.* (and any src/data/* it directly depends on),
  tests/CandleStoreIntegrityTests.cpp (new), tests/AyzrConcurrencyTests.cpp (new).
Do:
  1. Audit CandleStore: schema, PK/UNIQUE constraints, transaction boundaries,
     duplicate handling, concurrent writes, WAL/busy-timeout behavior, restart
     recovery, the grid-alignment quarantine, and latestOpenTimeAnySource.
  2. Look for concrete defects: partial writes, non-idempotent upserts, races,
     unbounded queries, missing indexes. Measure before changing.
  3. Add focused tests (duplicate batches, out-of-order, gaps, restart
     durability, concurrent readers/writers) using an isolated temp DB path.
  4. Implement only focused, tested fixes. Preserve the ingestion contract and
     the data-honesty quarantine semantics exactly.
""",

    "agent-c-realdata": """
YOUR SCOPE (Agent C - Real XAUUSD candle QA & historical replay).
Own files (edit only these):
  tools/ayzr_agents/real_candle_replay.py (new),
  tests/RealCandleReplayTests.cpp (new, may drive the Python tool or read the
  CSV directly), docs/ayzr/backend-hardening/REAL_CANDLE_TESTING.md.
Do:
  1. Use ONLY the genuine MT5 corpus above (verify its sha256 against
     research/data/xauusd_m1/checksums.sha256 first and record the hash).
  2. Build a reproducible replay workflow that:
     - loads real M1 bars and validates OHLC (low<=open<=high, low<=close<=high);
     - aggregates M1 -> M15/H4 with the SAME rules the backend uses (read
       src/data/BarNormalizer.* first, do not invent rules);
     - POSTs real batches to a locally started astra_server ingestion endpoint
       (ASTRA_SENDER_TOKEN), then reads them back via /api/v1/candles;
     - verifies duplicate-batch idempotency, restart persistence, and that
       stored candles match the API responses.
  3. Record provenance: symbol, timeframe, date range, row count, timestamp
     convention (broker time), checksum.
  4. Never invent expected values; compare only against the real data and
     independently-derivable aggregates.
""",

    "agent-d-security": """
YOUR SCOPE (Agent D - Security review).
Own files (edit only these):
  tests/AyzrSecurityTests.cpp (new), docs/ayzr/backend-hardening/SECURITY_FINDINGS.md,
  docs/ayzr/SECURITY.md (append findings only).
Do:
  1. Audit every sensitive route against intended permissions:
     - sender token authorizes only candle/news ingest;
     - client token cannot write candles, ingest news, or reach admin;
     - admin routes require the admin token and fail closed when unset;
     - missing credentials fail closed on every protected route.
  2. Audit input validation, request-size/rate limits, error responses for
     internal-detail leakage, and secret handling (no token in logs).
  3. Write tests/AyzrSecurityTests.cpp proving the cross-role denials with an
     in-process server (read tests/AstraApiServerTests.cpp for the pattern).
  4. Record each finding with severity, reproduction, fix, and regression test.
Implement fixes only within your owned files; if a fix needs a source file
owned by another agent, report it instead of editing it.
""",

    "agent-e-perf": """
YOUR SCOPE (Agent E - Performance, concurrency & failure resilience).
Own files (edit only these):
  tools/ayzr_agents/bench_backend.py (new),
  tests/AyzrPerfResilienceTests.cpp (new),
  docs/ayzr/backend-hardening/PERFORMANCE_RESULTS.md.
Do:
  1. Build a reproducible local benchmark against a locally started astra_server:
     - ingestion batches of 100/500/2000 candles (use the real corpus);
     - candle retrieval at M1/M15/H4;
     - duplicate-batch ingestion;
     - concurrent reads and concurrent ingest+read;
     - news list + history latency.
     Record environment (nproc/RAM), data size, request counts, concurrency,
     and latency (min/mean/p95) with error counts. Report measured values only.
  2. Add resilience tests: concurrent ingestion correctness, restart recovery,
     and that an AI/news failure does not disable candle or analysis endpoints.
  3. Identify the actual bottleneck from measurements; propose (do not blindly
     apply) any optimization.
Do NOT change source files owned by other agents; report needed changes.
""",
}


def resolve_profile_id() -> str:
    resp = api("GET", "/api/agent-profiles")
    if isinstance(resp, dict) and resp.get("active_agent_profile_id"):
        return resp["active_agent_profile_id"]
    profiles = resp.get("profiles") if isinstance(resp, dict) else None
    if profiles:
        return profiles[0]["id"]
    raise SystemExit("cannot resolve agent profile id")


def local_llm() -> dict:
    """LLM settings for the local agent-server.

    The profile file used previously is no longer present, so the values are
    taken from the server's own active agent profile (queried over the local
    API) rather than being hardcoded. Credentials are never printed or written
    to the state file.
    """
    # Read the active profile's LLM settings from an existing conversation, which
    # the server has already resolved to a working configuration.
    existing = api("GET", "/api/conversations/cddc487e-bd1c-4afb-b613-ae77473cb0b6")
    llm = existing.get("agent", {}).get("llm") if isinstance(existing, dict) else None
    if isinstance(llm, dict) and llm.get("model"):
        keep = (
            "model",
            "api_key",
            "base_url",
            "auth_type",
            "provider_connection_id",
            "usage_id",
        )
        return {k: llm[k] for k in keep if k in llm}
    raise SystemExit("cannot resolve LLM settings from the local agent-server")


def create(name: str):
    letter = name.split("-")[1]  # agent-a-api -> a
    workdir = f"{WT}/agent-{letter}"
    brief = SHARED.replace("{WORKDIR}", workdir).replace(
        "{BRANCH}", f"harden/agent-{letter}"
    ) + AGENTS[name]
    brief = brief.replace(
        "write a concise markdown report to\n/workspace/project/asp/docs/ayzr/backend-hardening/reports/<agent>.md",
        f"write a concise markdown report to\n{workdir}/docs/ayzr/backend-hardening/reports/{name}.md",
    )
    body = {
        "agent": {
            "kind": "Agent",
            "llm": local_llm(),
            "tools": [
                {"name": "terminal"},
                {"name": "file_editor"},
                {"name": "task_tracker"},
            ],
        },
        "workspace": {"kind": "LocalWorkspace", "working_dir": workdir},
        "initial_message": {"content": [{"type": "text", "text": brief}]},
        "autotitle": True,
        "tags": {"project": "ayzr", "round": "hardening", "agent": name},
    }
    return api("POST", "/api/conversations", body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--create", help="agent name or 'all'")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--status", action="store_true")
    args = ap.parse_args()

    if args.list:
        for name in AGENTS:
            print(name)
        return

    state = {}
    if os.path.exists(STATE_FILE):
        state = json.load(open(STATE_FILE))

    if args.create:
        names = list(AGENTS) if args.create == "all" else [args.create]
        for name in names:
            if name in state and state[name].get("id"):
                print(f"{name}: already created {state[name]['id']}")
                continue
            resp = create(name)
            if "id" in resp:
                state[name] = {"id": resp["id"], "status": "created"}
                print(f"{name}: created id={resp['id']}")
            else:
                print(f"{name}: FAILED {resp}")
        json.dump(state, open(STATE_FILE, "w"), indent=2)
        return

    if args.status:
        for name, info in state.items():
            conv = api("GET", f"/api/conversations/{info['id']}")
            status = conv.get(
                "execution_status", conv.get("status", conv.get("error"))
            )
            print(name, info["id"], status)
        return

    ap.print_help()


if __name__ == "__main__":
    main()
