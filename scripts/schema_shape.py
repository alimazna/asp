#!/usr/bin/env python3
"""T30(b) - exact-shape helper: every key a payload emits must be declared.

The frozen schema validators visit only *declared* keys, so a payload that emits
extra keys passes every suite. This closes that blind spot: given a route spec and
a live payload, it reports any emitted key path that the schema does not declare.
Empty result == exact-shape conformant.

Handles all three declaration dialects used by API_V1_SCHEMA.json:
  * data_properties / data_required
  * element_properties / element_required   (arrays)
  * properties (+ nested element_properties) at any depth
and both path-spelling conventions by normalising array indices to ``.[]``.

Used by tests/integration/test_e2e_real_host_t13.py (host) and
tests/integration/test_mock_shape_t30.py (mock). Reads nothing; pure functions.
"""

from __future__ import annotations

import json
import os
from typing import Dict, List, Set

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SCHEMA_PATH = os.path.join(REPO_ROOT, "docs", "architecture", "API_V1_SCHEMA.json")


def load_schema(path: str = SCHEMA_PATH) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _props(props: dict, segs: List[str], out: Set[str]) -> None:
    for key, spec in (props or {}).items():
        child = segs + [key]
        out.add(".".join(child))
        _value(spec, child, out)


def _value(spec: dict, segs: List[str], out: Set[str]) -> None:
    if not isinstance(spec, dict):
        return
    if spec.get("properties") is not None or spec.get("type") == "object":
        _props(spec.get("properties") or {}, segs, out)
    if spec.get("type") == "array" or "element_properties" in spec or "element_required" in spec:
        elem = segs + ["[]"]
        out.update(".".join(elem + [k]) for k in (spec.get("element_required") or []))
        _props(spec.get("element_properties") or {}, elem, out)
        if isinstance(spec.get("items"), dict):
            _value(spec["items"], elem, out)


def declared_paths(spec: dict) -> Set[str]:
    """The set of key paths the schema declares for one endpoint."""
    out: Set[str] = set(spec.get("required") or [])
    if spec.get("data_properties") is not None:
        out.update(spec.get("data_required") or [])
        _props(spec.get("data_properties") or {}, ["data"], out)
    if spec.get("data_type") == "array":
        out.add("data")
        out.update("data.[]." + k for k in (spec.get("element_required") or []))
        _props(spec.get("element_properties") or {}, ["data", "[]"], out)
    return out


def live_paths(value, segs: List[str] | None = None) -> Set[str]:
    """Every key path present in a payload; array indices normalised to ``[]``."""
    if segs is None:
        segs = []
    out: Set[str] = set()
    if isinstance(value, dict):
        for key, child in value.items():
            out.add(".".join(segs + [key]))
            out |= live_paths(child, segs + [key])
    elif isinstance(value, list):
        for elem in value:
            out |= live_paths(elem, segs + ["[]"])
    return out


def required_paths(spec: dict) -> Set[str]:
    """Key paths the schema marks as required (envelope + data + array element)."""
    out: Set[str] = set(spec.get("required") or [])
    if spec.get("data_properties") is not None:
        out.update("data." + k for k in (spec.get("data_required") or []))
    if spec.get("data_type") == "array":
        out.update("data.[]." + k for k in (spec.get("element_required") or []))
    return out


def missing_required(route: str, spec: dict, payload) -> List[str]:
    """Required paths the payload fails to supply (schema-required <= payload-keys).

    Array-element requirements (``...[]...``) are vacuous when the array is absent
    or empty: there is no element to violate them, and an absent ``data`` is
    already reported by its own required path.
    """
    seen = live_paths(payload)
    missing: List[str] = []
    for path in required_paths(spec):
        segs = path.split(".")
        if "[]" in segs:
            node = payload
            navigable = True
            for seg in segs[: segs.index("[]")]:
                if isinstance(node, dict) and seg in node:
                    node = node[seg]
                else:
                    navigable = False
                    break
            if not navigable or not isinstance(node, list) or not node:
                continue
        if path not in seen:
            missing.append(path)
    return sorted(missing)


def undeclared(route: str, spec: dict, payload) -> List[str]:
    """Key paths the payload emits that the route spec does not declare."""
    return sorted(live_paths(payload) - declared_paths(spec))


def check_exact_shape(route: str, spec: dict, payload) -> Dict[str, List[str]]:
    """Two-sided exact-shape check: extra (undeclared) + missing (required).

    ``payload-keys <= schema-keys`` (no extras) AND
    ``schema-required <= payload-keys`` (nothing required is missing).
    Returns ``{}`` when both hold.
    """
    problems: Dict[str, List[str]] = {}
    extra = undeclared(route, spec, payload)
    if extra:
        problems["undeclared"] = extra
    missing = missing_required(route, spec, payload)
    if missing:
        problems["missing_required"] = missing
    return problems


def undeclared_by_route(schema: dict, payloads: Dict[str, object]) -> Dict[str, List[str]]:
    """Route -> undeclared key paths, for every route in ``payloads``."""
    problems: Dict[str, List[str]] = {}
    for route, payload in payloads.items():
        extra = undeclared(route, schema["endpoints"][route], payload)
        if extra:
            problems[route] = extra
    return problems


if __name__ == "__main__":
    import sys as _sys
    schemax = load_schema()
    print("routes:", len(schemax["endpoints"]))
    _sys.exit(0)
