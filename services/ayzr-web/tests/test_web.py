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

def test_biquote_returns_nothing_for_w1():
    """biquote's `1w` is empty, which is why W1 comes from the fallback."""
    assert "W1" not in ds.BIQUOTE_INTERVAL

def test_w1_is_still_served_by_the_fallback():
    assert "W1" in ds.supported_timeframes()

def test_biquote_does_not_offer_mn1():
    """biquote answers `1M` with minute bars while echoing the label "1M"."""
    assert "MN1" not in ds.BIQUOTE_INTERVAL

def test_mn1_is_still_served_by_the_fallback():
    """Removing it from biquote must not remove it from the service."""
    assert "MN1" in ds.supported_timeframes()

def test_supported_timeframes_covers_both_sources():
    assert ds.supported_timeframes() == [
        "M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]

def test_every_offered_timeframe_has_at_least_one_source():
    for tf in ds.supported_timeframes():
        assert tf in ds.BIQUOTE_INTERVAL or tf in ds.YAHOO_INTERVAL

def test_mn1_from_the_fallback_is_monthly_not_minutely():
    """The regression that mattered: MN1 must never carry minute data."""
    monthly = [Candle(1_700_000_000 + i * 2592000, 2000.0, 2001.0, 1999.0,
                      2000.5, 10.0, "MN1") for i in range(12)]
    ds._assert_spacing(monthly, "MN1", "yahoo")

def test_mn1_carrying_minute_data_is_rejected():
    minutely = [Candle(1_700_000_000 + i * 60, 2000.0, 2001.0, 1999.0,
                       2000.5, 10.0, "MN1") for i in range(12)]
    with pytest.raises(DataUnavailable):
        ds._assert_spacing(minutely, "MN1", "biquote")


# --- structural spacing guard ------------------------------------------------
# This is the guard that catches a feed labelling data with one timeframe while
# serving another. A label comparison cannot see that.

def test_spacing_guard_accepts_correctly_spaced_bars():
    ds._assert_spacing(mk(50), "M15", "test")  # 900s apart, as M15 should be

def test_spacing_guard_rejects_minute_bars_labelled_monthly():
    """The real defect: minute bars served under a monthly label."""
    minute_bars = [Candle(1_700_000_000 + i * 60, 2000.0, 2001.0, 1999.0,
                          2000.5, 10.0, "MN1") for i in range(50)]
    with pytest.raises(DataUnavailable):
        ds._assert_spacing(minute_bars, "MN1", "biquote")

def test_spacing_guard_rejects_hour_bars_labelled_daily():
    hourly = [Candle(1_700_000_000 + i * 3600, 2000.0, 2001.0, 1999.0,
                     2000.5, 10.0, "D1") for i in range(50)]
    with pytest.raises(DataUnavailable):
        ds._assert_spacing(hourly, "D1", "test")

def test_spacing_guard_tolerates_real_weekend_gaps():
    """A gap must not make a correct series look wrong; the median is used."""
    bars = []
    t = 1_700_000_000
    for i in range(40):
        if i == 20:
            t += 3 * 86400  # a weekend of missing daily bars
        bars.append(Candle(t, 2000.0, 2001.0, 1999.0, 2000.5, 10.0, "D1"))
        t += 86400
    ds._assert_spacing(bars, "D1", "test")

def test_spacing_guard_is_skipped_for_tiny_series():
    ds._assert_spacing(mk(2), "M15", "test")  # too short to judge

def test_median_gap_is_order_independent():
    """biquote sends newest-first; the guard must not read that as unordered.

    This was a real outage: the guard rejected every biquote series and the
    service silently served the fallback for all nine timeframes.
    """
    asc = mk(20)
    desc = list(reversed(asc))
    assert ds._median_gap(asc) == ds._median_gap(desc) == 900

def test_spacing_guard_accepts_a_newest_first_series():
    ds._assert_spacing(list(reversed(mk(50))), "M15", "biquote")

def test_fetch_returns_bars_in_ascending_order(monkeypatch):
    async def newest_first(tf, limit, timeout_s):
        return list(reversed(mk(50)))
    monkeypatch.setattr(ds, "_fetch_biquote", newest_first)
    async def down(*a, **k):
        raise DataUnavailable("fallback down")
    monkeypatch.setattr(ds, "_fetch_yahoo", down)
    bars = asyncio.run(ds.fetch_candles("M15"))
    assert all(a.open_time < b.open_time for a, b in zip(bars, bars[1:]))

def test_biquote_is_preferred_over_the_fallback(monkeypatch):
    async def primary(tf, limit, timeout_s):
        return [Candle(1_700_000_000 + i * 900, 2000.0, 2001.0, 1999.0,
                       2000.5, 10.0, tf, "biquote:XAUUSD") for i in range(20)]
    async def fallback(tf, limit, timeout_s):
        raise DataUnavailable("should not be reached")
    monkeypatch.setattr(ds, "_fetch_biquote", primary)
    monkeypatch.setattr(ds, "_fetch_yahoo", fallback)
    bars = asyncio.run(ds.fetch_candles("M15"))
    assert "biquote" in bars[0].source

def test_median_gap_ignores_a_single_outlier():
    bars = mk(21)              # 900s apart
    bars[10].open_time += 4000  # one disturbance
    assert ds._median_gap(bars) == 900

def test_fetch_rejects_a_source_serving_the_wrong_spacing(monkeypatch):
    async def wrong(tf, limit, timeout_s):
        return [Candle(1_700_000_000 + i * 60, 2000.0, 2001.0, 1999.0,
                       2000.5, 10.0, tf) for i in range(50)]
    monkeypatch.setattr(ds, "_fetch_biquote", wrong)
    monkeypatch.setattr(ds, "_fetch_yahoo", wrong)
    with pytest.raises(DataUnavailable):
        asyncio.run(ds.fetch_candles("M15"))

def test_supported_timeframes_are_the_documented_nine():
    assert set(ds.supported_timeframes()) == {
        "M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"}

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
