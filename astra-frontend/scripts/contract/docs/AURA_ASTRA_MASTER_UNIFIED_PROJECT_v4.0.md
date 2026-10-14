# AURA / ASTRA — MASTER UNIFIED PROJECT V4.0

## Current Canonical Control & Implementation Specification

**Document status:** MASTER / CANONICAL / RESEARCH-FIRST / IMPLEMENTATION-CONTROLLED  
**Version:** 4.0  
**Date:** 2026-10-06  
**Technical project:** AURA  
**Product / desktop brand:** ASTRA  
**Asset:** XAUUSD only  
**Primary mode:** SHADOW  
**Backend-first build:** DeepSeek v4.1 Flash / OpenHands  
**Frontend after backend approval:** Alpha  

---

# V4-00. CANONICAL PRECEDENCE

This V4 document is the active authority for the clean rebuild.

The archived V3 Master remains preserved in:

`docs/archive/XAUUSD_SOVEREIGN_MASTER_UNIFIED_PROJECT_v3.0.original.md`

The archived Python guide remains preserved in:

`docs/archive/Python_MetaTrader5_Reference.original.md`

Where archived material conflicts with this V4 document, V4 wins for the new build.

---

# V4-01. PRODUCT IDENTITY

```text
TECHNICAL PROJECT: AURA
PRODUCT / DESKTOP BRAND: ASTRA
ASSET: XAUUSD ONLY
PRIMARY MODE: SHADOW
RUNTIME CORE: LOCAL C++
MARKET-DATA ACQUISITION: BUNDLED PYTHON + METATRADER5 BRIDGE
BRIDGE PROCESS: SEPARATE CHILD PROCESS, MANAGED BY AURA
BRIDGE TRANSPORT: LOOPBACK-ONLY LOCAL JSON API
RESEARCH / REPLAY: PYTHON-COMPATIBLE ENVIRONMENT
PRIMARY HUMAN CONTROL CENTER: ASTRA DESKTOP APPLICATION
TELEGRAM: OPTIONAL AUXILIARY LAYER
PERSISTENT LEDGER: HISTORICAL SOURCE OF TRUTH
PROFITABILITY: UNPROVEN
PROBABILITY CALIBRATION: NOT ESTABLISHED
BROKER VALIDATION: REQUIRED
LIVE AUTOMATION: NOT INITIAL OBJECTIVE
```

---

# V4-02. END-TO-END ARCHITECTURE

```text
BROKER / VALETAX
      |
      v
MT5 TERMINAL (user-installed, logged-in)
      |
      v
BUNDLED PYTHON MT5 BRIDGE
      |
      | 127.0.0.1 / loopback JSON API
      v
AURA C++ DATA INGESTION
      |
      v
DATA VALIDATION
      |
      v
TIMEFRAME STATE
      |
      v
FEATURES
      |
      v
STRUCTURE
      |
      v
REGIME
      |
      v
STRATEGY ELIGIBILITY
      |
      v
SIGNAL
      |
      v
SIGNAL VALIDATION
      |
      v
SCORE / CONFIDENCE / CALIBRATED PROBABILITY WHEN ESTABLISHED
      |
      v
MACRO / EVENT / MARKET-QUALITY GATES
      |
      v
RISK PROPOSAL
      |
      v
SHADOW EXECUTION
      |
      v
SIMULATED POSITION LIFECYCLE
      |
      v
OUTCOME
      |
      v
PERSISTENCE / AUDIT / RESEARCH LEDGER
```

The old `MT5/MQL5 adapters -> C++` candle-ingestion boundary is **replaced** by the Python bridge for this clean rebuild. The C++ runtime and decision chain are preserved.

---

# V4-03. PYTHON BRIDGE RESPONSIBILITY

The Python bridge is responsible for market-data acquisition and broker/terminal observation at the MT5 boundary.

It MUST:
- connect to the local MT5 Terminal through the official MetaTrader5 Python package;
- resolve the broker's XAUUSD symbol representation;
- read the nine canonical timeframe streams;
- return closed bars for decision-grade snapshots;
- expose current quotes/ticks where available for market-quality/execution context;
- expose symbol specification where available;
- expose broker/server/terminal identity;
- expose health/freshness state;
- return explicit errors rather than fabricated data;
- remain loopback-only;
- be supervised by AURA.

It is not the strategy brain, risk authority, evaluator, or governance authority.

---

# V4-04. NINE TIMEFRAME STREAMS

```text
M1
M5
M15
M30
H1
H4
D1
W1
MN1
```

Authority remains:

```text
M15 = PRIMARY OPERATIONAL / SETUP TIMEFRAME
H4  = PRIMARY STRUCTURAL AUTHORITY
H1/M30 = INTERMEDIATE CONTEXT
M5/M1/TICK = EXECUTION / MICROSTRUCTURE CONTEXT
D1/W1/MN1 = LONG-HORIZON CONTEXT
```

---

# V4-05. CLOSED-BAR / CAUSALITY CONTRACT

For decision-grade candle input:
- current/open bar is not treated as a completed decision bar;
- the bridge request uses closed-only semantics;
- the ingestion layer validates bar identity and timestamp ordering;
- no future information may enter a decision;
- no hidden repainting;
- the persistent record keeps source/broker identity and timestamps needed for audit.

A decision should be reproducible from information available at its decision time.

---

# V4-06. LOCALHOST / PROCESS MODEL

Python is **bundled with the application distribution** but is **not compiled into the C++ process**.

```text
ASTRA / AURA host
    |
    +--> starts Python MT5 bridge child process
    |
    +--> checks bridge handshake
    |
    +--> monitors bridge health
    |
    +--> requests market data over 127.0.0.1
    |
    +--> restarts only according to bounded recovery policy
    |
    +--> shuts down child cleanly
```

The bridge must not bind to a public interface.

The application must not require the developer to open CMD or run Python manually.

---

# V4-07. WINDOWS STARTUP CONTRACT

The packaged application MUST be able to start by double-click.

It MUST:
1. resolve its own installation/executable directory rather than trusting the current working directory;
2. locate bundled runtime assets from that root;
3. validate configuration;
4. start the bridge automatically;
5. complete the bridge protocol handshake;
6. verify terminal availability;
7. enter the appropriate system/service state;
8. surface actionable failure information in logs/status contracts;
9. cleanly terminate child processes on shutdown.

A developer shell, PATH modification, or manual Python command is not a product requirement.

If MT5 is not installed/open/logged-in, the product must report the dependency state instead of silently failing.

---

# V4-08. DATA / SERVICE STATES

Service states:

```text
STARTING
ONLINE
DEGRADED
OFFLINE
RECOVERING
PAUSED
BLOCKED
ERROR
```

System operating modes:

```text
STARTING
RECOVERY
NORMAL
DEGRADED
SHADOW
PAUSED
MANUAL
EMERGENCY
HALTED
```

These are different concepts.

---

# V4-09. DATA QUALITY

```text
VALID
DEGRADED
INVALID
UNKNOWN
STALE
MISSING
OUT_OF_ORDER
DUPLICATE
INCOMPLETE
```

`UNKNOWN` is not neutral.

Data-quality state must propagate to capability eligibility and decision gating.

---

# V4-10. RESILIENCE

Canonical behavior remains:

```text
FAILURE
 -> ISOLATE
 -> MARK FAILED/DEGRADED/OFFLINE/RECOVERING/PAUSED/BLOCKED/ERROR
 -> DISABLE ONLY DEPENDENT CAPABILITIES
 -> KEEP SAFE INDEPENDENT CAPABILITIES RUNNING
 -> ALERT HUMAN
 -> RECOVER WHEN POSSIBLE
```

Individual timeframe failure must identify its capability impact. In particular:
- M15 failure blocks M15-triggered decisions;
- H4 failure blocks H4-dependent structural authority;
- M5/M1 degradation disables dependent microstructure/execution-context features;
- stale tick/quote state blocks new execution proposals where required.

---

# V4-11. SHADOW BOUNDARY

The initial system is shadow-only.

```text
Decision
 -> ShadowCommand
 -> SimulatedFill
 -> ShadowPosition
 -> ShadowExit
 -> Outcome
 -> Reconciliation
```

The backend MUST NOT place live broker orders in this build.

Shadow should model the relevant lifecycle including costs/slippage assumptions, partial-fill behavior where applicable, and reconciliation. Assumptions must remain distinguishable from observed broker reality.

---

# V4-12. PERSISTENCE / AUDIT

Critical state must survive restart, including as applicable:
- adapter/bridge health;
- last processed bar by timeframe;
- decision IDs;
- signals;
- score/confidence/probability status;
- risk proposals;
- shadow positions/fills/outcomes;
- configuration/strategy/protocol versions;
- system mode;
- errors and alerts;
- research/experiment IDs.

Audit remains append-only.

Historical records are not rewritten merely to make the current system look correct.

---

# V4-13. DECISION IDENTITY

```text
DecisionID = Hash(
    symbol,
    trigger timeframe,
    closed bar ID,
    strategy version,
    configuration version
)
```

Idempotency is mandatory at relevant boundaries.

---

# V4-14. SCORE / CONFIDENCE / PROBABILITY

```text
SCORE != PROBABILITY
CONFIDENCE != GUARANTEE
```

A calibrated probability remains unavailable until a proper calibration and validation program establishes it.

The runtime may report an explicit `N/A / NOT_ESTABLISHED` state instead of fabricating a probability.

---

# V4-15. SELF-LEARNING / RESEARCH / EVOLUTION

Research and learning remain algorithmic/statistical and human-governed.

Allowed:
- observation;
- diagnosis;
- knowledge acquisition;
- contradiction tracking;
- hypothesis creation;
- bounded experiments;
- candidate generation/comparison;
- validation;
- stress/regression;
- checkpointing;
- reporting.

Forbidden:
- direct production mutation;
- evaluator modification;
- holdout unlocking;
- erasing history;
- provenance rewriting;
- criteria changes after results;
- auto-promotion;
- Guardian modification.

---

# V4-16. BACKEND / FRONTEND SEPARATION

Backend owns:
- market data;
- validation;
- state;
- analysis/decision chain;
- risk;
- shadow;
- persistence;
- research;
- governance;
- health/recovery;
- stable data/status APIs.

Frontend owns:
- ASTRA visual presentation;
- navigation;
- charts and dashboards;
- user interaction;
- status visualization;
- human approval controls through approved backend APIs.

Frontend does not become the source of truth.

---

# V4-17. BUILD SEQUENCE

```text
NEW REPOSITORY
   -> BACKEND IMPLEMENTATION (DeepSeek / OpenHands)
   -> BACKEND REVIEW
   -> BUILD / TEST / PACKAGING ACCEPTANCE
   -> BACKEND HANDOFF FREEZE
   -> FRONTEND IMPLEMENTATION (Alpha)
   -> UI REVIEW / INTEGRATION
```

This sequence is mandatory for the clean rebuild.

---

# V4-18. BACKEND PHASES

```text
PHASE 0   Immutable Foundations
PHASE 0.5 Resilience & Graceful Degradation
PHASE 1   Python MT5 Bridge + Deterministic Runtime/Data
PHASE 2   Observation & Outcomes
PHASE 3   Self-Learning
PHASE 4   Research Plane
PHASE 5   Evolution
PHASE 6   Validation
PHASE 7   Governance
PHASE 8   Operating Window & Recovery
PHASE 10  Auxiliary Telegram Backend
PHASE 12  Packaging / Backend-Frontend Contract
```

Desktop UI remains deferred to the Alpha frontend phase.

---

# V4-19. MVP ENGINEERING AUTHORITY

The initial analytical authority remains centered on:

```text
XAUUSD
M15
H4
M5
M1
basic higher-timeframe context
data validation
regime
one strategy family
risk proposal
shadow execution
shadow ledger
health monitoring
replay
```

Full nine-stream adapters may be implemented before all analytical authority is activated.

---

# V4-20. BACKEND DEFINITION OF DONE

The backend is ready for Alpha only when the completion gate in:

`docs/architecture/BACKEND_DONE_DEFINITION.md`

is satisfied and the review evidence is recorded in `project-control/TEST_LOG.md`.

---

# V4-21. HISTORICAL SOURCE MATERIAL

The previous V3 Master remains useful as architecture history and preserved research material. The older Python guide remains useful as the implementation origin for the bridge. Neither historical file is the new execution authority when it conflicts with V4.

---

# V4-22. FINAL SAFETY STATEMENT

Architecture does not prove profitability.

A working bridge does not prove broker validation.

A score does not prove calibrated probability.

A shadow result does not prove live execution equivalence.

No live automation is part of the initial build.
