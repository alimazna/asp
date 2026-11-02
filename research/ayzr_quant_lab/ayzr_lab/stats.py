"""Performance metrics and statistical validation.

Two jobs:

1. ``summarize`` turns a list of trades into an honest performance report
   (R-based expectancy, profit factor, drawdown, MAE/MFE, per-regime splits).
   Win rate alone is never the headline.
2. Validation helpers — walk-forward, out-of-sample splits, sensitivity sweeps
   and a bootstrap confidence interval — so a result that only exists on one
   slice of history is exposed rather than celebrated.

No probability/win-rate is ever presented as a calibrated probability of profit.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence

from .backtest import ExitReason, Trade


@dataclass
class Summary:
    n: int = 0
    wins: int = 0
    losses: int = 0
    breakeven: int = 0
    win_rate: float = 0.0
    profit_factor: float = 0.0
    expectancy_r: float = 0.0
    avg_win_r: float = 0.0
    avg_loss_r: float = 0.0
    total_r: float = 0.0
    max_drawdown_r: float = 0.0
    avg_bars_held: float = 0.0
    avg_mae_r: float = 0.0
    avg_mfe_r: float = 0.0
    targets: int = 0
    stops: int = 0
    timeouts: int = 0
    ambiguous: int = 0
    skipped: int = 0
    by_reason: Dict[str, float] = field(default_factory=dict)

    def as_dict(self) -> dict:
        return {k: (round(v, 4) if isinstance(v, float) else v)
                for k, v in self.__dict__.items()}


def summarize(trades: Sequence[Trade]) -> Summary:
    scored = [t for t in trades if not t.skipped]
    s = Summary()
    s.n = len(scored)
    if not scored:
        return s

    rs = [t.r_multiple for t in scored]
    s.total_r = sum(rs)
    s.expectancy_r = s.total_r / len(rs)

    wins = [r for r in rs if r > 0]
    losses = [r for r in rs if r < 0]
    s.wins = len(wins)
    s.losses = len(losses)
    s.breakeven = len(rs) - len(wins) - len(losses)
    s.win_rate = s.wins / len(rs)

    gross_win = sum(wins)
    gross_loss = -sum(losses)
    s.profit_factor = (gross_win / gross_loss) if gross_loss > 0 else float("inf") if gross_win > 0 else 0.0
    s.avg_win_r = (gross_win / len(wins)) if wins else 0.0
    s.avg_loss_r = (-gross_loss / len(losses)) if losses else 0.0

    # Equity curve in R, drawdown as the deepest peak-to-trough fall.
    equity = 0.0
    peak = 0.0
    dd = 0.0
    for r in rs:
        equity += r
        peak = max(peak, equity)
        dd = max(dd, peak - equity)
    s.max_drawdown_r = dd

    s.avg_bars_held = sum(t.bars_held for t in scored) / len(scored)
    s.avg_mae_r = sum(t.mae_r for t in scored) / len(scored)
    s.avg_mfe_r = sum(t.mfe_r for t in scored) / len(scored)

    s.targets = sum(1 for t in scored if t.reason == ExitReason.TARGET)
    s.stops = sum(1 for t in scored if t.reason == ExitReason.STOP)
    s.timeouts = sum(1 for t in scored if t.reason == ExitReason.TIMEOUT)
    s.ambiguous = sum(1 for t in scored if t.ambiguous)
    s.skipped = sum(1 for t in trades if t.skipped)
    s.by_reason = {
        "target": s.targets, "stop": s.stops, "timeout": s.timeouts,
    }
    return s


def summarize_by(trades: Sequence[Trade], key: str) -> Dict[str, Summary]:
    """Group trades by a context field (e.g. regime, session, direction)."""
    groups: Dict[str, List[Trade]] = {}
    for t in trades:
        if t.skipped:
            continue
        groups.setdefault(str(t.context.get(key, "?")), []).append(t)
    return {k: summarize(v) for k, v in sorted(groups.items())}


# --------------------------------------------------------------------------
# Splits and walk-forward
# --------------------------------------------------------------------------
@dataclass
class Split:
    name: str
    start_sec: int
    end_sec: int


def three_way_split(t0: int, t1: int, train: float = 0.5, val: float = 0.25) -> List[Split]:
    """Contiguous train / validation / out-of-sample split over [t0, t1]."""
    span = t1 - t0
    a = t0 + int(span * train)
    b = t0 + int(span * (train + val))
    return [Split("train", t0, a), Split("validation", a, b), Split("oos", b, t1)]


def walk_forward(t0: int, t1: int, folds: int = 5) -> List[Split]:
    """Anchored, non-overlapping OOS windows across the full span."""
    span = (t1 - t0) / folds
    out = []
    for i in range(folds):
        out.append(Split(f"wf{i}", int(t0 + i * span), int(t0 + (i + 1) * span)))
    return out


def filter_trades(trades: Sequence[Trade], lo: int, hi: int) -> List[Trade]:
    return [t for t in trades if lo <= t.entry_time_sec < hi]


# --------------------------------------------------------------------------
# Bootstrap CI on expectancy
# --------------------------------------------------------------------------
def bootstrap_expectancy(trades: Sequence[Trade], iters: int = 2000,
                         alpha: float = 0.05, seed: int = 12345):
    rs = [t.r_multiple for t in trades if not t.skipped]
    if len(rs) < 5:
        return None
    rnd = random.Random(seed)
    means = []
    n = len(rs)
    for _ in range(iters):
        sample = [rs[rnd.randrange(n)] for _ in range(n)]
        means.append(sum(sample) / n)
    means.sort()
    lo = means[int(iters * (alpha / 2))]
    hi = means[int(iters * (1 - alpha / 2))]
    return {"mean": sum(rs) / n, "ci_low": lo, "ci_high": hi, "iters": iters}


def sensitivity(param_values: Sequence[float],
                run: Callable[[float], Sequence[Trade]]) -> List[dict]:
    """Run a strategy across a parameter grid and report expectancy stability."""
    out = []
    for v in param_values:
        s = summarize(run(v))
        out.append({"param": v, "n": s.n, "expectancy_r": s.expectancy_r,
                    "profit_factor": s.profit_factor, "max_dd_r": s.max_drawdown_r})
    return out
