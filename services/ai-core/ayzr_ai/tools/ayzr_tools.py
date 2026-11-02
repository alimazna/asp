"""The AYZR tool set. Read-only, allowlisted, schema'd.

Ten tools, mirroring the capabilities the AI is permitted to have. Anything a
user asks that needs data outside this list gets an honest "not available"
rather than an invention.
"""

from __future__ import annotations

import time

from .registry import ToolRegistry, ToolResult, ToolSpec

TIMEFRAMES = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]


def _now() -> int:
    return int(time.time())


def build_registry(
    backend_url: str, client_token: str, timeout_ms: int = 10_000
) -> ToolRegistry:
    reg = ToolRegistry(backend_url, client_token, timeout_ms)

    async def latest_market_data(**_) -> ToolResult:
        res = await reg._get("/api/v1/candles?tf=M15&limit=1")
        if not res.ok:
            return res
        bars = (res.data or {}).get("bars") if isinstance(res.data, dict) else None
        if not bars:
            return ToolResult(ok=True, data=None, source="/api/v1/candles")
        bar = bars[-1]
        return ToolResult(
            ok=True,
            data={"symbol": "XAUUSD", "timeframe": "M15", "bar": bar},
            source="/api/v1/candles",
            timestamp=int(bar.get("time") or 0),
        )

    async def candle_history(timeframe: str = "M15", limit: int = 100, **_) -> ToolResult:
        tf = timeframe if timeframe in TIMEFRAMES else "M15"
        limit = max(1, min(int(limit), 500))
        res = await reg._get(f"/api/v1/candles?tf={tf}&limit={limit}")
        if not res.ok:
            return res
        bars = (res.data or {}).get("bars") if isinstance(res.data, dict) else None
        if not bars:
            return ToolResult(ok=True, data=None, source="/api/v1/candles")
        return ToolResult(
            ok=True,
            data={"symbol": "XAUUSD", "timeframe": tf, "count": len(bars), "bars": bars},
            source="/api/v1/candles",
            timestamp=int(bars[-1].get("time") or 0),
        )

    async def market_regime(**_) -> ToolResult:
        res = await reg._get("/api/v1/ayzr/ecosystem/engines")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def engine_results(**_) -> ToolResult:
        res = await reg._get("/api/v1/ayzr/engines")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def current_opportunities(**_) -> ToolResult:
        res = await reg._get("/api/v1/ayzr/ecosystem/opportunities")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def session_status(**_) -> ToolResult:
        res = await reg._get("/api/v1/ayzr/ecosystem/sessions")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def decision_ledger(limit: int = 20, **_) -> ToolResult:
        limit = max(1, min(int(limit), 100))
        res = await reg._get(f"/api/v1/ayzr/ecosystem/ledger?limit={limit}")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def news_events(limit: int = 20, **_) -> ToolResult:
        limit = max(1, min(int(limit), 50))
        res = await reg._get(f"/api/v1/news?limit={limit}")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def trade_lifecycle(**_) -> ToolResult:
        res = await reg._get("/api/v1/ayzr/trades")
        return ToolResult(ok=res.ok, data=res.data, error=res.error, source=res.source)

    async def system_health(**_) -> ToolResult:
        res = await reg._get("/api/v1/history?limit=1")
        ok = res.ok
        return ToolResult(
            ok=True,
            data={
                "backend_reachable": ok,
                "probed_at": _now(),
                "detail": res.data if ok else res.error,
            },
            source="/api/v1/history",
            timestamp=_now(),
        )

    reg.register(ToolSpec(
        "get_latest_market_data",
        "أحدث سعر إغلاق لـ XAUUSD على الإطار M15.",
        {"type": "object", "properties": {}, "required": []},
        latest_market_data,
    ))
    reg.register(ToolSpec(
        "get_candle_history",
        "شموع XAUUSD المغلقة لإطار زمني محدد.",
        {
            "type": "object",
            "properties": {
                "timeframe": {"type": "string", "enum": TIMEFRAMES},
                "limit": {"type": "integer", "minimum": 1, "maximum": 500},
            },
            "required": ["timeframe"],
        },
        candle_history,
    ))
    reg.register(ToolSpec(
        "get_market_regime",
        "حالة النظام السوقي من محركات AYZR.",
        {"type": "object", "properties": {}, "required": []},
        market_regime,
    ))
    reg.register(ToolSpec(
        "get_engine_results",
        "نتائج محركات AYZR التسعة.",
        {"type": "object", "properties": {}, "required": []},
        engine_results,
    ))
    reg.register(ToolSpec(
        "get_current_opportunities",
        "الفرص المنشورة حالياً من المنسّق المركزي.",
        {"type": "object", "properties": {}, "required": []},
        current_opportunities,
    ))
    reg.register(ToolSpec(
        "get_session_status",
        "حالة جلسات التداول الحالية.",
        {"type": "object", "properties": {}, "required": []},
        session_status,
    ))
    reg.register(ToolSpec(
        "get_decision_ledger",
        "سجل القرارات المنشورة سابقاً.",
        {
            "type": "object",
            "properties": {"limit": {"type": "integer", "minimum": 1, "maximum": 100}},
            "required": [],
        },
        decision_ledger,
    ))
    reg.register(ToolSpec(
        "get_news_events",
        "الأخبار الاقتصادية المتاحة.",
        {
            "type": "object",
            "properties": {"limit": {"type": "integer", "minimum": 1, "maximum": 50}},
            "required": [],
        },
        news_events,
    ))
    reg.register(ToolSpec(
        "get_trade_lifecycle",
        "دورة حياة الصفقات الظلية.",
        {"type": "object", "properties": {}, "required": []},
        trade_lifecycle,
    ))
    reg.register(ToolSpec(
        "get_system_health",
        "حالة اتصال الخدمات الخلفية.",
        {"type": "object", "properties": {}, "required": []},
        system_health,
    ))

    return reg
