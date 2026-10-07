#!/usr/bin/env python3
"""T22 - analysis-API schema fixtures: validate every fixture against the frozen schema.

Every file under tests/fixtures/api_v1/ is derived from
docs/architecture/API_V1_SCHEMA.json. This checker enforces that derivation:

  1. each valid/ payload passes mock_api.validate_envelope (envelope + data);
  2. each invalid/ payload is REJECTED (a fixture that disagrees with the
     schema is a bug in the fixture, so a fixture that accidentally validates
     is itself a failure);
  3. each errors/ body matches the schema error_schema;
  4. the frozen-null contract holds in the uncalibrated default:
     probability/probability_calibrated/levels.*/meta.score_is_probability
     are null/false, and score is present (E07 / RULE C).

Exit code 0 = all checks passed.
"""

from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FIXTURES = os.path.join(REPO, "tests", "fixtures", "api_v1")
sys.path.insert(0, os.path.join(REPO, "scripts"))
import mock_api  # noqa: E402

# Maps a fixture filename to the schema route it is derived from.
VALID_ROUTES = {
    "system_state.json": "GET /api/v1/system/state",
    "health.json": "GET /api/v1/health",
    "health_v1.json": "GET /api/v1/health/v1",
    "timeframes.json": "GET /api/v1/timeframes",
    "timeframe_snapshot.json": "GET /api/v1/timeframes/{tf}/snapshot",
    "analysis_latest.json": "GET /api/v1/analysis/latest",
    "analysis_latest_calibrated.json": "GET /api/v1/analysis/latest",
    "analysis_history.json": "GET /api/v1/analysis/history",
    "context_latest.json": "GET /api/v1/context/latest",
    "bridge_status.json": "GET /api/v1/bridge/status",
    "risk_latest.json": "GET /api/v1/risk/latest",
    "shadow_positions.json": "GET /api/v1/shadow/positions",
    "shadow_outcomes.json": "GET /api/v1/shadow/outcomes",
    "research_status.json": "GET /api/v1/research/status",
    "governance_status.json": "GET /api/v1/governance/status",
    "audit_recent.json": "GET /api/v1/audit/recent",
}

INVALID_ROUTES = {
    "missing_levels_field.json": "GET /api/v1/analysis/latest",
    "bad_coverage_tier_const.json": "GET /api/v1/analysis/latest",
    "probability_above_one.json": "GET /api/v1/analysis/latest",
    "probability_below_zero.json": "GET /api/v1/analysis/latest",
    "not_enveloped.json": "GET /api/v1/analysis/latest",
    "missing_envelope_schema_key.json": "GET /api/v1/analysis/latest",
    "wrong_envelope_api_const.json": "GET /api/v1/analysis/latest",
}

_results = []


def check(name, ok, detail=""):
    _results.append(ok)
    print(("[PASS] " if ok else "[FAIL] ") + name + ("" if ok else f" - {detail}"))


def load(rel):
    with open(os.path.join(FIXTURES, rel), "r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    with open(mock_api.SCHEMA_PATH, "r", encoding="utf-8") as handle:
        schema = json.load(handle)
    endpoints = schema["endpoints"]

    for name, route in VALID_ROUTES.items():
        payload = load(os.path.join("valid", name))
        try:
            mock_api.validate_envelope(route, endpoints[route], payload)
            check(f"valid/{name} conforms to {route}", True)
        except Exception as exc:  # noqa: BLE001
            check(f"valid/{name} conforms to {route}", False, str(exc))

    for name, route in INVALID_ROUTES.items():
        payload = load(os.path.join("invalid", name))
        try:
            mock_api.validate_envelope(route, endpoints[route], payload)
            check(f"invalid/{name} is REJECTED", False, "unexpectedly validated")
        except Exception:  # noqa: BLE001
            check(f"invalid/{name} is REJECTED", True)

    # semantic/: structurally schema-valid, but they violate a contract
    # invariant the schema cannot encode (a schema cannot say "this boolean
    # must be false when probability is null"). The fixture is correct only if
    # it passes structural validation AND trips the invariant check below.
    for name, route in (("uncalibrated_claims_probability.json", "GET /api/v1/analysis/latest"),
                        ("uncalibrated_non_null_levels.json", "GET /api/v1/analysis/latest")):
        payload = load(os.path.join("semantic", name))
        try:
            mock_api.validate_envelope(route, endpoints[route], payload)
            check(f"semantic/{name} is structurally schema-valid", True)
        except Exception as exc:  # noqa: BLE001
            check(f"semantic/{name} is structurally schema-valid", False, str(exc))

    def invariant_violations(data):
        out = []
        sig, meta, lv = data["signal"], data["meta"], data["levels"]
        if sig["probability"] is None and meta["score_is_probability"]:
            out.append("score_is_probability=true while probability is null (E07)")
        if sig["probability"] is None and any(v is not None for v in lv.values()):
            out.append("levels present while uncalibrated")
        return out

    for name in ("uncalibrated_claims_probability.json", "uncalibrated_non_null_levels.json"):
        data = load(os.path.join("semantic", name))["data"]
        v = invariant_violations(data)
        check(f"semantic/{name} trips an invariant", len(v) > 0)

    # and the valid default trips no invariant
    check("valid/analysis_latest.json trips no invariant",
          invariant_violations(load(os.path.join("valid", "analysis_latest.json"))["data"]) == [])

    error_schema = schema["error_schema"]
    for name in ("503_missing_dependency.json", "404_unknown_route.json",
                 "405_method_not_allowed.json"):
        body = load(os.path.join("errors", name))
        ok = all(k in body for k in error_schema["required"]) and body.get("error") == "true"
        check(f"errors/{name} matches error_schema", ok, str(body))

    # Frozen-null contract (E07 / RULE C): the uncalibrated default must not
    # present a score as a probability, and must not carry SL/TP levels.
    uncal = load(os.path.join("valid", "analysis_latest.json"))["data"]
    sig, meta, lv = uncal["signal"], uncal["meta"], uncal["levels"]
    check("uncalibrated: probability is null", sig["probability"] is None)
    check("uncalibrated: probability_calibrated is false",
          sig["probability_calibrated"] is False)
    check("uncalibrated: score is present", sig["score"] is not None)
    check("uncalibrated: score_is_probability is false",
          meta["score_is_probability"] is False)
    check("uncalibrated: every level is null",
          all(lv[k] is None for k in lv))

    cal = load(os.path.join("valid", "analysis_latest_calibrated.json"))["data"]
    check("calibrated: probability in [0,1]",
          isinstance(cal["signal"]["probability"], (int, float))
          and 0.0 <= cal["signal"]["probability"] <= 1.0)
    check("calibrated: probability_calibrated is true",
          cal["signal"]["probability_calibrated"] is True)
    check("calibrated: score_is_probability still false (E07)",
          cal["meta"]["score_is_probability"] is False)

    failed = _results.count(False)
    print(f"\n{len(_results)} check(s), {failed} failed")
    print("RESULT: " + ("PASS" if failed == 0 else "FAIL"))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
