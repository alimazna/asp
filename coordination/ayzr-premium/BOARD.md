# AYZR Premium Evolution — Team Board

Owner: principal agent (this conversation). Branch: `feat/ayzr-premium-evolution`.
Baseline backup: `backup/ayzr-before-premium-evolution-20261102-093104`
@ `b1722e5ba277a42bbdd2a15260f20520ed25b06a` (verified on GitHub).

## Environment capability disclosure (verified, not assumed)

| Capability | Status | Evidence |
|---|---|---|
| Multi-agent delegation (OpenHands conversations) | **UNAVAILABLE** | `api.all-hands.dev` unreachable (curl 000); local agent-server on :8001 refused (000); production runtime `/api/conversations` → **401**. |
| Git worktrees / branches | AVAILABLE | used for the backup branch |
| Node 24.21 / npm 11.21 | AVAILABLE | `node --version` |
| Python 3.13 + Pillow | AVAILABLE | installed this session |
| C++17 toolchain + cmake | AVAILABLE | 49/49 test binaries built |
| Playwright | NOT INSTALLED | `import playwright` fails |

Consequence: the work is executed as **staged specialist roles played by the
principal agent**, each with its own memory file under `memory/agents/`, not as
separate running conversations. This is disclosed rather than simulated.

## Roles and file ownership

| # | Role | Owns | Must not touch |
|---|---|---|---|
| 1 | Chief Architect / Director | `coordination/ayzr-premium/**`, `docs/ayzr/**` | engine math |
| 2 | Git / Infra / Dev Env | `.github/**`, `docker/**`, tooling installs | app logic |
| 3 | Quant Research & Validation | `research/ayzr_quant_lab/**` | `src/**` |
| 4 | Strategy Engines | `src/ayzr/engines/**` | `src/runtime/**`, `src/scoring/**` (ASTRA) |
| 5 | Central Orchestrator | `src/ayzr/ecosystem/CentralOrchestrator.*`, `DecisionLedger.*` | engine math |
| 6 | BiQuote / Market Data | `tools/ayzr_agents/**`, data contracts | ASTRA |
| 7 | UI/UX Design Director | `docs/ayzr/VISUAL_DESIGN.md`, design tokens | backend |
| 8 | Premium Web Frontend | `tools/ayzr_observatory/**` | backend |
| 9 | Web Integration & Security | `tools/ayzr_observatory/serve*.py` | engine math |
| 10 | Android Flutter | `ayzr-android/**` | backend |
| 11 | QA Automation | `tests/**`, `tools/**/tests/**` | product code |
| 12 | Reliability / Perf / Security Audit | `docs/ayzr/backend-hardening/**` | product code |

## Absolute constraints (from SHARED_MEMORY.md §0)

- XAUUSD only. **SHADOW ONLY** — no order execution, ever.
- **ASTRA CORE frozen.** Fingerprint
  `aee61fa9225b4fc6a3ba6dd7a2d82640227ec07179cd592e42b93e526889c480` (15 cases)
  must stay identical. Gate: `python3 research/ayzr_quant_lab/tests/test_astra_protection.py`.
- Never invent prices/candles/news/indicators/probabilities. Unavailable ⇒ `N/A`.
- No live LLM calls in tests. `ASTRA_LLM_PROVIDER=mock`.
- Never commit secrets. The previously-exposed LLM7 key is compromised: do not use.

## Baseline (measured 2026-11-02, before any change)

- ASTRA CORE protection gate: **PASS — ASTRA CORE unchanged**.
- C++ suite: **49/49 binaries pass, 0 fail**.
- Quant lab: 5/5 pass when run with `PYTHONPATH=.` (`test_lab_registry.py` needs it).
- Reference image: `docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg` (1280×698).
  The literal name in the brief ("AYZR Arabic Trading Intelligence Dashboard")
  does **not** exist in the repo; this is the only brand reference present.
- Android screens: **14** real `lib/ui/screens/*.dart` files (documents disagreed).

## Phase board

| Phase | Role | Status | Tests | Blockers |
|---|---|---|---|---|
| 0 Backup checkpoint | 2 | DONE | remote sha == local sha | none |
| 1 Team/env | 1,2 | DONE | tooling probed | no multi-agent platform |
| 2 Full audit | 1 | IN PROGRESS | baseline green | none |
| 3 Arch map | 1 | TODO | | |
| 4 Visual design | 7 | TODO | | |
| 5 Design system | 7,8 | TODO | | |
| 6 Web redesign | 8 | TODO | | |
| 7 Android | 10 | TODO | | |
| 8 Engines | 4,11 | TODO | | |
| 9 Orchestrator | 5,11 | TODO | | |
| 10 BiQuote/data | 6 | TODO | | |
| 11 Session/regime | 3,11 | TODO | | |
| 12 Quant lab | 3 | TODO | | |
| 13 Ledger/replay | 5,11 | TODO | | |
| 14 E2E | 11 | TODO | | |
| 15 Security/perf | 9,12 | TODO | | |
| 16 Device/browser | 10,8 | TODO | | |
| 17 Acceptance | 1 | TODO | | |

Status vocabulary: PASS / PARTIAL / FAIL / BLOCKED / NOT TESTED.
