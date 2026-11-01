"""Point-in-time alignment of external datasets to the trading timeline.

The one rule that matters: a value must not be visible to a decision before it
was actually published. FRED daily series are stamped with the observation date
but are released the next business morning, so every value is shifted to the
following business day. COT is stamped with the Tuesday report date but released
the following Friday, so it is shifted to the Monday after release.

This module exposes ``ExternalContext`` which, given a UTC timestamp, returns the
latest values that were *available at that instant* — never a future value.
"""

from __future__ import annotations

import bisect
import csv
import datetime as dt
import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATASETS = os.path.join(LAB, "datasets")


def _parse_date(s: str) -> dt.date:
    return dt.datetime.strptime(s, "%Y-%m-%d").date()


def _next_business_day(d: dt.date) -> dt.date:
    n = d + dt.timedelta(days=1)
    while n.weekday() >= 5:
        n += dt.timedelta(days=1)
    return n


@dataclass
class ExternalContext:
    """Point-in-time lookup over aligned daily series.

    Each series is a sorted list of (available_date, value). ``as_of`` returns
    the most recent value whose available_date <= the given date.
    """
    series: Dict[str, List[Tuple[dt.date, float]]] = field(default_factory=dict)

    def _avail(self, name: str, when: dt.date) -> Optional[float]:
        s = self.series.get(name)
        if not s:
            return None
        dates = [d for d, _ in s]  # could precompute; fine at this scale
        i = bisect.bisect_right(dates, when) - 1
        return s[i][1] if i >= 0 else None

    def value(self, name: str, when: dt.date) -> Optional[float]:
        return self._avail(name, when)

    def change(self, name: str, when: dt.date, lookback_days: int = 5) -> Optional[float]:
        now = self._avail(name, when)
        then = self._avail(name, when - dt.timedelta(days=lookback_days))
        if now is None or then is None:
            return None
        return now - then


def _load_fred(series: str, lag_business_days: int = 1) -> List[Tuple[dt.date, float]]:
    path = os.path.join(DATASETS, f"fred_{series}.csv")
    if not os.path.exists(path):
        return []
    out = []
    for row in csv.DictReader(open(path)):
        try:
            obs = _parse_date(row["date"])
            val = float(row["value"])
        except (ValueError, KeyError):
            continue
        avail = obs
        for _ in range(lag_business_days):
            avail = _next_business_day(avail)
        out.append((avail, val))
    return out


def _load_yahoo(sym: str, lag_business_days: int = 1) -> List[Tuple[dt.date, float]]:
    path = os.path.join(DATASETS, f"yahoo_{sym.replace('=','_').replace('.','_')}.csv")
    if not os.path.exists(path):
        return []
    out = []
    for row in csv.DictReader(open(path)):
        try:
            obs = _parse_date(row["date"])
            val = float(row["close"])
        except (ValueError, KeyError, TypeError):
            continue
        avail = obs
        for _ in range(lag_business_days):
            avail = _next_business_day(avail)
        out.append((avail, val))
    return out


def _load_cot() -> List[Tuple[dt.date, float]]:
    """COT Tuesday report is released the following Friday -> usable Monday."""
    path = os.path.join(DATASETS, "cftc_cot_gold.csv")
    if not os.path.exists(path):
        return []
    out = []
    for row in csv.DictReader(open(path)):
        try:
            rep = _parse_date(row["report_date"])
            net = float(row["mm_net"])
        except (ValueError, KeyError, TypeError):
            continue
        # Tuesday -> next Monday (release Friday + weekend)
        avail = rep + dt.timedelta(days=6)
        out.append((avail, net))
    return out


def load_context() -> ExternalContext:
    ctx = ExternalContext()
    for s in ("DGS10", "DGS2", "DFII10", "DTWEXBGS", "VIXCLS"):
        ctx.series[s] = _load_fred(s)
    ctx.series["GC"] = _load_yahoo("GC=F")
    ctx.series["DXY"] = _load_yahoo("DX-Y.NYB")
    ctx.series["SI"] = _load_yahoo("SI=F")
    ctx.series["COT_MM_NET"] = _load_cot()
    return ctx
