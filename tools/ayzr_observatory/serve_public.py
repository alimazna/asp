#!/usr/bin/env python3
"""Public launcher for the AYZR observatory.

`serve.py` deliberately binds loopback only, which makes the surface
unreachable from a browser outside the host. This launcher reuses the exact
same handler (`make_handler`) -- same read-only allowlist, same token handling,
same refusal of write-shaped paths -- but binds every interface so the
environment's public URL can reach it.

This widens *reachability*, not *permission*: the surface is still read-only
and still needs `AYZR_CLIENT_TOKEN` server-side. Only run it where the port is
meant to be reachable (a preview/tunnel), never on a host that should stay
private. Guarded by `AYZR_ALLOW_PUBLIC=1` so it cannot be started by accident.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serve  # noqa: E402  (same-directory import of the hardened handler)


def main() -> int:
    if os.environ.get("AYZR_ALLOW_PUBLIC") != "1":
        print("refusing to start: set AYZR_ALLOW_PUBLIC=1 to expose the "
              "read-only observatory beyond loopback.", file=sys.stderr)
        return 2

    backend = serve.validate_backend(
        os.environ.get("AYZR_BACKEND", serve.DEFAULT_BACKEND))
    host = os.environ.get("AYZR_HOST", "0.0.0.0")
    port = int(os.environ.get("PORT", serve.DEFAULT_PORT))
    token = os.environ.get("AYZR_CLIENT_TOKEN", "")

    httpd = serve.Reuse((host, port), serve.make_handler(backend, token))
    print("AYZR observatory (PUBLIC, read-only) on http://%s:%d -> %s"
          % (host, port, backend))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
