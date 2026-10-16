# AUDIT — T32 Frontend Data Honesty & Cloudflare Pages Deploy Blocker (2026-10-16)

Status: **code complete and verified locally; production frontend redeploy BLOCKED on credentials**
Author: agent (autonomous)
Scope: continued from `AUDIT-T31-live-deployment.md`. Frontend honesty work plus
an attempt to publish the new frontend build to Cloudflare Pages.

Every "VERIFIED" line below was reproduced from this environment. Anything not
reproducible here is marked **NOT VERIFIED** explicitly.

---

## 1. Starting state

`AUDIT-T31` recorded the stack as live: Railway backend
`https://astra-server-production.up.railway.app`, Cloudflare Pages frontend
`https://astra-dia.pages.dev`, Windows sender release `sender-v0.3.0`.

The live Pages site, however, still served the **original single-page build**.
Verified by comparing the deployed bundle against the repository build:

```
$ curl -s https://astra-dia.pages.dev/assets/app.js | grep -c "views.forecast\|candleBatchIntegrity"
0
$ curl -s https://astra-dia.pages.dev/assets/app.js | wc -c
10910
```

The repository build of `assets/app.js` is 1679 lines and contains the new
multi-page navigation, so **the deployed frontend was one build behind the
repository**. This session's work is the content of that pending build plus
further data-honesty fixes.

## 2. The central correctness problem: non-MT5 M15 rows presented as real

The live store holds 4 M15 rows that are **not** MT5 data:

- price jumps `1901 → 2001` between consecutive bars,
- open times are not M15-aligned (`1792068271`, `1792068276`).

They were written during earlier ingest testing. The backend derives its M15
`observed` flag, its M15 watermark, and its M15 freshness from these rows, so a
naive frontend reports a synthetic timestamp as a genuine market reading. This
directly violates the mission rule that no synthetic data may surface as real.

The backend cannot be edited here (it is deployed from an image built in a prior
session), so the honesty gate is enforced in the frontend from the raw candle
batch.

## 3. Fix implemented

`assets/lib.js` gained `candleBatchIntegrity(bars, timeframe)`:

- every open time must be an exact multiple of the timeframe length,
- consecutive open times must be spaced exactly one timeframe apart.

`assets/app.js` applies it to the M15 batch on every view that could otherwise
present it as real:

| View | Before | After |
| --- | --- | --- |
| Market Chart | bars drawn and labelled "Real closed candles" | warning banner; not labelled real |
| Market Forecast | M15 watermark shown as a reading | withheld with a stated reason |
| Market Data — "Last closed candle (M15)" | synthetic timestamp | `not available` + reason |
| Market Data — "Timeframes observed" | `1 / 9` (counted M15) | `0 / 9`, M15 excluded, reason given |
| Market Data — coverage matrix | M15 shown as an observed state | M15 `UNTRUSTED` |
| Market Data — watermark table | M15 row showed a FRESH timestamp | `withheld (non-MT5 rows)` / `untrusted` |
| Dashboard — "Timeframe coverage" | `1 / 9 observed` | `0 / 9 observed (M15 excluded: non-MT5 rows)` |
| Dashboard — coverage matrix | M15 shown as observed | M15 `UNTRUSTED` |

The gate is data-driven, not hardcoded: if genuine MT5 M15 bars arrive, the
integrity check passes and every view shows them normally.

## 4. Other fixes in this build

- **Polling.** One `setInterval` refreshes only `/health` and `/system/state`
  while the tab is visible; interval is user-configurable and `0` disables it.
  The earlier polling flood is not reproducible.
- **Secret gate.** The previous gate scanned for the words "secret"/"token" and
  false-positived on the UI's own prose (`Secrets in this bundle: none`).
  Replaced by `scripts/secret-scan.js`, which flags credential-shaped values
  (`ghu_`, `cf_`, JWT, AWS keys, 32+ char high-entropy strings), unknown
  `config.js` keys, and a production build whose `BACKEND_BASE` is empty.
- **Brand.** Deep navy `#0A1622` and the geometric mark per
  `docs/brand/ASTRA_VISUAL_IDENTITY.md`.

## 5. Verification (VERIFIED)

### Frontend

```
$ node --test tests/*.test.js
ℹ pass 24
ℹ fail 0
```

```
$ BACKEND_BASE=https://astra-server-production.up.railway.app node build.js
$ node scripts/secret-scan.js dist
secret gate passed: 6 file(s) scanned, no credential-shaped content
```

Gate negative controls:

- planted `Bearer 12345…` in `dist/` → exit 1
- empty `BACKEND_BASE` production build → exit 1

Rendered pages, driven through a local same-origin proxy to the live Railway
backend (so CORS cannot mask a failure):

- Dashboard: `Timeframe coverage 0 / 9 observed (M15 excluded: non-MT5 rows)`,
  M15 cell `UNTRUSTED`.
- Market Data: `Timeframes observed 0 / 9`, `Last closed candle (M15) not
  available — withheld: stored M15 rows are not MT5-aligned`, watermark row
  `withheld (non-MT5 rows)` / `untrusted`.
- Forecast: `M15 is reported as having no trustworthy closed bar…`
- Trade Plan: `Plan available: not available`, `Proposal: no decision evaluated
  yet` — no fabricated levels.

### Backend (live, read-only)

| Endpoint | Result |
| --- | --- |
| `/api/v1/health` | 200 |
| `/api/v1/system/state` | 200 |
| `/api/v1/timeframes` | 200 |
| `/api/v1/analysis/latest` | 200 |
| `/api/v1/context/latest` | 200 |
| `/api/v1/bridge/status` | 200 |
| `/api/v1/shadow/outcomes` | 200 |
| `/api/v1/risk/latest` | 200 |
| `/api/v1/candles?tf=M15&limit=5` | 200 |
| `POST /api/v1/candles/ingest` (no token) | **401** |
| CORS `Origin: https://astra-dia.pages.dev` | 200 + origin echoed |
| CORS `Origin: http://localhost:12013` | **403** |

CORS allowlisting is strict; loopback origins are refused.

### Windows sender

```
$ python3 -m unittest discover -s tests -v
Ran 17 tests in 0.671s
OK
```

`AstraSender.exe` from `sender-v0.3.0` was re-verified in the prior session:
11,985,360 bytes, SHA-256
`f5f8639b6e20f98605a3334e61dd11ccbd9b7a3834d7638c1cf2dca5c96ae716`.

## 6. Blocker — Cloudflare Pages deploy

The deploy workflow ran and **failed at the wrangler step**:

```
In a non-interactive environment, it's necessary to set a CLOUDFLARE_API_TOKEN
environment variable for wrangler to work.
```

CI now fails earlier, at a dedicated preflight step, with an actionable message
(run `38877693932`), which is direct evidence that the repository secrets are
unset:

```
deploy  Run unit tests                              # pass 24
deploy  Assert the bundle carries no secret         secret gate passed: 6 file(s) scanned
deploy  Preflight — Cloudflare credentials present  ::error title=Cloudflare secrets missing
```

Diagnosis, in order:

1. No `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`, or `CF_*` variable exists
   in this environment.
2. No wrangler OAuth session exists (`~/.config/.wrangler` holds only logs and
   metrics).
3. The token available here is a GitHub App token with no `x-oauth-scopes`
   header; `gh secret set` fails with **403 Resource not accessible by
   integration**, so the repository secrets cannot be created or read from here.
4. Conversation history contains no Cloudflare credential value.
5. Cloudflare Pages is **direct-upload only** for this project — no preview
   alias responds:

```
deploy-astra-stack.astra-dia.pages.dev     HTTP 404
astra-frontend.astra-dia.pages.dev         HTTP 404
main.astra-dia.pages.dev                   HTTP 404
```

So a Git push cannot trigger a Pages build. Deployment requires a
`CLOUDFLARE_API_TOKEN` with the **Cloudflare Pages: Edit** permission plus the
account ID.

**Consequence:** `https://astra-dia.pages.dev` continues to serve the previous
build. The corrected frontend is committed and pushed to `deploy/astra-stack`
but **NOT VERIFIED** in production.

### Unblock procedure

Repository → Settings → Secrets and variables → Actions → add:

- `CLOUDFLARE_API_TOKEN` — token scoped to *Cloudflare Pages: Edit*
- `CLOUDFLARE_ACCOUNT_ID` — the account owning the `astra` Pages project

then re-run the `Deploy static frontend to Cloudflare Pages` workflow. The gate
steps (unit tests, build, secret scan) already pass.

Alternatively, deploy once from a shell without touching repository settings:

```bash
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... ./astra-frontend/scripts/deploy-pages.sh
```

That script runs the unit tests, builds with the production `BACKEND_BASE`, runs
the secret gate, and only then calls `wrangler pages deploy`. It refuses to run
when either variable is empty.

## 7. Costs and limits

No resource was provisioned this session. No plan was upgraded. Railway trial
and Cloudflare free tier are unchanged.

## 8. Remaining items

- **Production frontend redeploy — NOT VERIFIED** (blocker above).
- **Live MT5 verification on the owner's Windows PC — NOT VERIFIED.** No MT5
  terminal is reachable from this environment; the sender's MT5 path is covered
  by the repo's `fake_mt5` fixture and 17 unit tests.
- **Windows release-attach CI step** still returns 403; the release asset exists
  regardless. Needs repository-settings permission, not a code change.
- The M15 rows remain in the production store. They are now withheld
  everywhere, but deleting them would need either a store-level endpoint or a
  Railway shell, neither of which is available here.
