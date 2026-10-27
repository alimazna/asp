# AYZR — System Architecture

AYZR is a personal, Arabic-only Android app for XAUUSD market intelligence on top
of the existing ASTRA backend. **Shadow only: there is no order execution, ever.**

## 1. Component map

```
 ┌──────────────────────────┐        ┌───────────────────────────────────────┐
 │ Windows MT5 Sender EXE   │        │  AYZR Android app (Flutter, Arabic RTL)│
 │ (SEPARATE PROJECT, OOS)  │        │  الرئيسية | التحليل | الأخبار | السجل │
 └────────────┬─────────────┘        └───────────────┬───────────────────────┘
              │ POST /api/v1/candles/ingest          │ GET /api/v1/news
              │ Bearer <sender token>                │ GET /api/v1/news/analyze
              ▼                                      │ GET /api/v1/history
 ┌──────────────────────────────────────────────────┴───────────────────────┐
 │                     ASTRA backend (C++17, astra_server)                   │
 │                                                                          │
 │  AstraApiServer ── routes + auth (sender | admin | client) + rate limit   │
 │    ├── /candles/ingest, /sync/latest      (sender token)                  │
 │    ├── /admin/candles                      (admin token)                  │
 │    ├── /candles, /analysis/*, /context/*   (public read, frozen v1)       │
 │    └── /news, /news/analyze, /history      (client token)  ← NEW          │
 │                                                                          │
 │  News intelligence (NEW, additive)                                       │
 │    NewsStore ── durable SQLite (news_articles, news_analyses)            │
 │    NewsAnalysisService ── provider-neutral; bounded retries              │
 │    ILlmProvider ← Llm7Provider (libcurl HTTPS) | MockLlmProvider (tests)  │
 │    NewsAnalysisParser ── defensive validation (no JSON mode)             │
 │    NewsPrompt ── real candles + real ASTRA analysis, read-only           │
 │                                                                          │
 │  CandleStore ── SQLite, grid-alignment quarantine (UNCHANGED)            │
 │  AuraRuntime / decision pipeline (UNCHANGED, read-only to news layer)    │
 └───────────────┬───────────────────────────────┬──────────────────────────┘
                 │ SQLite (WAL)                  │ HTTPS (libcurl, TLS verified)
                 ▼                               ▼
     Railway volume /opt/astra/data        LLM7 api.llm7.io/v1
     (candles.sqlite, news.sqlite)         model nemotron-3-nano:30b
```

## 2. Design principles

- **Reuse before rebuild.** The existing `CandleStore`, decision pipeline, frozen
  v1 read contract, auth model and rate limiter are reused unchanged. The news
  module is strictly additive.
- **Read-only AI.** The news layer reads candles and the existing ASTRA analysis;
  it never mutates a decision, never executes anything, and is rendered beside
  (not over) the original ASTRA result.
- **No fabrication.** Every field the backend cannot source is `null`/`UNKNOWN`,
  never a plausible number. The frontend and API both withhold grid-misaligned
  data rather than serving it.
- **Fail honest.** A provider failure yields `status:"unavailable"`; a schema
  violation yields `status:"invalid_model_output"`; neither is ever rendered as a
  recommendation.
- **Provider-neutral.** Business logic depends on `ILlmProvider`; LLM7 is one
  implementation selected by configuration.

## 3. Data model (news, new)

`news_articles` — PK `(source, article_id)`; unique `dedup_key` for cross-source
dedup; `published_at`, `collected_at`, `body_available`, `gold_relevance`.

`news_analyses` — PK `article_id` (upsert = supersede, so history keeps the latest
validated result per article); status, Arabic summary/facts/implications,
direction, optional levels, ASTRA agreement, provider/model ids, timestamps.

Candle identity (existing, unchanged): `(source_id, symbol, timeframe, open_time)`.

## 4. Authentication boundaries

| Credential | Grants | Cannot |
|------------|--------|--------|
| `ASTRA_SENDER_TOKEN` | candle ingest + sync | read news, purge data |
| `ASTRA_ADMIN_TOKEN` | admin quarantine inspect/purge | write candles |
| `ASTRA_CLIENT_TOKEN` | read news/analyze/history | write candles, purge |

The Android app holds **only** the client token (read-only). No database
credential, sender token, admin token, or LLM7 token is ever embedded in the APK.

## 5. Failure isolation

- LLM7 down → news list and candles still serve; analyze returns `unavailable`.
- News store down → server logs a warning; other routes keep working.
- Candle store stale/empty → analysis uses an honest empty candle summary.
- ASTRA analysis unavailable → the news layer reports `UNAVAILABLE` for it.
- Network loss in the app → explicit offline/stale state, never a stale value
  presented as current.

## 6. Deployment topology

- Backend: Railway (`railway.toml`, `Dockerfile`), SQLite on volume
  `/opt/astra/data`, health `/api/v1/livez`, readiness `/api/v1/readyz`.
- Frontend (existing, out of scope): Cloudflare Pages `astra-dia.pages.dev`.
- No Cloudflare database exists; see `CLOUDFLARE_DATABASE.md`.
