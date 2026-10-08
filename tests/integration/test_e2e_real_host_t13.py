#!/usr/bin/env python3
"""T13 - end-to-end against the REAL backend host (not the mock).

Complements T24 (tests/integration/test_e2e_frozen_v1.py), which drives the
synthetic mock server through scripts/mock_api.py. This harness drives the
production binary build/aura_backend_host over a real loopback socket and checks
the frozen v1 contract holds on the *real* implementation:

  1. the real host answers every route in the frozen schema with HTTP 200 and a
     response that validates via Agent-B's shared T23 reader
     (src/models/contract_checker.py) - the same single frozen-set reader used by
     F17-1 and the T24 harness;
  2. RULE C / E06 / E07 hold on /analysis/latest: probability null,
     probability_calibrated false, score_is_probability false;
  3. the host is honest about its startup posture: with no staged bridge the
     system/state reports DEGRADED + shadow_only, never a false "ready".

This closes F24-1's gap (T24 only exercised the mock) at the real-host level. The
real host is NOT app-root relocatable (PathResolver derives appRootDir from the
binary's directory), so a fresh checkout starts DEGRADED - that is reported, not
hidden. The evidential (real XAUUSD data) PASS remains gated on E05; this
exercises the real binary on the synthetic/no-data path only.

Exit 0 = the frozen v1 contract holds end to end on the real host. Skips (exit 0
with a notice) when the host has not been built in this checkout. Reads the binary;
never mutates anything.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
sys.path.insert(0, os.path.join(REPO, "src", "models"))
import mock_api  # noqa: E402
import contract_checker  # noqa: E402
import schema_shape  # noqa: E402

HOST_BIN = os.path.join(REPO, "build", "aura_backend_host")
SCHEMA_PATH = mock_api.SCHEMA_PATH

_results = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    _results.append(ok)
    print(("[PASS] " if ok else "[FAIL] ") + name + (f" - {detail}" if detail and not ok else ""))
    return ok


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def fetch(url: str, timeout: float = 5.0):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


def wait_for(url: str, attempts: int = 50):
    last = None
    for _ in range(attempts):
        try:
            return fetch(url)
        except Exception as exc:  # noqa: BLE001 - last error surfaced below
            last = exc
            time.sleep(0.1)
    raise RuntimeError(f"real host did not serve: {last}")


def main() -> int:
    if not os.path.exists(HOST_BIN):
        print(f"[SKIP] {HOST_BIN} not built; run `cmake --build build` first")
        return 0

    with open(SCHEMA_PATH, "r", encoding="utf-8") as handle:
        schema = json.load(handle)

    port = free_port()
    # No --once: the host stops right after its first cycle and the API would be
    # unreachable. Run it as a server and terminate it ourselves.
    proc = subprocess.Popen(
        [HOST_BIN, "--api-port", str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    base = f"http://127.0.0.1:{port}"
    try:
        wait_for(base + "/api/v1/system/state")

        for route, spec in schema["endpoints"].items():
            method, _, path = route.partition(" ")
            assert method == "GET", route
            live_path = path.replace("{tf}", "M15")
            try:
                status, body = fetch(base + live_path)
            except urllib.error.HTTPError as exc:
                check(f"{route} reachable", False, f"HTTP {exc.code}")
                continue
            check(f"{route} status 200", status == 200, str(status))
            try:
                contract_checker.validate_envelope(route, spec, body)
                check(f"{route} matches frozen schema (real host)", True)
            except Exception as exc:  # noqa: BLE001
                check(f"{route} matches frozen schema (real host)", False, repr(exc))
            # T30(b): the exact-shape teeth (two-sided) - the real host must emit
            # no key the schema does not declare, and must supply every required
            # key. Forward-looking: also covers state-dependent fields (e.g.
            # freshness sub-fields) once real data flows under E05.
            found = schema_shape.check_exact_shape(route, spec, body)
            detail = "; ".join(f"{k}: {'; '.join(v)}" for k, v in sorted(found.items()))
            check(f"{route} is exact-shape (real host)", not found, detail)

        # RULE C on the real host: synthetic/unstaged => probability stays null.
        _, latest = fetch(base + "/api/v1/analysis/latest")
        signal = latest["data"]["signal"]
        check("real host analysis probability null (RULE C)",
              signal["probability"] is None, str(signal["probability"]))
        check("real host probability_calibrated false",
              signal["probability_calibrated"] is False)
        check("real host score_is_probability false",
              latest["data"]["meta"]["score_is_probability"] is False)
        violations = contract_checker.analysis_contract_violations(latest, schema)
        check("real host analysis passes semantic invariants", not violations,
              "; ".join(violations))

        # Honest posture: no staged bridge => DEGRADED, shadow_only, never ready.
        _, state = fetch(base + "/api/v1/system/state")
        data = state["data"]
        check("real host reports mode DEGRADED (no staged bridge)",
              data.get("mode") == "DEGRADED", str(data.get("mode")))
        check("real host shadow_only true during DEGRADED",
              data.get("shadow_only") is True)
        check("real host does not claim ready (no real data, E05)",
              data.get("ready") is False)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    passed = sum(1 for ok in _results if ok)
    total = len(_results)
    print(f"\n{passed}/{total} checks passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
