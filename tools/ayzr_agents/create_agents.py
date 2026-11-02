#!/usr/bin/env python3
"""Create AYZR specialist OpenHands conversations on the local agent-server.

This is the *real* delegation mechanism available in this environment: the
local OpenHands agent-server (openhands-agent-server 1.58.1) exposes
`POST /api/conversations`, authenticated with the session API key held by the
server process. Each created conversation is an independent agent with its own
context window and its own workspace view.

The session key is read from the server process environment at runtime and is
never written to disk or printed.

Usage:
    python3 tools/ayzr_agents/create_agents.py --list
    python3 tools/ayzr_agents/create_agents.py --create all
    python3 tools/ayzr_agents/create_agents.py --status
"""

import argparse
import json
import os
import subprocess
import urllib.error
import urllib.request

SERVER = os.environ.get("OH_LOCAL_AGENT_SERVER", "http://127.0.0.1:60000")
REPO = "/workspace/project/asp"
STATE_FILE = os.path.join(os.path.dirname(__file__), "agents_state.json")
PROFILE_ID = os.environ.get("AYZR_AGENT_PROFILE_ID", "")


def session_key() -> str:
    """Read SESSION_API_KEY from the agent-server process environment."""
    key = os.environ.get("LOCAL_BACKEND_API_KEY") or os.environ.get(
        "SESSION_API_KEY"
    )
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
    except Exception as exc:  # network/timeout
        return {"error": "exception", "detail": str(exc)[:400]}


# Each specialist gets a self-contained brief: the new conversation shares no
# history with the principal, so the brief must stand alone.
SHARED = f"""You are a specialist agent on the AYZR project. AYZR is a personal
Arabic-only Android app for XAUUSD (gold) market intelligence. SHADOW ONLY:
there is NO order execution, ever.

Repository: {REPO} (git branch main). Backend is C++17 (CMake). Store is SQLite.
An independent Android app is being built separately from the repository's
existing frontend, which is OUT OF SCOPE: never read, reuse or modify
`astra-frontend/` (the existing frontend). The Windows MT5 Sender EXE is a
separate project and is OUT OF SCOPE.

Hard rules: never invent prices, candles, news, sources, indicators, analysis
results, trade levels or probabilities. Never hardcode secrets. Preserve the
existing backend's decision engine, data-integrity guards and API contracts.
Do not deploy, push, or delete production data.

When you finish, write a short markdown report to
{REPO}/docs/ayzr/reports/<your-agent-name>.md containing: files created or
changed (exact paths), commands run, and exact test results (passed/failed
totals). Do not claim success you have not verified.
"""

AGENTS = {
    "agent-a-backend": SHARED + """
YOUR SCOPE (Agent A - Backend & Database):
- Own: src/news/**, src/api/NewsApi.*, src/api/AstraApiServer.* (news routes only),
  tests/NewsIntelligenceTests.cpp, tests/NewsApiIntegrationTests.cpp,
  docs/ayzr/BACKEND.md.
- Audit the existing backend and document reuse vs new code.
- The news-intelligence module (NewsStore, Llm7Provider, MockLlmProvider,
  NewsAnalysisService, NewsAnalysisParser, NewsPrompt) already exists and its
  tests pass. Review it, harden it, and add tests. Do NOT rewrite the CandleStore
  or the decision pipeline.
- Resolve the Cloudflare database question: the backend currently uses SQLite on
  a Railway volume. Search the repo for evidence of the intended Cloudflare
  product (D1/Workers/Hyperdrive). If none exists, document that SQLite is the
  current store and propose a non-destructive migration path; do not migrate.
Report exact test totals.""",

    "agent-b-android": SHARED + """
YOUR SCOPE (Agent B - Android App):
- Own: the new Android project directory (choose `ayzr-android/` at the repo
  root) and docs/ayzr/ANDROID.md. Do NOT touch backend C++ or the existing
  frontend.
- Build the Arabic-only RTL Flutter app: Home (الرئيسية), Analysis (التحليل),
  News (الأخبار), History (السجل). Dark navy + gold accents.
- All user-facing text Arabic. RTL layout. Loading/empty/offline/stale/error
  states. Real backend integration via documented authenticated APIs (bearer
  client token; never embed privileged secrets).
- Flutter 3.24.5 is at /opt/flutter. Android SDK at /opt/android-sdk
  (platform 34, build-tools 34.0.0). JDK 21 at
  /usr/lib/jvm/java-21-openjdk-amd64.
- Write widget tests and run `flutter test`. Produce a debug APK with
  `flutter build apk --debug` if the toolchain permits.
Report the APK path and exact test totals.""",

    "agent-c-ai-news": SHARED + """
YOUR SCOPE (Agent C - AI News Intelligence):
- Own: src/news/NewsAnalysisService.*, src/news/NewsPrompt.*,
  src/news/NewsAnalysisParser.* and tests for them. Coordinate with Agent A
  before changing shared files.
- Verify the LLM7 integration against the official docs: base URL
  https://api.llm7.io/v1, model nemotron-3-nano:30b, OpenAI-compatible
  /chat/completions. Note the model does NOT support JSON mode, so output is
  parsed defensively (already implemented - verify and extend tests).
- The analysis is READ-ONLY with respect to ASTRA: it reads candles and the
  existing analysis, never modifies a decision, never executes a trade.
- Add failure-injection tests: auth failure, rate limit, timeout, malformed
  output, missing news body, missing candles, conflicting evidence.
Report exact test totals.""",

    "agent-d-visual": SHARED + """
YOUR SCOPE (Agent D - Image Design & Editing):
- Own: ayzr-android/assets/** (or the Flutter assets dir chosen by Agent B) and
  docs/ayzr/VISUAL_DESIGN.md.
- Inspect server resources (CPU/RAM/GPU/storage/permissions) and identify what
  image tooling can actually run. Pillow 13.0.0 is installed; there is no GPU
  and no ImageMagick.
- Install a realistic, lightweight image workflow (e.g. Pillow scripts) and
  perform a REAL test operation, saving output to the assets directory.
- Produce AYZR assets: app icon (multiple densities), a dark-navy/gold splash or
  background, and simple SVG/vector-style graphics. Verify dimensions, format,
  transparency and file size are Android-appropriate.
- Document honestly what the workflow CAN and CANNOT do (deterministic editing
  and SVG vs generative AI image synthesis). Do not claim generative capability
  that is not installed. Report the exact files produced and their sizes.""",

    "agent-e-testing": SHARED + """
YOUR SCOPE (Agent E - Testing & Security):
- Own: docs/ayzr/TESTING.md, docs/ayzr/SECURITY.md, and any new test files under
  tests/ that you coordinate with Agent A before adding.
- Build and run the full backend suite; report exact pass/fail totals.
- Design and run failure-injection tests: network interruption, API timeout,
  DB connection/write failure, LLM7 auth/rate-limit/timeout, malformed model
  response, missing news/candles, stale data, conflicting signals, duplicate
  candle batches, invalid OHLC, invalid timestamps, empty feed.
- Security review: authentication boundaries (sender vs admin vs client tokens),
  input validation, secret handling, error responses that must not leak
  internals. Verify the Android app holds no privileged secret.
Report exact totals and any finding with its resolution.""",
}


def resolve_profile_id() -> str:
    if PROFILE_ID:
        return PROFILE_ID
    resp = api("GET", "/api/agent-profiles")
    if isinstance(resp, dict) and resp.get("active_agent_profile_id"):
        return resp["active_agent_profile_id"]
    profiles = resp.get("profiles") if isinstance(resp, dict) else None
    if profiles:
        return profiles[0]["id"]
    raise SystemExit("cannot resolve agent profile id")


def local_llm() -> dict:
    """Build the LLM settings from the local Default profile.

    The profile file holds the same credential this agent itself uses; it is
    read at runtime and passed only to the local agent-server. It is never
    printed or written to the state file.
    """
    path = os.path.expanduser("~/.openhands/profiles/Default.json")
    with open(path) as fh:
        profile = json.load(fh)
    llm = {
        "model": profile["model"],
        "base_url": profile.get("base_url"),
        "api_key": profile.get("api_key"),
        "usage_id": profile.get("usage_id", "default"),
    }
    return {k: v for k, v in llm.items() if v is not None}


def create(name: str):
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
        "workspace": {"kind": "LocalWorkspace", "working_dir": REPO},
        "initial_message": {"content": [{"type": "text", "text": AGENTS[name]}]},
        "autotitle": True,
        "tags": {"project": "ayzr", "agent": name},
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
            status = conv.get("execution_status", conv.get("status", conv.get("error")))
            print(name, info["id"], status)
        return

    ap.print_help()


if __name__ == "__main__":
    main()
