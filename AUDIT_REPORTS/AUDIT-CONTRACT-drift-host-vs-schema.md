# Audit Report — Freeze drift: real host emits fields the frozen schema does not declare

- **Auditor:** Agent-D (Verification & Audit)
- **Date:** 2026-10-08 07:05 UTC
- **Repo HEAD:** 3639e81
- **Severity:** **material** — the frozen contract's authoritative artifact
  (`API_V1_SCHEMA.json`) under-specifies what the real host actually serves, and
  every current suite is blind to it.
- **Zones:** schema/freeze (Lead-owned), host emission (Agent-C). Agent-D reports
  only; makes no change.

---

## Context

The contract doc states: *"machine-readable contract: `docs/architecture/
API_V1_SCHEMA.json` … served by `aura::BackendFacade` … The schema file is
authoritative; if this prose and the schema disagree, the schema wins,"* and
*"Within `v1`, only additive changes are permitted … Every additive change must
(a) update `API_V1_SCHEMA.json`, (b) keep `scripts/mock_api.py --check` green,
and (c) be noted here."*

I compared the **real host's** live payloads (driven by the T13 real-host
harness, which I re-ran read-only) against the schema's declared key set, at leaf
level, across all 15 routes.

## Evidence

The schema declares no `additionalProperties`, and both validators
(`scripts/mock_api.validate_envelope`, `src/models/contract_checker.validate_envelope`)
visit only *declared* keys — so undeclared fields are invisible to every check.
The mock's generators emit **exactly** the declared set; the fixtures match the
mock. The real host does not:

| route | undeclared keys the real host emits | mock |
|---|---|---|
| `GET /api/v1/context/latest` | `context.{regime,h4_bias,m15_trigger,mtf_agreement,volatility_state}` (schema says only `context: {"type":"object"}` — **no properties**) | emits them too, also undeclared |
| `GET /api/v1/timeframes` | `capability_impact[]`, `decision_grade` (element lacks it in schema while the snapshot element has it) | omits |
| `GET /api/v1/timeframes/{tf}/snapshot` | `capability_impact`, `freshness`, `has_closed_bar`, `last_successful_update` | omits the first |
| `GET /api/v1/bridge/status` | 13 keys: `bridge_symbol, broker, initialized, last_error, last_successful_request, managed_by_application, mt5_ready_live, observed, package_available, process_state, requires_manual_cmd, resolved_symbol, server` | omits all 13 |
| `GET /api/v1/risk/latest` | `proposal, proposal_available, proposal_reason` | omits |
| `GET /api/v1/research/status` | `experiment_count, experiments, failure_count, failures` | omits |
| `GET /api/v1/governance/status` | `history, pending, pending_count` | omits |
| `GET /api/v1/audit/recent` | `active_incidents, audit_records, audit_stream_size` | omits |
| `GET /api/v1/system/state` | embedded `data.api`, `data.schema` (duplicated inside `data`) | omits |

**8 of 15 routes** carry real host fields absent from the frozen schema. Some are
significant surfaces (bridge/status exposes broker/MT5 session state; risk/latest
exposes the risk proposal the frontend guide calls load-bearing).

## Why the suites miss it

- T24 (`test_e2e_frozen_v1.py`) drives the **mock**, whose shape is a *subset* of
  the declared set — structurally clean by construction.
- T13-real-host (`test_e2e_real_host_t13.py`) and T16
  (`test_contract_t16.py`) validate the host against the schema, but the schema
  validator only checks declared keys, so extra keys pass.
- `contract_checker` (T23) and `mock_api.validate_envelope` mirror the same
  permissive dialect (no `additionalProperties`), by design.
- The fixture↔live key-path equality in T24 is mock-vs-fixture, so it too stays
  green.

So the freeze can drift on the production host without any red test — the exact
class of gap this contract is meant to prevent.

## Recommendation (Lead ruling needed)

1. **Decide the authority direction.** Per "implementation is the truth" (D-1
   ruling) and "additive changes must update the schema," the likely intent is:
   extend `API_V1_SCHEMA.json` **additively** to declare the host's real fields
   (bridge/risk/timeframes/context/research/governance/audit), then update the
   mock + fixtures to match. Alternatively, rule that the extra fields are
   internal and have Agent-C stop emitting them. Either is valid; leaving it
   undeclared is not.
2. **Add an exact-shape assertion** to the real-host harness: every key emitted by
   the host is declared in the schema (host-keys ⊆ schema-keys). This closes the
   blind spot so the freeze cannot silently drift again. Low-risk, in-zone for
   Agent-C / Lead.
3. `context/latest`'s `context` object should declare its properties (it is
   `{"type":"object"}` today, i.e. unconstrained).

## Result

**Finding — needs Lead ruling.** Not a code defect in any one task per se, but a
contract-integrity gap that every green suite hides. Flagging per the Agent-D
mandate ("do not accept gaps the suites miss"). No source touched by Agent-D.

## Verification notes

- Reproduced at 3639e81 by (a) running `test_e2e_real_host_t13.py` (37/37 green),
  then (b) enumerating live leaf key names per route and diffing against the
  schema's declared names. Scripts inline, uncommitted.
- Same finding would surface on any host build; it is data-independent (present in
  the DEGRADED no-data posture).
