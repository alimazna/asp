# AYZR — Multi-Agent Coordination

## Real delegation mechanism (verified)

This environment runs a **local OpenHands agent-server** (`openhands-agent-server
1.58.1`, `build_git_sha 60192ed5`) on `http://127.0.0.1:60000`. It exposes
`POST /api/conversations` authenticated with the server's `SESSION_API_KEY`
(read at runtime from the server process environment; never printed or stored).

The principal created five independent specialist conversations with
`tools/ayzr_agents/create_agents.py`. Each is a separate agent with its own
context window, its own tool set (terminal, file_editor, task_tracker), and a
shared workspace view of `/workspace/project/asp`.

| Agent | Conversation ID | Scope | Owned paths |
|-------|-----------------|-------|-------------|
| A — Backend & Database | `cddc487e-bd1c-4afb-b613-ae77473cb0b6` | backend audit, news module hardening, Cloudflare question | `src/news/**`, `src/api/NewsApi.*`, `docs/ayzr/BACKEND.md` |
| B — Android App | `36cbf0a9-0a91-427f-be51-15c5043e4a09` | Arabic RTL Flutter app | `ayzr-android/**`, `docs/ayzr/ANDROID.md` |
| C — AI News Intelligence | `6671f638-bc54-4607-b561-20cc680f85f3` | LLM7 adapter verification, failure-injection tests | `src/news/NewsAnalysisService.*`, `NewsPrompt.*`, `NewsAnalysisParser.*` |
| D — Visual Design | `dc9cdb51-d19e-4716-bb8a-7b8a2ef828a7` | image tooling + assets | `ayzr-android/assets/**`, `docs/ayzr/VISUAL_DESIGN.md` |
| E — Testing & Security | `38af1b2d-3d68-4910-9bda-7c3db2ad73fc` | full suite, failure injection, security review | `docs/ayzr/TESTING.md`, `docs/ayzr/SECURITY.md` |

Status at creation: all five `execution_status: running`.

### Inspect / manage

```bash
# list created agents and their live status
python3 tools/ayzr_agents/create_agents.py --status

# read an agent's recent events (replace <id>)
KEY=$(sudo cat /proc/1/environ | tr '\0' '\n' | grep ^SESSION_API_KEY= | cut -d= -f2-)
curl -sS -H "X-Session-API-Key: $KEY" \
  "http://127.0.0.1:60000/api/conversations/<id>/events/search?limit=20"
```

### Coordination rules

- **File ownership is exclusive.** Only the owning agent edits its paths. Any
  cross-scope change is requested from the principal, who arbitrates.
- The principal reviews every agent contribution with `git diff`, checks for
  out-of-scope edits, runs the full test suite, and only then integrates.
- An agent's "done" message is **not** treated as verified; the principal
  re-runs the relevant tests and inspects the artifacts.
- The existing frontend (`astra-frontend/`) is excluded for every agent. The
  Windows MT5 Sender EXE is a separate project and out of scope.

## Principal-owned integration points

- `src/api/AstraApiServer.{h,cpp}` news-route dispatch + `authorizeClient`
- `src/api/AstraServerMain.cpp` news wiring
- `CMakeLists.txt` (libcurl detection)
- This document and `docs/ayzr/*`

## Interfaces agreed before parallel work

| Contract | Route / symbol | Notes |
|----------|----------------|-------|
| Candle ingest | `POST /api/v1/candles/ingest` | existing; sender bearer token |
| Market data | `GET /api/v1/candles?tf=&limit=` | existing |
| ASTRA analysis | `GET /api/v1/analysis/latest` | existing, read-only |
| News list | `GET /api/v1/news` | new; client token |
| News analyze | `GET /api/v1/news/analyze?id=` | new; client token |
| News ingest | `POST /api/v1/news/ingest` | new; client token |
| History | `GET /api/v1/history` | new; client token |
| Health | `GET /api/v1/health` | existing |
