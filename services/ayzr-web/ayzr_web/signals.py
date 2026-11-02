"""Deterministic trade-proposal engine.

Every number this module returns is computed from real closed candles by a
formula written out in the code below. Nothing is sampled, guessed, or
hardcoded. If there is not enough data to compute a level, the proposal is
withheld and the reason is returned instead.

What this is NOT: it is not a validated profitable strategy. No outcome data
exists here, so no win rate, expectancy or probability is reported. The
`confidence` field is a transparent count of how many independent checks agree,
not a probability and not a forecast.

The rules are deliberately simple and inspectable. A more elaborate model would
not be more honest; it would only be harder to audit.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .datasource import Candle


# --- indicators (plain, auditable formulas) ---------------------------------


def ema(values: list[float], period: int) -> list[float]:
    """Exponential moving average. First value seeds with a simple mean."""
    if not values or period <= 0 or len(values) < period:
        return []
    k = 2.0 / (period + 1.0)
    out = [sum(values[:period]) / period]
    for v in values[period:]:
        out.append(v * k + out[-1] * (1 - k))
    return out


def rsi(values: list[float], period: int = 14) -> float | None:
    """Wilder RSI over the whole series. None when there is too little data."""
    if len(values) < period + 1:
        return None
    gains, losses = 0.0, 0.0
    for i in range(1, period + 1):
        d = values[i] - values[i - 1]
        gains += max(d, 0.0)
        losses += max(-d, 0.0)
    avg_gain, avg_loss = gains / period, losses / period
    for i in range(period + 1, len(values)):
        d = values[i] - values[i - 1]
        avg_gain = (avg_gain * (period - 1) + max(d, 0.0)) / period
        avg_loss = (avg_loss * (period - 1) + max(-d, 0.0)) / period
    if avg_loss == 0:
        # No downward movement at all. RSI is 100 only when there were genuine
        # gains; a motionless series is neutral, not maximally overbought.
        return 100.0 if avg_gain > 0 else 50.0
    rs = avg_gain / avg_loss
    return 100.0 - (100.0 / (1.0 + rs))


def atr(candles: list[Candle], period: int = 14) -> float | None:
    """Average true range. The basis for every stop distance we quote."""
    if len(candles) < period + 1:
        return None
    trs = []
    for i in range(1, len(candles)):
        h, l, pc = candles[i].high, candles[i].low, candles[i - 1].close
        trs.append(max(h - l, abs(h - pc), abs(l - pc)))
    if len(trs) < period:
        return None
    return sum(trs[-period:]) / period


def swings(candles: list[Candle], left: int = 3, right: int = 3) -> tuple[list[float], list[float]]:
    """Fractal swing highs and lows: a bar higher/lower than `left` before it
    and `right` after it. These are the structural levels we trade around."""
    highs, lows = [], []
    for i in range(left, len(candles) - right):
        c = candles[i]
        if all(c.high >= candles[j].high for j in range(i - left, i)) and all(
            c.high > candles[j].high for j in range(i + 1, i + right + 1)
        ):
            highs.append(c.high)
        if all(c.low <= candles[j].low for j in range(i - left, i)) and all(
            c.low < candles[j].low for j in range(i + 1, i + right + 1)
        ):
            lows.append(c.low)
    return highs, lows


# --- proposal ---------------------------------------------------------------


@dataclass
class Proposal:
    status: str  # "ok" | "insufficient_data"
    direction: str  # bullish | bearish | sideways | no_trade
    reason_ar: str
    entry: float | None = None
    stop_loss: float | None = None
    take_profit: float | None = None
    risk_reward: float | None = None
    confidence: int = 0
    checks: list[str] = field(default_factory=list)
    indicators: dict = field(default_factory=dict)
    levels: dict = field(default_factory=dict)
    timeframe: str = ""
    as_of: int = 0
    source: str = ""

    def to_dict(self) -> dict:
        return {
            "status": self.status,
            "direction": self.direction,
            "reason_ar": self.reason_ar,
            "entry": round(self.entry, 2) if self.entry else None,
            "stop_loss": round(self.stop_loss, 2) if self.stop_loss else None,
            "take_profit": round(self.take_profit, 2) if self.take_profit else None,
            "risk_reward": round(self.risk_reward, 2) if self.risk_reward else None,
            "confidence": self.confidence,
            "checks": self.checks,
            "indicators": self.indicators,
            "levels": self.levels,
            "timeframe": self.timeframe,
            "as_of": self.as_of,
            "source": self.source,
            "disclaimer_ar": (
                "هذا تحليل فني آلي من شموع حقيقية. ليس توصية استثمارية، "
                "ولا توجد بيانات نتائج تُثبت ربحية هذه القواعد."
            ),
        }


def build_proposal(candles: list[Candle], timeframe: str) -> Proposal:
    """Derive a proposal from real closed candles, or explain why not."""
    if len(candles) < 60:
        return Proposal(
            status="insufficient_data",
            direction="no_trade",
            reason_ar=f"عدد الشموع المتاحة {len(candles)} وهو أقل من الحد المطلوب (60) لبناء مستويات موثوقة.",
            timeframe=timeframe,
        )

    closes = [c.close for c in candles]
    price = closes[-1]
    as_of = candles[-1].open_time

    ema20 = ema(closes, 20)
    ema50 = ema(closes, 50)
    r = rsi(closes, 14)
    a = atr(candles, 14)
    swing_highs, swing_lows = swings(candles)

    if not ema20 or not ema50 or a is None or a <= 0:
        return Proposal(
            status="insufficient_data",
            direction="no_trade",
            reason_ar="تعذّر حساب المؤشرات الأساسية من البيانات المتاحة.",
            timeframe=timeframe,
            as_of=as_of,
        )

    fast, slow = ema20[-1], ema50[-1]

    # Independent checks. Each contributes at most one point; the total is a
    # count of agreement, never a probability.
    checks: list[str] = []
    score = 0

    # A gap smaller than 0.05% of price is noise, not a trend. Without this a
    # motionless market reads as bearish simply because fast is not above slow.
    gap = (fast - slow) / price if price else 0.0
    if abs(gap) < 0.0005:
        checks.append("المتوسطان متقاربان: لا يوجد اتجاه واضح")
    elif gap > 0:
        score += 1
        checks.append("الاتجاه العام صاعد: المتوسط السريع فوق المتوسط البطيء")
    else:
        score -= 1
        checks.append("الاتجاه العام هابط: المتوسط السريع تحت المتوسط البطيء")

    if r is not None:
        if r < 35:
            score += 1
            checks.append(f"مؤشر القوة النسبية {r:.1f} يشير إلى تشبع بيعي")
        elif r > 65:
            score -= 1
            checks.append(f"مؤشر القوة النسبية {r:.1f} يشير إلى تشبع شرائي")
        else:
            checks.append(f"مؤشر القوة النسبية {r:.1f} في المنطقة المحايدة")

    dev = (price - fast) / price if price else 0.0
    if abs(dev) < 0.0005:
        checks.append("السعر ملاصق للمتوسط السريع")
    elif dev > 0:
        score += 1
        checks.append("السعر فوق المتوسط السريع")
    else:
        score -= 1
        checks.append("السعر تحت المتوسط السريع")

    # Nearest structural levels, taken from real swing points only.
    res = min([h for h in swing_highs if h > price], default=None)
    sup = max([l for l in swing_lows if l < price], default=None)

    if score >= 2:
        direction = "bullish"
    elif score <= -2:
        direction = "bearish"
    else:
        direction = "sideways"

    if direction == "sideways":
        return Proposal(
            status="ok",
            direction="sideways",
            reason_ar=(
                "الإشارات متعارضة، لا يوجد اتجاه واضح. القرار الأسلم هو عدم الدخول "
                "حتى تتوافق المؤشرات."
            ),
            confidence=abs(score),
            checks=checks,
            indicators={
                "ema20": round(fast, 2),
                "ema50": round(slow, 2),
                "rsi14": round(r, 2) if r is not None else None,
                "atr14": round(a, 2),
                "price": round(price, 2),
            },
            levels={
                "support": round(sup, 2) if sup else None,
                "resistance": round(res, 2) if res else None,
            },
            timeframe=timeframe,
            as_of=as_of,
            source=candles[-1].source,
        )

    # Stop distance is ATR-based, so it scales with real volatility instead of
    # being a fixed number of dollars.
    stop_dist = a * 1.5
    if direction == "bullish":
        entry = price
        stop_loss = entry - stop_dist
        if sup is not None and sup < entry:
            stop_loss = min(stop_loss, sup - a * 0.25)
        risk = entry - stop_loss
        take_profit = entry + risk * 2.0
        if res is not None and res > entry:
            take_profit = max(take_profit, res)
    else:
        entry = price
        stop_loss = entry + stop_dist
        if res is not None and res > entry:
            stop_loss = max(stop_loss, res + a * 0.25)
        risk = stop_loss - entry
        take_profit = entry - risk * 2.0
        if sup is not None and sup < entry:
            take_profit = min(take_profit, sup)

    risk = abs(entry - stop_loss)
    reward = abs(take_profit - entry)
    rr = reward / risk if risk > 0 else None

    if rr is None or rr < 1.2:
        return Proposal(
            status="ok",
            direction="no_trade",
            reason_ar=(
                f"الاتجاه {'صاعد' if direction == 'bullish' else 'هابط'} لكن نسبة "
                f"المخاطرة إلى المكافأة {rr:.2f} إن وجدت، وهي أقل من الحد الأدنى (1.2). "
                "الدخول هنا لا يستحق المخاطرة."
            )
            if rr
            else "تعذّر حساب نسبة المخاطرة إلى المكافأة من المستويات المتاحة.",
            confidence=abs(score),
            checks=checks,
            timeframe=timeframe,
            as_of=as_of,
            source=candles[-1].source,
        )

    return Proposal(
        status="ok",
        direction=direction,
        reason_ar=(
            f"توافقت {abs(score)} من الإشارات المستقلة على اتجاه "
            f"{'صاعد' if direction == 'bullish' else 'هابط'}. "
            f"الدخول عند {entry:.2f}، ووقف الخسارة عند {stop_loss:.2f} "
            f"محسوب من مدى الحركة الحقيقي (ATR) لا من رقم ثابت، "
            f"والهدف عند {take_profit:.2f} بنسبة مخاطرة إلى مكافأة {rr:.2f}."
        ),
        entry=entry,
        stop_loss=stop_loss,
        take_profit=take_profit,
        risk_reward=rr,
        confidence=abs(score),
        checks=checks,
        indicators={
            "ema20": round(fast, 2),
            "ema50": round(slow, 2),
            "rsi14": round(r, 2) if r is not None else None,
            "atr14": round(a, 2),
            "price": round(price, 2),
        },
        levels={
            "support": round(sup, 2) if sup else None,
            "resistance": round(res, 2) if res else None,
        },
        timeframe=timeframe,
        as_of=as_of,
        source=candles[-1].source,
    )
