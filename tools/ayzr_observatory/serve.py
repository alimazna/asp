#!/usr/bin/env python3
"""Same-origin proxy for the AYZR observatory.

Serves the static observatory page and forwards /api/* to the AYZR backend.
This exists so a browser can reach the page and the API on ONE origin: the
backend binds loopback only (never exposed), and the browser refuses a
cross-origin call to a different host. The proxy adds no data of its own --
every byte it returns is the backend's own response.
"""
import http.server
import socketserver
import urllib.request
import urllib.error
import os
import sys

WEB_ROOT = os.path.dirname(os.path.abspath(__file__))
BACKEND = os.environ.get("AYZR_BACKEND", "http://127.0.0.1:8081")
PORT = int(os.environ.get("PORT", "12000"))


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=WEB_ROOT, **kw)

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))

    def _proxy(self):
        url = BACKEND + self.path
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length) if length else None
        req = urllib.request.Request(url, data=body, method=self.command)
        # Forward only the credential-bearing headers the backend needs; never
        # echo the client's cookies or the proxy host back to the backend.
        for h in ("Authorization", "Content-Type", "Accept"):
            if h in self.headers:
                req.add_header(h, self.headers[h])
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                data = r.read()
                self.send_response(r.status)
                for k, v in r.headers.items():
                    if k.lower() not in ("transfer-encoding", "connection"):
                        self.send_header(k, v)
                self.end_headers()
                self.wfile.write(data)
        except urllib.error.HTTPError as e:
            data = e.read()
            self.send_response(e.code)
            self.send_header("Content-Type",
                             e.headers.get("Content-Type", "application/json"))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            msg = ('{"error":"true","code":"proxy_unreachable",'
                   '"message":"backend not reachable"}').encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(msg)

    def do_GET(self):
        if self.path.startswith("/api/"):
            return self._proxy()
        return super().do_GET()

    def do_POST(self):
        if self.path.startswith("/api/"):
            return self._proxy()
        self.send_error(405)

    def do_DELETE(self):
        if self.path.startswith("/api/"):
            return self._proxy()
        self.send_error(405)


class Reuse(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with Reuse(("0.0.0.0", PORT), Handler) as httpd:
        print("AYZR observatory on http://0.0.0.0:%d -> %s" % (PORT, BACKEND))
        httpd.serve_forever()
