#!/usr/bin/env python3
"""T30(b) - exact-shape assertion for the MOCK payloads (mock-keys <= schema-keys).

The mock must emit exactly the frozen schema's declared shape (no extra keys), so
that fixtures derived from it and the host both conform. This is the mock-side twin
of the real-host assertion in test_e2e_real_host_t13.py.

Exit 0 = conformant. Reads scripts/mock_api.py + the frozen schema; no network.
"""

from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
import mock_api  # noqa: E402
import schema_shape  # noqa: E402

_results = []


def check(name, ok, detail=""):
    _results.append(ok)
    print(("[PASS] " if ok else "[FAIL] ") + name + (f" - {detail}" if detail and not ok else ""))


def main() -> int:
    schema = schema_shape.load_schema(mock_api.SCHEMA_PATH)
    payloads = mock_api.build_payloads(schema, calibrated=False)

    check("mock emits a payload for every frozen route",
          set(payloads) == set(schema["endpoints"]),
          f"mock-only={set(payloads) - set(schema['endpoints'])} "
          f"schema-only={set(schema['endpoints']) - set(payloads)}")

    problems = schema_shape.undeclared_by_route(schema, payloads)
    for route in sorted(schema["endpoints"]):
        extra = problems.get(route, [])
        check(f"{route} mock emits only schema-declared keys", not extra, "; ".join(extra))

    passed = sum(1 for ok in _results if ok)
    total = len(_results)
    print(f"\n{passed}/{total} checks passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
