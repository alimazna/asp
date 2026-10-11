"""Deliberately slow Python-bridge stand-in for the concurrency regression.

Stands in for the external bridge only. /v1/health answers immediately;
/v1/candles sleeps for ``--delay`` seconds before answering. The backend host
proxies /candles to this process, so a request to the backend's own
/api/v1/candles holds a bridge read for the whole delay. That is exactly the
real cold-MT5 read that used to serialize the backend's accept loop and starve
/api/v1/health.
"""

from __future__ import annotations

import argparse
import http.server
import json
import socketserver
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--delay", type=float, default=3.0)
    args = parser.parse_args()

    delay = args.delay

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 - http.server API
            if self.path.startswith("/v1/candles"):
                time.sleep(delay)
                payload = {
                    "status": "OK",
                    "payload": {
                        "bars": [
                            {"time": 1, "open": 1, "high": 2, "low": 0.5,
                             "close": 1.5, "tick_volume": 1, "spread": 0,
                             "real_volume": 0}
                        ],
                        "timeframe": "M15",
                        "symbol": "XAUUSD",
                        "count": 1,
                        "closed_only": True,
                    },
                }
            elif self.path.startswith("/v1/health"):
                payload = {"status": "OK",
                           "payload": {"process_state": "ONLINE"}}
            else:
                payload = {}
            body = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a) -> None:  # noqa: ANN002 - silence
            pass

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        daemon_threads = True
        allow_reuse_address = True

    with Server((args.host, args.port), Handler) as httpd:
        httpd.serve_forever()


if __name__ == "__main__":
    main()
