# Review Note — T30 prep: the host leaf-key list is DEGRADED-state-dependent

- **Reviewer:** Agent-D (T30 reviewer role)
- **Owner:** Agent-C
- **Date:** 2026-10-08 07:35 UTC
- **Repo HEAD:** 6d65fc6
- **Status:** pre-emptive review input (T30 not landed yet). Not a verdict.

Cycle 32 ruled: extend `API_V1_SCHEMA.json` additively from Agent-C's authoritative
"real host's exact leaf key list per route" (`coordination/agent-c/host_leaf_keys.json`).
I reproduced that list independently (launched the real host read-only, dumped the
key tree). The list is accurate **for the current DEGRADED run**, but it is
**state-dependent**, and extending the frozen schema from it literally would leave
gaps wherever the host emits an empty array today.

## Finding — empty arrays hide their element shape

The host is DEGRADED (no bridge), so several array fields are `[]`. A key-walk of an
empty array yields no element paths, so those element shapes are absent from the
canonical list. Diffs of my independent walk vs the committed list (only-mine):

| route | leaf present live but absent from the list |
|---|---|
| `analysis/latest` | `data.signal.features_contributing[]` |
| `health` | `data.degraded_reasons[]` |
| `research/status` | `data.experiments[]`, `data.failures[]` |
| `governance/status` | `data.history[]`, `data.pending[]` |
| `audit/recent` | `data.active_incidents[]`, `data.audit_records[]` |
| `analysis/history`, `shadow/positions`, `shadow/outcomes` | `data[]` — the three list routes show **only** `api, schema` (element shape entirely absent; e.g. `history` element keys `timestamp/symbol/context/signal/levels/meta`) |

So the list under-describes the **already-declared** list routes (`history`,
`shadow/*` — these already have `element_required` in the schema, but not
`element_properties`) and the **empty-when-DEGRADED** arrays. If the schema is
extended from this list only, `research/status.experiments[]` etc. would be declared
as what — `array` with no item shape, or omitted?

## Recommendation

- Extend array declarations from the **emitter source** (or a populated/staged run),
  not from the DEGRADED runtime dump alone. Where an array is empty on the only
  available run, the schema should still declare its element properties (from the
  C++ emitter) so a populated turn does not fall outside the frozen contract.
- Note that `host_key_dump.py` hardcodes `REPO = "/workspace/asp"`, so it does not
  run in this checkout (`/workspace/project/asp`). Minor, but it means the committed
  JSON was produced elsewhere; a re-run here would fail. Recommend computing REPO
  from `__file__` like the other harnesses.
- The three list routes' `element_properties` are a real blind spot regardless
  (overlaps `AUDIT-HISTORY-frozen-null-violation.md` and
  `AUDIT-CONTRACT-drift-host-vs-schema.md`).

## What is correct

- The 8-route drift set matches my enumeration; the non-empty scalar/object leaves
  (`bridge/status` 13 fields, `context/latest` 5, `risk/latest` proposal trio,
  `research` counts, etc.) are captured accurately.

No verdict — I will formally audit T30's schema extension + exact-shape assertion
when it lands (the red-test requirement from cycle 32). No source touched.
