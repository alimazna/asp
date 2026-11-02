-- AYZR candle store on Supabase (Postgres).
--
-- Purpose: durable, off-Railway persistence of the closed XAUUSD candles the
-- feeder collects. This is a pure archive: the ASTRA engine keeps reading its
-- local SQLite store, and nothing here feeds back into a decision. If Supabase
-- is down the engine is unaffected.
--
-- Apply once, by hand, in the Supabase dashboard: SQL Editor -> New query ->
-- paste this file -> Run. The REST API cannot create tables, so this step is
-- manual and deliberate (no DDL credential is ever given to the feeder).
--
-- Columns mirror src/persistence/CandleStore.cpp exactly, so a row means the
-- same thing on both sides. The primary key is the same candle identity the
-- engine uses: (source_id, symbol, timeframe, open_time). Upserts are therefore
-- idempotent - re-sending a batch can never duplicate a candle.

CREATE TABLE IF NOT EXISTS candles (
  source_id    TEXT             NOT NULL,
  symbol       TEXT             NOT NULL,
  timeframe    TEXT             NOT NULL,
  open_time    BIGINT           NOT NULL,   -- epoch seconds, UTC
  open         DOUBLE PRECISION NOT NULL,
  high         DOUBLE PRECISION NOT NULL,
  low          DOUBLE PRECISION NOT NULL,
  close        DOUBLE PRECISION NOT NULL,
  tick_volume  INTEGER          NOT NULL DEFAULT 0,
  spread       INTEGER          NOT NULL DEFAULT 0,
  real_volume  INTEGER          NOT NULL DEFAULT 0,
  received_at  BIGINT           NOT NULL,   -- epoch seconds, UTC (archive time)
  PRIMARY KEY (source_id, symbol, timeframe, open_time)
);

-- The app and any ad-hoc read query filter by symbol+timeframe and order by
-- open_time, which is the access pattern this index serves.
CREATE INDEX IF NOT EXISTS idx_candles_lookup
  ON candles (symbol, timeframe, open_time DESC);

-- --- Access control --------------------------------------------------------
-- The table is written and read only by server-side components that hold the
-- secret key (the feeder and the backend). No mobile client ever touches it:
-- the Android app reads through the backend API. Row Level Security is enabled
-- with NO policy for anon/authenticated roles, so the publishable key can
-- neither read nor write this table. Only the secret key (which bypasses RLS)
-- has access. This is the least-privilege default for an archive table.
ALTER TABLE candles ENABLE ROW LEVEL SECURITY;

-- Belt-and-braces: even if a policy is added later by mistake, revoke the
-- default grants from the public API roles.
REVOKE ALL ON candles FROM anon;
REVOKE ALL ON candles FROM authenticated;
