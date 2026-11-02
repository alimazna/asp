"""Grounding: the rule that a market answer needs market data.

The measured failure mode of small models is confident invention. Qwen2.5-1.5B,
given no price, correctly refused to guess (good). But the same class of model,
given *some* data, will happily produce an entry price it was never told.

So we enforce it outside the model:

- A market question with no retrieved data returns `insufficient_data` and the
  model is not asked at all. There is nothing to ground an answer in.
- Any numeric level the model returns that does not appear in the retrieved
  data is stripped and reported, never shown as a trade level.
- Every grounded answer carries its sources with the timestamp they came from.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field


@dataclass
class Evidence:
    """One retrieved fact, with where it came from and when."""

    kind: str          # candles | astra | opportunity | news | ledger | health
    label: str         # human-readable Arabic label for the UI
    source: str        # backend route or provider id
    timestamp: int = 0  # epoch seconds; 0 means the source did not report one
    values: dict = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {
            "kind": self.kind,
            "label": self.label,
            "source": self.source,
            "timestamp": self.timestamp,
            "values": self.values,
        }


@dataclass
class GroundingResult:
    sufficient: bool
    reason_ar: str = ""
    evidence: list[Evidence] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "sufficient": self.sufficient,
            "reason_ar": self.reason_ar,
            "evidence": [e.to_dict() for e in self.evidence],
        }


def _numbers_in(value) -> set[float]:
    found: set[float] = set()
    if isinstance(value, dict):
        for v in value.values():
            found |= _numbers_in(v)
    elif isinstance(value, (list, tuple)):
        for v in value:
            found |= _numbers_in(v)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        found.add(round(float(value), 4))
    elif isinstance(value, str):
        for m in re.findall(r"-?\d+(?:\.\d+)?", value):
            try:
                found.add(round(float(m), 4))
            except ValueError:
                pass
    return found


def evidence_numbers(evidence: list[Evidence]) -> set[float]:
    """Every number the retrieved data actually contained."""
    out: set[float] = set()
    for ev in evidence:
        out |= _numbers_in(ev.values)
    return out


def strip_ungrounded_levels(
    levels: dict[str, float | None], evidence: list[Evidence]
) -> tuple[dict[str, float | None], list[str]]:
    """Drop any level the model returned that is not in the data.

    Returns (kept_levels, dropped_keys). A dropped level is not silently
    zeroed: the caller reports it, so a user never sees an invented price.
    """
    known = evidence_numbers(evidence)
    kept: dict[str, float | None] = {}
    dropped: list[str] = []
    for key, val in levels.items():
        if val is None:
            kept[key] = None
            continue
        if round(float(val), 4) in known:
            kept[key] = float(val)
        else:
            dropped.append(key)
            kept[key] = None
    return kept, dropped


def assess_market_grounding(
    question_kind: str, evidence: list[Evidence]
) -> GroundingResult:
    """Decide whether a market question can be answered at all.

    `question_kind` comes from the router: market | news | engines | general.
    """
    has_candles = any(e.kind == "candles" for e in evidence)
    has_astra = any(e.kind == "astra" for e in evidence)
    has_news = any(e.kind == "news" for e in evidence)

    if question_kind == "market":
        if not has_candles:
            return GroundingResult(
                sufficient=False,
                reason_ar="لا تتوفر بيانات شموع حديثة لـ XAUUSD، لذا لا يمكن تقديم تحليل سعري موثوق.",
                evidence=evidence,
            )
        return GroundingResult(True, evidence=evidence)

    if question_kind == "engines":
        if not has_astra and not has_candles:
            return GroundingResult(
                sufficient=False,
                reason_ar="لا تتوفر نتائج محركات أو بيانات سوق حديثة لتفسير إشارة.",
                evidence=evidence,
            )
        return GroundingResult(True, evidence=evidence)

    if question_kind == "news":
        if not has_news:
            return GroundingResult(
                sufficient=False,
                reason_ar="لا توجد أخبار متاحة للتحليل حالياً.",
                evidence=evidence,
            )
        return GroundingResult(True, evidence=evidence)

    return GroundingResult(True, evidence=evidence)


INSUFFICIENT_AR = "لا تتوفر بيانات كافية للإجابة بشكل موثوق."
