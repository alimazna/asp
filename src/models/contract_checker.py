"""T23 — frozen ASTRA API v1 contract + invariant checker.

Single canonical enforcement point for the frozen backend/frontend contract
(`docs/architecture/API_V1_SCHEMA.json`, tag `api-v1.0`). Two layers, per the
Lead's E06 ruling:

  1. **structure** — a payload conforms to the frozen schema (types, required,
     `const`, enums, ranges). The schema file is the source of truth; this
     module reads it rather than re-declaring a copy of the contract.
  2. **semantics** — the frozen-null invariants the JSON Schema cannot express.
     While `signal.probability` is null, the decision-model fields are null
     (`signal.horizon`, `confidence_lo`, `confidence_hi`, `model_version`, every
     `levels.*`, `meta.data_freshness_sec`, `context.mtf_agreement`) and
     `meta.score_is_probability` is false; a non-null probability requires
     `signal.probability_calibrated` true.

This module is the shared helper Agent-C's F17-1 check and Agent-D's audit
consume instead of re-implementing the frozen-null set. Agent-A's
`tests/integration/test_api_fixtures.py` remains the fixture-side validation; it
is not replaced here. A test parity-checks this module's structural validator
against `scripts/mock_api.validate_envelope` so the two readers of the schema
cannot silently diverge.

Python standard library only (deterministic, bare-container safe). Nothing here
publishes a probability — RULE C still governs the producer.
"""

from __future__ import annotations

import json
import math
import os
from typing import Any, Dict, List, Optional

_HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
SCHEMA_PATH = os.path.join(REPO_ROOT, "docs", "architecture", "API_V1_SCHEMA.json")

ANALYSIS_ENDPOINT = "GET /api/v1/analysis/latest"

# The frozen-null set (v1). These fields are `null` this release regardless of
# calibration; `BACKEND_FRONTEND_API_V1.md` ("Unavailable is not zero") lists
# them. They populate additively in v1.x, never by removing or retyping.
FROZEN_NULL_SIGNAL = ("horizon", "confidence_lo", "confidence_hi", "model_version")
FROZEN_NULL_LEVELS = (
    "entry",
    "stop_loss",
    "take_profit",
    "reward_risk",
    "suggested_risk_pct",
    "sl_method",
    "tp_method",
)
FROZEN_NULL_META = ("data_freshness_sec",)
FROZEN_NULL_CONTEXT = ("mtf_agreement",)

# Live (not frozen) analysis fields: real runtime state, not pinned nulls. An
# impl-vs-schema check must compare these loosely, never for equality (F22-3).
DYNAMIC_ANALYSIS_FIELDS = (
    ("timestamp",),
    ("symbol",),
    ("context", "regime"),
    ("context", "h4_bias"),
    ("context", "m15_trigger"),
    ("context", "volatility_state"),
    ("signal", "direction"),
    ("signal", "score"),
    ("meta", "coverage_tier"),
    ("meta", "degraded"),
    ("meta", "disclaimer"),
)


class ContractError(Exception):
    """A payload violates the frozen contract (structure or semantics)."""


# --------------------------------------------------------------------------
# Structural layer — a purpose-built validator for the API_V1_SCHEMA dialect.
# --------------------------------------------------------------------------


def _json_type(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    return "unknown"


def _matches_type(value: Any, expected: Any) -> bool:
    names = expected if isinstance(expected, list) else [expected]
    actual = _json_type(value)
    for name in names:
        if name == "number" and actual in ("integer", "number"):
            return True
        if name == actual:
            return True
    return False


def _validate_properties(value: Any, spec: Dict[str, Any], where: str) -> None:
    expected = spec.get("type")
    if expected is not None and not _matches_type(value, expected):
        raise ContractError(
            f"{where}: expected type {expected}, got {_json_type(value)}"
        )
    if "const" in spec and value != spec["const"]:
        raise ContractError(f"{where}: expected const {spec['const']!r}, got {value!r}")
    if "enum" in spec and value not in spec["enum"]:
        raise ContractError(f"{where}: {value!r} not in enum {spec['enum']}")
    if value is not None and isinstance(value, (int, float)) and not isinstance(
        value, bool
    ):
        # F23-1: reject non-finite numbers. The schema dialect's range check
        # alone cannot (`NaN < min` and `NaN > max` are both false), and JSON
        # permits bare `NaN`/`Infinity`, so `json.loads` would accept them. This
        # mirrors the `math.isfinite` guard Agent-C added to
        # `scripts/mock_api.py` (F22-4b-v) so the two readers of one schema agree.
        if isinstance(value, float) and not math.isfinite(value):
            raise ContractError(f"{where}: non-finite number {value!r} is not allowed")
        if "minimum" in spec and value < spec["minimum"]:
            raise ContractError(f"{where}: {value} below minimum {spec['minimum']}")
        if "maximum" in spec and value > spec["maximum"]:
            raise ContractError(f"{where}: {value} above maximum {spec['maximum']}")


def _validate_object(
    endpoint: str, where: str, value: Any, required: List[str], properties: Dict[str, Any]
) -> None:
    if not isinstance(value, dict):
        raise ContractError(f"{endpoint}.{where}: expected object")
    for key in required:
        if key not in value:
            raise ContractError(f"{endpoint}.{where}: missing required '{key}'")
    for key, subspec in properties.items():
        if key not in value:
            continue
        node = value[key]
        _validate_properties(node, subspec, f"{endpoint}.{where}.{key}")
        if subspec.get("type") == "object" and "required" in subspec:
            for sub in subspec["required"]:
                if sub not in node:
                    raise ContractError(
                        f"{endpoint}.{where}.{key}: missing required '{sub}'"
                    )
        if subspec.get("type") == "object" and "properties" in subspec:
            for sub, subsub in subspec["properties"].items():
                if sub in node:
                    _validate_properties(
                        node[sub], subsub, f"{endpoint}.{where}.{key}.{sub}"
                    )


def validate_data(endpoint: str, spec: Dict[str, Any], data: Any) -> None:
    if spec.get("data_type") == "array":
        if not isinstance(data, list):
            raise ContractError(f"{endpoint}.data: expected array")
        for i, element in enumerate(data):
            _validate_object(
                endpoint,
                f"data[{i}]",
                element,
                spec.get("element_required", []),
                spec.get("element_properties", {}),
            )
        return
    _validate_object(
        endpoint,
        "data",
        data,
        spec.get("data_required", []),
        spec.get("data_properties", {}),
    )


def validate_envelope(endpoint: str, spec: Dict[str, Any], payload: Any) -> None:
    """Validate one enveloped response body against an endpoint spec."""
    if not isinstance(payload, dict):
        raise ContractError(f"{endpoint}: response body is not an object")
    for key in spec.get("required", []):
        if key not in payload:
            raise ContractError(f"{endpoint}: envelope missing '{key}'")
    if payload.get("api") != "v1":
        raise ContractError(f"{endpoint}: api != v1")
    if payload.get("schema") != "1.0":
        raise ContractError(f"{endpoint}: schema != 1.0")
    validate_data(endpoint, spec, payload.get("data"))


# --------------------------------------------------------------------------
# Semantic layer — the frozen-null invariants (E06/E07).
# --------------------------------------------------------------------------


def frozen_violations(data: Any) -> List[str]:
    """Every frozen-contract invariant `data` violates (empty means clean).

    `analysis/latest` is the only surface with frozen nulls and conditional
    calibration invariants, so this takes the `data` object directly. It is
    deliberately tolerant of missing keys: a structurally broken payload is
    reported as violations rather than raising, so a caller that runs structure
    and semantics together never loses the semantic findings.
    """
    if not isinstance(data, dict):
        return ["data is not an object"]

    out: List[str] = []
    signal = data.get("signal")
    meta = data.get("meta")
    levels = data.get("levels")
    context = data.get("context")

    for name, container, keys in (
        ("signal", signal, FROZEN_NULL_SIGNAL),
        ("meta", meta, FROZEN_NULL_META),
        ("context", context, FROZEN_NULL_CONTEXT),
    ):
        if not isinstance(container, dict):
            out.append(f"{name} is missing or not an object")
            continue
        for key in keys:
            if key not in container:
                out.append(f"{name}.{key} is absent (must be present, null)")
            elif container[key] is not None:
                out.append(f"{name}.{key} is non-null (frozen null this release)")

    if not isinstance(levels, dict):
        out.append("levels is missing or not an object")
    else:
        for key in FROZEN_NULL_LEVELS:
            if key not in levels:
                out.append(f"levels.{key} is absent (must be present, null)")
            elif levels[key] is not None:
                out.append(f"levels.{key} is non-null (frozen null this release)")

    if isinstance(meta, dict) and meta.get("score_is_probability") is not False:
        out.append("meta.score_is_probability is not false (E07)")

    if isinstance(signal, dict):
        probability = signal.get("probability")
        calibrated = signal.get("probability_calibrated")
        if probability is None:
            if calibrated is not False:
                out.append(
                    "signal.probability is null but probability_calibrated is not false"
                )
        elif calibrated is not True:
            out.append(
                "signal.probability is non-null without probability_calibrated:true"
            )

    return out


# --------------------------------------------------------------------------
# Combined entry point.
# --------------------------------------------------------------------------


def load_schema(path: Optional[str] = None) -> Dict[str, Any]:
    with open(path or SCHEMA_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def analysis_contract_violations(
    payload: Any,
    schema: Optional[Dict[str, Any]] = None,
    endpoint: str = ANALYSIS_ENDPOINT,
) -> List[str]:
    """All contract violations in one `analysis/latest` response body.

    Runs the structural layer first; if the payload does not even conform to the
    schema, the semantic invariant check is not meaningful, so the structural
    error is returned alone.
    """
    schema = schema if schema is not None else load_schema()
    spec = schema["endpoints"][endpoint]
    try:
        validate_envelope(endpoint, spec, payload)
    except ContractError as exc:
        return [f"structure: {exc}"]
    return [f"invariant: {v}" for v in frozen_violations(payload["data"])]


def require_valid_analysis(
    payload: Any,
    schema: Optional[Dict[str, Any]] = None,
    endpoint: str = ANALYSIS_ENDPOINT,
) -> None:
    """Raise `ContractError` listing every violation, else return None."""
    violations = analysis_contract_violations(payload, schema=schema, endpoint=endpoint)
    if violations:
        raise ContractError("; ".join(violations))
