"""Tests for ayzr-web: real data plumbing, signal maths, and failure states.

Network calls are stubbed so the suite is deterministic and never depends on a
live market feed or the local model. The stubs return real captured shapes from
biquote so the parsing paths are exercised for real.
"""
import os, sys, asyncio, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import pytest
from ayzr_web import datasource as ds, signals
from ayzr_web.datasource import Candle, DataUnavailable


def mk(n=120, start=2000.0, step=1.0):
    """A deterministic rising series with valid OHLC relationships."""
    out = []
    for i in range(n):
        o = start + i * step
        c = o + step * 0.6
        out.append(Candle(1_700_000_000 + i * 900, o, max(o, c) + 0.5,
                          min(o, c) - 0.5, c, 100.0, "M15"))
    return out


# --- OHLC validation ---------------------------------------------------------

def test_valid_ohlc_accepts_normal_bar():
    assert ds._valid_ohlc(10.0, 12.0, 9.0, 11.0)

def test_valid_ohlc_rejects_high_below_body():
    assert not ds._valid_ohlc(10.0, 10.5, 9.0, 11.0)

def test_valid_ohlc_rejects_low_above_body():
    assert not ds._valid_ohlc(10.0, 12.0, 10.5, 11.0)

def test_valid_ohlc_rejects_non_positive():
    assert not ds._valid_ohlc(0.0, 12.0, 9.0, 11.0)


# --- grid alignment ----------------------------------------------------------

def test_alignment_accepts_exact_multiple():
    assert ds.is_aligned(1_700_000_000 - (1_700_000_000 % 900), "M15")

def test_alignment_rejects_offset_bar():
    assert not ds.is_aligned(1_700_000_123, "M15")

def test_daily_is_exempt_from_alignment():
    # Broker offsets shift D1 by whole hours; that is not a data defect.
    assert ds.is_aligned(1_700_000_123, "D1")

def test_unknown_timeframe_is_not_aligned():
    assert not ds.is_aligned(1_700_000_000, "X9")


# --- indicators --------------------------------------------------------------

def test_ema_tracks_a_rising_series():
    vals = [float(i) for i in range(1, 60)]
    e = signals.ema(vals, 20)
    assert e and e[-1] > e[0]

def test_ema_needs_enough_data():
    assert signals.ema([1.0, 2.0], 20) == []

def test_rsi_is_high_when_price_only_rises():
    r = signals.rsi([float(i) for i in range(1, 40)], 14)
    assert r is not None and r > 90

def test_rsi_is_low_when_price_only_falls():
    r = signals.rsi([float(i) for i in range(40, 1, -1)], 14)
    assert r is not None and r < 10

def test_rsi_needs_enough_data():
    assert signals.rsi([1.0, 2.0, 3.0], 14) is None

def test_atr_is_positive_and_scales_with_range():
    tight = signals.atr(mk(60, step=0.1), 14)
    wide = signals.atr(mk(60, step=5.0), 14)
    assert tight and wide and wide > tight

def test_swings_detects_a_clear_peak():
    bars = mk(30, step=1.0)
    bars[15].high = 9999.0
    highs, _ = signals.swings(bars)
    assert 9999.0 in highs


# --- proposal ----------------------------------------------------------------

def test_proposal_withholds_on_too_few_candles():
    p = signals.build_proposal(mk(20), "M15")
    assert p.status == "insufficient_data"
    assert p.direction == "no_trade"
    assert p.entry is None

def test_proposal_levels_are_ordered_correctly_for_long():
    p = signals.build_proposal(mk(120), "M15")
    if p.direction == "bullish" and p.entry:
        assert p.stop_loss < p.entry < p.take_profit

def test_proposal_levels_are_ordered_correctly_for_short():
    p = signals.build_proposal(mk(120, start=5000.0, step=-1.0), "M15")
    if p.direction == "bearish" and p.entry:
        assert p.take_profit < p.entry < p.stop_loss

def test_risk_reward_meets_minimum_when_trade_is_offered():
    p = signals.build_proposal(mk(120), "M15")
    if p.direction in ("bullish", "bearish") and p.risk_reward:
        assert p.risk_reward >= 1.2

def test_confidence_is_a_small_agreement_count_not_a_probability():
    p = signals.build_proposal(mk(120), "M15")
    assert 0 <= p.confidence <= 3

def test_proposal_carries_a_disclaimer():
    d = signals.build_proposal(mk(120), "M15").to_dict()
    assert "ليس توصية" in d["disclaimer_ar"]

def test_proposal_never_invents_numbers_when_data_is_thin():
    d = signals.build_proposal(mk(10), "M15").to_dict()
    assert d["entry"] is None and d["stop_loss"] is None

def test_flat_market_yields_no_trade_or_sideways():
    flat = [Candle(1_700_000_000 + i * 900, 2000.0, 2000.5, 1999.5, 2000.0, 10.0, "M15")
            for i in range(120)]
    p = signals.build_proposal(flat, "M15")
    assert p.direction in ("sideways", "no_trade")


# --- datasource failure handling --------------------------------------------

def test_unsupported_timeframe_raises():
    with pytest.raises(DataUnavailable):
        asyncio.run(ds.fetch_candles("X9"))

def test_w1_is_not_offered_because_biquote_returns_nothing():
    assert "W1" not in ds.supported_timeframes()

def test_supported_timeframes_are_the_documented_eight():
    assert set(ds.supported_timeframes()) == {
        "M1", "M5", "M15", "M30", "H1", "H4", "D1", "MN1"}

def test_fetch_raises_when_both_sources_fail(monkeypatch):
    async def boom(*a, **k):
        raise DataUnavailable("stubbed failure")
    monkeypatch.setattr(ds, "_fetch_biquote", boom)
    monkeypatch.setattr(ds, "_fetch_yahoo", boom)
    with pytest.raises(DataUnavailable):
        asyncio.run(ds.fetch_candles("M15"))

def test_fetch_falls_back_to_second_source(monkeypatch):
    async def boom(*a, **k):
        raise DataUnavailable("biquote down")
    async def ok(tf, limit, timeout_s):
        return mk(80)
    monkeypatch.setattr(ds, "_fetch_biquote", boom)
    monkeypatch.setattr(ds, "_fetch_yahoo", ok)
    bars = asyncio.run(ds.fetch_candles("M15"))
    assert len(bars) == 80

def test_interval_substitution_is_rejected(monkeypatch):
    """If biquote answers with a different interval we must not serve it."""
    class FakeResp:
        status_code = 200
        def json(self):
            return {"interval": "1h", "bars": [
                {"openTime": "2026-11-02T18:00:00Z", "open": 1.0, "high": 2.0,
                 "low": 0.5, "close": 1.5, "isOpen": False}]}
    class FakeClient:
        async def __aenter__(self): return self
        async def __aexit__(self, *a): return False
        async def get(self, *a, **k): return FakeResp()
    monkeypatch.setattr(ds.httpx, "AsyncClient", lambda **k: FakeClient())
    with pytest.raises(DataUnavailable):
        asyncio.run(ds._fetch_biquote("M15", 10, 5.0))

def test_open_bar_is_dropped_when_flagged(monkeypatch):
    """biquote's isOpen flag must be honoured, not guessed."""
    bars_payload = [
        {"openTime": "2026-11-02T18:00:00Z", "open": 10.0, "high": 12.0,
         "low": 9.0, "close": 11.0, "isOpen": True},
        {"openTime": "2026-11-02T17:45:00Z", "open": 9.0, "high": 11.0,
         "low": 8.0, "close": 10.0, "isOpen": False},
    ]
    class FakeResp:
        status_code = 200
        def json(self): return {"interval": "15m", "bars": bars_payload}
    class FakeClient:
        async def __aenter__(self): return self
        async def __aexit__(self, *a): return False
        async def get(self, *a, **k): return FakeResp()
    monkeypatch.setattr(ds.httpx, "AsyncClient", lambda **k: FakeClient())
    out = asyncio.run(ds._fetch_biquote("M15", 10, 5.0))
    assert len(out) == 1 and out[0].close == 10.0

def test_corrupt_bar_is_dropped(monkeypatch):
    class FakeResp:
        status_code = 200
        def json(self):
            return {"interval": "15m", "bars": [
                {"openTime": "2026-11-02T17:45:00Z", "open": 10.0, "high": 9.0,
                 "low": 8.0, "close": 10.5, "isOpen": False},
                {"openTime": "2026-11-02T17:30:00Z", "open": 9.0, "high": 11.0,
                 "low": 8.0, "close": 10.0, "isOpen": False}]}
    class FakeClient:
        async def __aenter__(self): return self
        async def __aexit__(self, *a): return False
        async def get(self, *a, **k): return FakeResp()
    monkeypatch.setattr(ds.httpx, "AsyncClient", lambda **k: FakeClient())
    out = asyncio.run(ds._fetch_biquote("M15", 10, 5.0))
    assert len(out) == 1

def test_iso_parser_handles_zulu_and_fractional_seconds():
    assert ds._parse_iso("2026-11-02T18:00:00Z") == ds._parse_iso("2026-11-02T18:00:00.000Z")
    assert ds._parse_iso("garbage") is None
