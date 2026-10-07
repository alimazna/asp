"""Probability output contract — DRAFT, for later handoff to Agent-C.

Status: DRAFT / NOT PUBLISHED. Per RULE C this contract must not be served to
any consumer until a calibrated model exists AND Agent-D has audited the
calibration (T11). `calibrated` is therefore a required field that the producer
must set honestly; the default is False, and a false value means the payload is
a research artifact, not a probability.

Wire shape (as directed in Phase 2.0 Step 4):

    {
      "timestamp": "...",
      "direction": "UP" | "DOWN",
      "probability": 0.0-1.0,
      "calibrated": true|false,
      "confidence_interval": [lo, hi],
      "coverage_tier": "high"|"medium"|"low",
      "model_version": "v1.0"
    }

This module only defines and validates the shape. It computes nothing.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Tuple

from src.models.calibration import TIER_BOUNDS
from src.models.splits import SplitError

VALID_DIRECTIONS = ("UP", "DOWN")
VALID_TIERS = tuple(name for name, _, _ in TIER_BOUNDS)


def tier_for(probability: float) -> str:
    """The coverage tier a probability belongs to. Mirrors `coverage_analysis`
    so the emitted tier and the audited coverage use one definition.
    """
    if not (0.0 <= probability <= 1.0):
        raise SplitError(f"probability out of range [0,1]: {probability}")
    for name, lower, upper in TIER_BOUNDS:
        if lower <= probability < upper or (name == "high" and probability >= upper):
            return name
    raise SplitError(f"no tier for probability {probability}")  # unreachable


@dataclass(frozen=True)
class ProbabilityOutput:
    """One probability record. Immutable, so a published record cannot be
    silently edited after the fact (RULE E — history is append-only)."""

    timestamp: str
    direction: str
    probability: float
    calibrated: bool
    confidence_interval: Tuple[float, float]
    coverage_tier: str
    model_version: str

    def validate(self) -> None:
        if self.direction not in VALID_DIRECTIONS:
            raise SplitError(
                f"direction must be one of {VALID_DIRECTIONS}, got {self.direction!r}"
            )
        if not (0.0 <= self.probability <= 1.0):
            raise SplitError(f"probability out of range [0,1]: {self.probability}")
        lo, hi = self.confidence_interval
        if not (0.0 <= lo <= self.probability <= hi <= 1.0):
            raise SplitError(
                "confidence_interval must bracket the probability within [0,1]: "
                f"lo={lo}, p={self.probability}, hi={hi}"
            )
        if self.coverage_tier not in VALID_TIERS:
            raise SplitError(
                f"coverage_tier must be one of {VALID_TIERS}, got {self.coverage_tier!r}"
            )
        if self.coverage_tier != tier_for(self.probability):
            raise SplitError(
                f"coverage_tier {self.coverage_tier!r} disagrees with the tier of "
                f"probability {self.probability}"
            )
        if not self.model_version:
            raise SplitError("model_version is required")
        # Parse the timestamp to reject malformed values early.
        _parse_timestamp(self.timestamp)

    def to_json(self) -> str:
        """Canonical serialization: sorted keys, fixed separators, so the same
        record always produces byte-identical output (determinism)."""
        self.validate()
        payload = asdict(self)
        payload["confidence_interval"] = list(self.confidence_interval)
        return json.dumps(payload, sort_keys=True, separators=(",", ":"))

    @staticmethod
    def from_json(text: str) -> "ProbabilityOutput":
        raw = json.loads(text)
        interval = raw.get("confidence_interval")
        if not (isinstance(interval, (list, tuple)) and len(interval) == 2):
            raise SplitError("confidence_interval must be a 2-element array")
        record = ProbabilityOutput(
            timestamp=raw["timestamp"],
            direction=raw["direction"],
            probability=float(raw["probability"]),
            calibrated=bool(raw["calibrated"]),
            confidence_interval=(float(interval[0]), float(interval[1])),
            coverage_tier=raw["coverage_tier"],
            model_version=raw["model_version"],
        )
        record.validate()
        return record


def _parse_timestamp(value: str) -> datetime:
    """Parse an ISO-8601 timestamp; require an explicit timezone so the record
    is unambiguous across the 9 timeframe streams."""
    text = value.replace("Z", "+00:00") if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(text)
    except ValueError as exc:
        raise SplitError(f"invalid timestamp {value!r}: {exc}") from exc
    if parsed.tzinfo is None:
        raise SplitError("timestamp must be timezone-aware (e.g. ...Z or +00:00)")
    return parsed.astimezone(timezone.utc)
