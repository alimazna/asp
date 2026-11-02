"""AYZR web service: real gold candles, real signals, local Arabic AI.

Everything the site shows is fetched live from a real market feed and computed
here. When a source is down the endpoint reports it and the page shows an
explicit Arabic state; nothing is ever substituted with a plausible-looking
number.
"""

from __future__ import annotations

import os
import time
from pathlib import Path

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from . import ai, datasource, signals
from .datasource import DataUnavailable

STATIC = Path(__file__).resolve().parent.parent / "static"
MODEL_URL = os.environ.get("AYZR_MODEL_URL", "http://127.0.0.1:8080")

app = FastAPI(
    title="AYZR — منصة ذكاء أسواق الذهب",
    version="1.0.0",
    description="عرض حقيقي لبيانات XAUUSD مع تحليل محلي بالعربية.",
)

# Small in-process cache so a page refresh does not hammer the market feed.
_CACHE: dict[str, tuple[float, object]] = {}
_CACHE_TTL = 20.0


def _cached(key: str, ttl: float = _CACHE_TTL):
    hit = _CACHE.get(key)
    if hit and (time.time() - hit[0]) < ttl:
        return hit[1]
    return None


def _store(key: str, value):
    _CACHE[key] = (time.time(), value)
    return value


@app.get("/api/health")
async def health():
    model = await ai.health(MODEL_URL)
    return {
        "service": "ayzr-web",
        "status": "ok",
        "model": model,
        "market_feed": "biquote:XAUUSD (MetaTrader 5)",
        "time": int(time.time()),
    }


@app.get("/api/quote")
async def quote():
    cached = _cached("quote")
    if cached:
        return cached
    try:
        return _store("quote", await datasource.fetch_quote())
    except DataUnavailable as exc:
        # 503 with a reason: the page renders "no data", never a stale price
        # presented as current.
        raise HTTPException(status_code=503, detail=str(exc))


@app.get("/api/candles")
async def candles(
    tf: str = Query("M15"),
    limit: int = Query(300, ge=10, le=1000),
):
    if tf not in datasource.TIMEFRAME_SECONDS:
        raise HTTPException(status_code=400, detail=f"إطار زمني غير مدعوم: {tf}")
    key = f"candles:{tf}:{limit}"
    cached = _cached(key)
    if cached:
        return cached
    try:
        bars = await datasource.fetch_candles(tf, limit=limit)
    except DataUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc))
    return _store(
        key,
        {
            "symbol": "XAUUSD",
            "timeframe": tf,
            "source": bars[-1].source,
            "count": len(bars),
            "candles": [c.to_dict() for c in bars],
        },
    )


@app.get("/api/analysis")
async def analysis(
    tf: str = Query("M15"),
    with_ai: bool = Query(True),
):
    if tf not in datasource.TIMEFRAME_SECONDS:
        raise HTTPException(status_code=400, detail=f"إطار زمني غير مدعوم: {tf}")
    try:
        bars = await datasource.fetch_candles(tf, limit=300)
    except DataUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc))

    proposal = signals.build_proposal(bars, tf)
    payload: dict = {
        "symbol": "XAUUSD",
        "timeframe": tf,
        "proposal": proposal.to_dict(),
        "candle_count": len(bars),
    }
    if with_ai and proposal.status == "ok":
        try:
            payload["ai"] = await ai.analyze(proposal, base_url=MODEL_URL)
        except ai.AiUnavailable as exc:
            # The signal still stands on its own; the AI section says why it is
            # missing instead of failing the whole request.
            payload["ai"] = {"status": "unavailable", "reason_ar": str(exc)}
    return payload


@app.get("/api/news")
async def news(limit: int = Query(30, ge=1, le=200)):
    cached = _cached(f"news:{limit}", ttl=120.0)
    if cached:
        return cached
    try:
        events = await datasource.fetch_news(limit=limit)
    except DataUnavailable as exc:
        raise HTTPException(status_code=503, detail=str(exc))
    return _store(f"news:{limit}", {
        "count": len(events),
        "source": "biquote / MQL5 economic calendar",
        "events": events,
    })


@app.get("/api/timeframes")
async def timeframes():
    return {
        "timeframes": [
            {"code": tf, "label": tf}
            for tf in datasource.supported_timeframes()
        ]
    }


@app.exception_handler(DataUnavailable)
async def _data_unavailable(request, exc):
    return JSONResponse(status_code=503, content={"detail": str(exc)})


app.mount("/static", StaticFiles(directory=STATIC), name="static")


@app.get("/")
async def index():
    return FileResponse(STATIC / "index.html")
