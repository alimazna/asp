"""Real XAUUSD candle data, primarily from biquote.io.

biquote is the requested feed. It aggregates real MetaTrader 5 broker feeds and
exposes them without an API key, and — unlike a plain chart API — it marks the
still-forming bar with `isOpen: true`. That flag is better than guessing which
bar is incomplete, so we trust it rather than assuming the last bar is open.

Yahoo (COMEX GC=F) stays as a fallback: if biquote is unreachable the site keeps
working instead of going blank. Whichever source answered is reported in the
response, so the UI can say where a number came from. If neither answers, the
caller gets DataUnavailable and must show "no data" — never a fabricated candle.

Contract note: biquote wants `interval` in lowercase form (`15m`, `4h`, `1M`).
`M15` is silently ignored and it falls back to 1h, which would be a silent
timeframe substitution — exactly the class of bug this system must not have.
"""

from __future__ import annotations

import calendar
import time
from dataclasses import dataclass

import httpx

BIQUOTE = "https://api.biquote.io"
YAHOO = "https://query1.finance.yahoo.com/v8/finance/chart/GC=F"
SYMBOL = "XAUUSD"

BIQUOTE_INTERVAL: dict[str, str] = {
    "M1": "1m",
    "M5": "5m",
    "M15": "15m",
    "M30": "30m",
    "H1": "1h",
    "H4": "4h",
    "D1": "1d",
    # W1 and MN1 are intentionally absent. biquote returns an empty series for
    # `1w`, and `1M` is worse than empty: it answers with *minute* bars while
    # echoing the label "1M" back. Both are reported as unsupported rather than
    # served wrong.
}

YAHOO_INTERVAL: dict[str, str] = {
    "M1": "1m", "M5": "5m", "M15": "15m", "M30": "30m", "H1": "1h",
    "H4": "4h", "D1": "1d", "W1": "1wk", "MN1": "1mo",
}
YAHOO_RANGE: dict[str, str] = {
    "M1": "7d", "M5": "1mo", "M15": "1mo", "M30": "1mo", "H1": "3mo",
    "H4": "1y", "D1": "2y", "W1": "5y", "MN1": "10y",
}

TIMEFRAME_SECONDS = {
    "M1": 60, "M5": 300, "M15": 900, "M30": 1800, "H1": 3600,
    "H4": 14400, "D1": 86400, "W1": 604800, "MN1": 2592000,
}


@dataclass
class Candle:
    open_time: int
    open: float
    high: float
    low: float
    close: float
    volume: float
    timeframe: str
    source: str = "biquote:XAUUSD"
    is_open: bool = False

    def to_dict(self) -> dict:
        return {
            "open_time": self.open_time,
            "open": round(self.open, 2),
            "high": round(self.high, 2),
            "low": round(self.low, 2),
            "close": round(self.close, 2),
            "volume": self.volume,
            "timeframe": self.timeframe,
            "source": self.source,
        }


class DataUnavailable(RuntimeError):
    """Raised when no real data can be obtained. Callers must surface this."""


def timeframe_seconds(tf: str) -> int:
    return TIMEFRAME_SECONDS[tf]


def is_aligned(open_time: int, tf: str) -> bool:
    """True when a bar's open time sits exactly on its timeframe grid."""
    step = TIMEFRAME_SECONDS.get(tf)
    if not step:
        return False
    # D1 and above are exempt: a broker offset can shift them by whole hours
    # without making the bar wrong.
    if tf in ("D1", "W1", "MN1"):
        return True
    return open_time % step == 0


def supported_timeframes() -> list[str]:
    """Timeframes the service can actually deliver, in chart order.

    This is the union of both sources, not just biquote. Reporting only the
    primary feed hid W1/MN1, which the fallback serves fine — the UI was
    hiding timeframes that work. Each candle carries the source that answered,
    so a caller can always see where a series came from.
    """
    order = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]
    available = set(BIQUOTE_INTERVAL) | set(YAHOO_INTERVAL)
    return [tf for tf in order if tf in available]


def _valid_ohlc(o: float, h: float, l: float, c: float) -> bool:
    if not all(x > 0 for x in (o, h, l, c)):
        return False
    # A high below the body, or a low above it, means the row is corrupt.
    return not (h < max(o, c) or l > min(o, c))


def _parse_iso(ts: str) -> int | None:
    """Parse biquote's ISO-8601 UTC stamp to epoch seconds without a tz db."""
    try:
        clean = ts.replace("Z", "").split(".")[0]
        return calendar.timegm(time.strptime(clean, "%Y-%m-%dT%H:%M:%S"))
    except (ValueError, AttributeError):
        return None


async def _fetch_biquote(timeframe: str, limit: int, timeout_s: float) -> list[Candle]:
    interval = BIQUOTE_INTERVAL.get(timeframe)
    if interval is None:
        raise DataUnavailable(f"biquote does not serve {timeframe}")

    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            resp = await client.get(
                f"{BIQUOTE}/api/{SYMBOL}/ohlc",
                params={"interval": interval, "limit": min(limit, 1000)},
                headers={"User-Agent": "AYZR/1.0"},
            )
    except httpx.HTTPError as exc:
        raise DataUnavailable(f"biquote unreachable: {exc}") from exc

    if resp.status_code != 200:
        raise DataUnavailable(f"biquote returned HTTP {resp.status_code}")

    try:
        payload = resp.json()
        returned = payload["interval"]
        bars = payload["bars"]
    except (KeyError, TypeError, ValueError) as exc:
        raise DataUnavailable(f"unexpected biquote shape: {exc}") from exc

    if returned != interval:
        raise DataUnavailable(
            f"biquote answered interval {returned!r} but {interval!r} was requested"
        )

    out: list[Candle] = []
    for b in bars:
        try:
            o, h, l, c = (
                float(b["open"]), float(b["high"]),
                float(b["low"]), float(b["close"]),
            )
        except (KeyError, TypeError, ValueError):
            continue
        if not _valid_ohlc(o, h, l, c):
            continue
        ts = _parse_iso(b.get("openTime", ""))
        if ts is None:
            continue
        vol = b.get("tickVolume") or b.get("volume") or 0
        out.append(
            Candle(
                open_time=ts, open=o, high=h, low=l, close=c,
                volume=float(vol), timeframe=timeframe,
                source="biquote:XAUUSD (MetaTrader 5)",
                is_open=bool(b.get("isOpen", False)),
            )
        )

    if not out:
        raise DataUnavailable(f"biquote returned no usable {timeframe} candles")

    # Drop the bar biquote itself flagged as still forming; we trust its flag
    # rather than assuming the last row is the open one.
    closed = [c for c in out if not c.is_open]
    out = closed if closed else out[:-1]
    out.sort(key=lambda c: c.open_time)
    return out[-limit:]


def _median_gap(bars: list[Candle]) -> float:
    """Median spacing between consecutive bars, independent of feed order.

    Feeds differ: biquote sends newest-first, Yahoo oldest-first. Sorting here
    means the guard judges the data's spacing, which is what matters, instead of
    failing on an ordering convention that fetch_candles normalises anyway.
    """
    times = sorted(c.open_time for c in bars)
    gaps = sorted(b - a for a, b in zip(times, times[1:]))
    if not gaps:
        return 0.0
    mid = len(gaps) // 2
    return float(gaps[mid] if len(gaps) % 2 else (gaps[mid - 1] + gaps[mid]) / 2)


def _assert_spacing(bars: list[Candle], timeframe: str, source: str) -> None:
    """Reject a series whose real bar spacing contradicts its timeframe.

    This is the guard that matters: it catches a feed that labels data with one
    timeframe while serving another, which a label comparison cannot see.
    """
    step = TIMEFRAME_SECONDS[timeframe]
    if len(bars) < 3:
        return
    gap = _median_gap(bars)
    if gap <= 0:
        raise DataUnavailable(f"{source} returned duplicated {timeframe} bars")
    # Half to double the expected step. Real markets gap, but not by 1000x.
    if not (step * 0.5 <= gap <= step * 2.0):
        raise DataUnavailable(
            f"{source} served {timeframe} bars spaced {gap:.0f}s apart; "
            f"expected about {step}s"
        )


async def _fetch_yahoo(timeframe: str, limit: int, timeout_s: float) -> list[Candle]:
    interval = YAHOO_INTERVAL.get(timeframe)
    if interval is None:
        raise DataUnavailable(f"yahoo does not serve {timeframe}")
    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            resp = await client.get(
                YAHOO,
                params={
                    "interval": interval,
                    "range": YAHOO_RANGE[timeframe],
                    "includePrePost": "false",
                },
                headers={"User-Agent": "Mozilla/5.0 (compatible; AYZR/1.0)"},
            )
    except httpx.HTTPError as exc:
        raise DataUnavailable(f"yahoo unreachable: {exc}") from exc
    if resp.status_code != 200:
        raise DataUnavailable(f"yahoo returned HTTP {resp.status_code}")

    try:
        result = resp.json()["chart"]["result"][0]
        stamps = result["timestamp"]
        quote = result["indicators"]["quote"][0]
    except (KeyError, IndexError, TypeError, ValueError) as exc:
        raise DataUnavailable(f"unexpected yahoo shape: {exc}") from exc

    opens, highs, lows, closes = (
        quote.get("open"), quote.get("high"), quote.get("low"), quote.get("close")
    )
    vols = quote.get("volume") or []
    if not all(isinstance(x, list) for x in (opens, highs, lows, closes)):
        raise DataUnavailable("yahoo omitted OHLC arrays")

    out: list[Candle] = []
    for i, ts in enumerate(stamps):
        try:
            o, h, l, c = opens[i], highs[i], lows[i], closes[i]
        except IndexError:
            continue
        if None in (o, h, l, c):
            continue
        o, h, l, c = float(o), float(h), float(l), float(c)
        if not _valid_ohlc(o, h, l, c):
            continue
        v = vols[i] if i < len(vols) and vols[i] is not None else 0.0
        out.append(Candle(int(ts), o, h, l, c, float(v), timeframe, "yahoo:GC=F"))
    if not out:
        raise DataUnavailable(f"yahoo returned no usable {timeframe} candles")
    out.sort(key=lambda c: c.open_time)
    # Yahoo has no closed flag, so the last bar is assumed to be forming.
    return out[:-1][-limit:] if len(out) > 1 else out


async def fetch_candles(
    timeframe: str,
    *,
    limit: int = 300,
    timeout_s: float = 15.0,
) -> list[Candle]:
    """Closed candles for one timeframe, biquote first then Yahoo.

    Raises DataUnavailable only when both real sources fail.
    """
    if timeframe not in TIMEFRAME_SECONDS:
        raise DataUnavailable(f"unsupported timeframe: {timeframe}")

    errors: list[str] = []
    for name, fn in (("biquote", _fetch_biquote), ("yahoo", _fetch_yahoo)):
        try:
            bars = await fn(timeframe, limit, timeout_s)
            if not bars:
                continue
            # Verified here, at the one place every source passes through, so a
            # new or swapped fetcher cannot bypass it.
            _assert_spacing(bars, timeframe, source=name)
            # Ascending is part of the contract callers rely on, so it is
            # enforced here instead of assumed of each feed. biquote sends
            # newest-first and Yahoo oldest-first.
            return sorted(bars, key=lambda c: c.open_time)
        except DataUnavailable as exc:
            errors.append(f"{name}: {exc}")
    raise DataUnavailable("; ".join(errors))


async def fetch_quote(timeout_s: float = 10.0) -> dict:
    """Live bid/ask/spread plus day change. Raises DataUnavailable."""
    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            resp = await client.get(
                f"{BIQUOTE}/api/{SYMBOL}/", headers={"User-Agent": "AYZR/1.0"}
            )
        if resp.status_code == 200:
            d = resp.json()
            bid, ask = d.get("bid"), d.get("ask")
            if bid and ask:
                mid = (float(bid) + float(ask)) / 2.0
                return {
                    "symbol": SYMBOL,
                    "price": round(mid, 2),
                    "bid": round(float(bid), 2),
                    "ask": round(float(ask), 2),
                    "spread": round(float(ask) - float(bid), 2),
                    "day_high": round(float(d["high"]), 2) if d.get("high") else None,
                    "day_low": round(float(d["low"]), 2) if d.get("low") else None,
                    "change_pct": round(float(d["dayDiffPercent"]), 2)
                    if d.get("dayDiffPercent") is not None else None,
                    "direction": d.get("direction"),
                    "market_time": d.get("timestamp"),
                    "exchange": d.get("exchange", "OTC"),
                    "source": d.get("source", "MetaTrader 5"),
                }
    except (httpx.HTTPError, KeyError, TypeError, ValueError):
        pass

    # Fallback: derive a mid price from the newest closed M15 candle.
    bars = await fetch_candles("M15", limit=3, timeout_s=timeout_s)
    last, prev = bars[-1], (bars[-2] if len(bars) > 1 else bars[-1])
    change_pct = ((last.close - prev.close) / prev.close) * 100.0 if prev.close else None
    return {
        "symbol": SYMBOL,
        "price": round(last.close, 2),
        "bid": None,
        "ask": None,
        "spread": None,
        "day_high": None,
        "day_low": None,
        "change_pct": round(change_pct, 2) if change_pct is not None else None,
        "direction": None,
        "market_time": last.open_time,
        "exchange": "OTC",
        "source": last.source,
    }


async def fetch_news(limit: int = 30, timeout_s: float = 12.0) -> list[dict]:
    """Real economic calendar events from biquote (MQL5 sourced).

    These are scheduled macro releases with actual/forecast/previous readings —
    the category that actually moves gold. Nothing here is generated text: if
    the feed is down the caller shows no events rather than invented headlines.
    """
    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            resp = await client.get(
                f"{BIQUOTE}/api/calendar",
                params={"limit": min(limit, 200)},
                headers={"User-Agent": "AYZR/1.0"},
            )
        if resp.status_code != 200:
            raise DataUnavailable(f"calendar returned HTTP {resp.status_code}")
        rows = resp.json()
    except httpx.HTTPError as exc:
        raise DataUnavailable(f"calendar unreachable: {exc}") from exc
    except (ValueError, TypeError) as exc:
        raise DataUnavailable(f"unexpected calendar shape: {exc}") from exc

    rank = {"high": 3, "medium": 2, "low": 1, "none": 0}
    out = []
    for e in rows if isinstance(rows, list) else []:
        name = e.get("name")
        if not name:
            continue
        imp = (e.get("importance") or "none").lower()
        if rank.get(imp, 0) < 2:
            continue  # only events that plausibly move gold
        out.append({
            "id": e.get("id"),
            "time": e.get("time"),
            "currency": e.get("currency"),
            "country": e.get("countryCode"),
            "name": name,
            "importance": imp,
            "actual": e.get("actual"),
            "forecast": e.get("forecast"),
            "previous": e.get("previous"),
            "unit": e.get("unit"),
            "source": e.get("source", "MQL5"),
        })
    out.sort(key=lambda x: x.get("time") or "", reverse=True)
    return out[:limit]
