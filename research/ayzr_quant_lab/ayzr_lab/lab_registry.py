"""AYZR Quant Lab — strategy registry, cost model and champion/challenger gate.

This module extends the existing lab (astra_port, backtest, stats) rather than
replacing it. It adds the three things the ecosystem spec requires and the lab
did not yet have:

  * a durable **experiment registry** (JSONL, append-only) so every run is
    recorded with its parameters, dataset window, costs and metrics;
  * an explicit **transaction-cost model** (spread + slippage + commission)
    so a backtest cannot silently assume frictionless fills;
  * a **champion/challenger promotion gate** that only promotes a challenger on
    out-of-sample evidence with a bounded multiple-testing correction — never on
    in-sample fit.

Nothing here calls a live service, and nothing here modifies ASTRA CORE.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import time
from dataclasses import asdict, dataclass, field
from typing import Dict, List, Optional, Sequence

from .backtest import Trade
from .stats import Summary, summarize, walk_forward, filter_trades


# ---------------------------------------------------------------------------
# Transaction-cost model
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class CostModel:
    """Explicit round-trip costs, in XAUUSD price units.

    A backtest that omits these is optimistic. The defaults reflect a realistic
    retail XAUUSD spread; they are *declared*, not derived from a live feed.
    """
    spread: float = 0.30          # one full spread per round trip
    slippage: float = 0.10        # adverse on entry and on stop/timeout
    commission_per_trade: float = 0.0   # in price units, applied once per trade

    def as_dict(self) -> dict:
        return asdict(self)

    @staticmethod
    def frictionless() -> "CostModel":
        """Only for a control run that explicitly measures the cost's effect."""
        return CostModel(spread=0.0, slippage=0.0, commission_per_trade=0.0)


# ---------------------------------------------------------------------------
# Experiment registry (append-only JSONL)
# ---------------------------------------------------------------------------
@dataclass
class ExperimentRecord:
    experiment_id: str
    created_at: int
    strategy_id: str
    strategy_version: str
    params: Dict[str, object]
    cost_model: Dict[str, float]
    window: Dict[str, int]
    dataset: str
    metrics: Dict[str, float]
    metrics_oos: Optional[Dict[str, float]] = None
    notes: str = ""
    # A content hash of (params, window, dataset) so an identical run is
    # detectable and cannot be double-counted as independent evidence.
    run_fingerprint: str = ""

    def as_dict(self) -> dict:
        return asdict(self)


def run_fingerprint(strategy_id: str, params: Dict[str, object],
                    window: Dict[str, int], dataset: str) -> str:
    """Deterministic fingerprint of a run's inputs."""
    blob = json.dumps(
        {"s": strategy_id, "p": params, "w": window, "d": dataset},
        sort_keys=True, separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()[:16]


class ExperimentRegistry:
    """Append-only JSONL registry of experiments.

    A file that does not exist yet is created on first append. A malformed line
    is skipped on read (with a recorded count) rather than raising, so one bad
    write cannot make the whole registry unreadable.
    """

    def __init__(self, path: str):
        self.path = path
        self.skipped_lines = 0

    def append(self, record: ExperimentRecord) -> None:
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
        line = json.dumps(record.as_dict(), sort_keys=True, ensure_ascii=False)
        with open(self.path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")

    def read_all(self) -> List[ExperimentRecord]:
        out: List[ExperimentRecord] = []
        self.skipped_lines = 0
        if not os.path.exists(self.path):
            return out
        with open(self.path, "r", encoding="utf-8") as fh:
            for raw in fh:
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    d = json.loads(raw)
                    out.append(ExperimentRecord(**d))
                except (json.JSONDecodeError, TypeError):
                    self.skipped_lines += 1
        return out

    def by_strategy(self, strategy_id: str) -> List[ExperimentRecord]:
        return [r for r in self.read_all() if r.strategy_id == strategy_id]

    def fingerprints(self) -> Dict[str, int]:
        """Count how many times each run fingerprint appears."""
        counts: Dict[str, int] = {}
        for r in self.read_all():
            counts[r.run_fingerprint] = counts.get(r.run_fingerprint, 0) + 1
        return counts


# ---------------------------------------------------------------------------
# Champion / challenger promotion gate
# ---------------------------------------------------------------------------
@dataclass
class PromotionDecision:
    promote: bool
    reason: str
    challenger_id: str
    champion_id: str
    oos_expectancy_challenger: float
    oos_expectancy_champion: float
    min_required: float
    oos_trades: int
    multiple_testing_penalty: float

    def as_dict(self) -> dict:
        return asdict(self)


@dataclass
class PromotionGate:
    """A conservative, out-of-sample promotion rule.

    A challenger is promoted only when ALL hold:
      1. it has at least ``min_oos_trades`` out-of-sample trades;
      2. its OOS expectancy exceeds the champion's by ``min_edge_r``;
      3. after a Bonferroni-style penalty for the number of challengers tested,
         its OOS expectancy is still positive.

    The penalty is deliberately crude but explicit: it makes the cost of
    multiple testing visible instead of pretending it does not exist.
    """
    min_oos_trades: int = 30
    min_edge_r: float = 0.05
    confidence_z: float = 1.0     # one-sided, ~84%; conservative on purpose

    def evaluate(self,
                 champion_id: str,
                 champion_oos: Sequence[Trade],
                 challenger_id: str,
                 challenger_oos: Sequence[Trade],
                 challengers_tested: int = 1) -> PromotionDecision:
        cs = summarize(challenger_oos)
        chs = summarize(champion_oos)
        penalty = float(max(1, challengers_tested))
        # The required bar rises with the number of challengers tried: a best-of-N
        # selection inflates the winner's measured expectancy by roughly its OOS
        # standard error times a penalty that grows with N.
        se = self._expectancy_se(challenger_oos)
        min_required = chs.expectancy_r + self.min_edge_r + self.confidence_z * se * (
            (penalty - 1.0) / penalty)

        if cs.n < self.min_oos_trades:
            return PromotionDecision(False, "insufficient_oos_trades", challenger_id,
                                     champion_id, cs.expectancy_r, chs.expectancy_r,
                                     min_required, cs.n, penalty)
        if cs.expectancy_r <= 0.0:
            return PromotionDecision(False, "challenger_oos_not_positive", challenger_id,
                                     champion_id, cs.expectancy_r, chs.expectancy_r,
                                     min_required, cs.n, penalty)
        if cs.expectancy_r < min_required:
            return PromotionDecision(False, "edge_below_threshold", challenger_id,
                                     champion_id, cs.expectancy_r, chs.expectancy_r,
                                     min_required, cs.n, penalty)
        return PromotionDecision(True, "promoted_on_oos", challenger_id, champion_id,
                                 cs.expectancy_r, chs.expectancy_r, min_required,
                                 cs.n, penalty)

    @staticmethod
    def _expectancy_se(trades: Sequence[Trade]) -> float:
        """Standard error of the mean R-multiple (0 when undefined)."""
        rs = [t.r_multiple for t in trades if not t.skipped]
        n = len(rs)
        if n < 2:
            return 0.0
        mean = sum(rs) / n
        var = sum((r - mean) ** 2 for r in rs) / (n - 1)
        return math.sqrt(var / n)


# ---------------------------------------------------------------------------
# Convenience: evaluate one strategy over an in-sample / out-of-sample split
# ---------------------------------------------------------------------------
@dataclass
class SplitEvaluation:
    in_sample: Summary
    out_of_sample: Summary
    folds: List[Dict[str, Summary]] = field(default_factory=list)

    def as_dict(self) -> dict:
        return {
            "in_sample": self.in_sample.as_dict(),
            "out_of_sample": self.out_of_sample.as_dict(),
            "folds": [
                {"in_sample": f["in_sample"].as_dict(),
                 "out_of_sample": f["out_of_sample"].as_dict()}
                for f in self.folds
            ],
        }


def evaluate_split(trades: Sequence[Trade], t0: int, t1: int,
                   train: float = 0.5, folds: int = 0) -> SplitEvaluation:
    """Split trades into IS/OOS by the lab's existing splitter.

    ``folds > 0`` additionally walks the full span in non-overlapping windows
    and reports the OOS expectancy of each, so a single call yields both the
    headline split and the fold stability.
    """
    from .stats import three_way_split  # local import: keeps module import cheap
    # Give the OOS tail a real slice: split the remainder evenly between the
    # validation and OOS thirds rather than letting val consume the whole tail.
    val = (1.0 - train) / 2.0
    parts = three_way_split(t0, t1, train=train, val=val)
    train_part = parts[0]
    oos_part = parts[2]
    is_trades = filter_trades(trades, train_part.start_sec, train_part.end_sec)
    oos_trades = filter_trades(trades, oos_part.start_sec, oos_part.end_sec)

    ev = SplitEvaluation(in_sample=summarize(is_trades),
                         out_of_sample=summarize(oos_trades))
    if folds > 0:
        for f in walk_forward(t0, t1, folds=folds):
            ev.folds.append({
                "in_sample": summarize(is_trades),
                "out_of_sample": summarize(filter_trades(trades, f.start_sec, f.end_sec)),
            })
    return ev


def record_experiment(registry: ExperimentRegistry,
                      strategy_id: str,
                      strategy_version: str,
                      params: Dict[str, object],
                      cost: CostModel,
                      window: Dict[str, int],
                      dataset: str,
                      metrics: Dict[str, float],
                      metrics_oos: Optional[Dict[str, float]] = None,
                      notes: str = "") -> ExperimentRecord:
    """Build and append one experiment record, returning it."""
    rec = ExperimentRecord(
        experiment_id=f"exp-{int(time.time())}-{run_fingerprint(strategy_id, params, window, dataset)}",
        created_at=int(time.time()),
        strategy_id=strategy_id,
        strategy_version=strategy_version,
        params=params,
        cost_model=cost.as_dict(),
        window=window,
        dataset=dataset,
        metrics=metrics,
        metrics_oos=metrics_oos,
        notes=notes,
        run_fingerprint=run_fingerprint(strategy_id, params, window, dataset),
    )
    registry.append(rec)
    return rec
