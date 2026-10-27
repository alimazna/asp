# AYZR — Cloudflare Database Investigation & Resolution

## Question

The target architecture (master prompt §14) describes:

> Market data is persisted through the configured Cloudflare database integration.

This document records what the repository **actually** contains and resolves the
discrepancy between that description and the implemented backend.

## Findings (evidence-based)

A repository-wide search for Cloudflare database products was run:

```bash
grep -rin "cloudflare\|d1\b\|hyperdrive\|workers\.dev" \
  --include="*.md" --include="*.toml" --include="*.json" \
  --include="*.yml" --include="*.yaml" --include="*.cpp" \
  --include="*.h" --include="*.sh" . | grep -v astra-frontend/
```

Result: **no Cloudflare database product (D1, Workers KV, Hyperdrive, R2, Durable
Objects) is referenced anywhere in the backend.** The only Cloudflare references
are:

1. `railway.toml` — the comment `ASTRA_ALLOWED_ORIGINS - comma-separated
   Cloudflare/browser origins`, i.e. Cloudflare **Pages** as a browser origin.
2. `AUDIT_REPORTS/AUDIT-T32-...md` — Cloudflare **Pages** static hosting and the
   `CLOUDFLARE_API_TOKEN` used to deploy the frontend.
3. The existing frontend (excluded from this work) — Cloudflare Pages is its host.

There is no `wrangler.toml`, no `d1` binding, no `wrangler.jsonc`, and no
Cloudflare database client in the codebase.

## What the backend actually uses

- **Store:** SQLite via `sqlite3` (`src/persistence/CandleStore.cpp`,
  `src/news/NewsStore.cpp`).
- **Location:** `ASTRA_DATA_DIR` (default `./data`); in production a Railway
  volume mounted at `/opt/astra/data` (`railway.toml` → `deploy.volumes.astra-data`).
- **WAL journaling + busy timeout**, one transaction per batch.

So the current persistence layer is **SQLite on a Railway volume**, not a
Cloudflare database. This is a deliberate, working design: a single-writer
shadow-only analytics store with no concurrent multi-tenant access.

## Resolution

Per the master prompt's own rule — *"Do not automatically assume Cloudflare D1,
Workers, Hyperdrive, or another product without evidence"* and *"Reuse existing
working database and deployment adapters"* — the correct action is:

1. **Do not migrate.** SQLite on the Railway volume is the working, tested store.
   Replacing it now would be a destructive migration with no functional benefit
   and would risk the existing data-integrity guarantees.
2. **Document the truth:** the persistence layer is SQLite; Cloudflare's only role
   is hosting the (out-of-scope) frontend.
3. **Provide a non-destructive migration path** if a Cloudflare database is later
   required (see below), so the decision stays open without being forced.

## Non-destructive path to a Cloudflare database (if required later)

If the owner later wants market data in a Cloudflare database, the least
disruptive options are, in order of preference:

1. **Keep SQLite as the write store; expose it read-only through the backend.**
   No change needed — the Android app already reads through the backend API and
   never touches the database directly. This satisfies "the Android app must not
   connect directly to the database".
2. **Cloudflare Hyperdrive in front of a Postgres instance.** Hyperdrive is a
   connection pooler, not a database; it would front Postgres. This requires a
   Postgres instance and a store adapter rewrite.
3. **Cloudflare D1** (SQLite-compatible, HTTP API). D1 is the closest match to the
   current SQLite schema, so a `D1Store` implementing the same interface as
   `CandleStore`/`NewsStore` could be added behind a feature flag, dual-writing
   until verified, then cut over. This is the recommended path **if** a Cloudflare
   database is mandated, because the SQL is nearly portable.

Any of these must be implemented as an **additive adapter** behind the existing
interfaces, never as an in-place destructive migration, and only after a verified
backup of the Railway volume.

## What is required from the owner to proceed with a Cloudflare DB

Only if the owner decides a Cloudflare database is required:

- The intended Cloudflare product (D1 / Hyperdrive+Postgres / other) — this is a
  product decision the repository cannot answer.
- `CLOUDFLARE_ACCOUNT_ID` and a scoped API token.
- For D1: the database name/id; for Hyperdrive: the Postgres connection string.

None of these are needed for the current, working SQLite deployment, and none are
requested now.

## Status

- **Verified:** no Cloudflare database exists in the backend; SQLite on a Railway
  volume is the store; all 28 backend tests pass against it.
- **Not done (deliberately):** no migration, no schema replacement.
- **Blocked on a product decision, not a credential:** if a Cloudflare DB is
  wanted, the product choice must come from the owner.
