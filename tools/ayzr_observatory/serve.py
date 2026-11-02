#!/usr/bin/env python3
"""Same-origin proxy for the AYZR observatory.

Serves the static observatory page and forwards a small allowlist of read-only
GET routes to the AYZR backend. This exists so a browser can reach the page and
the API on ONE origin: the backend binds loopback only (never exposed), and the
browser refuses a cross-origin call to a different host.

Security properties (see docs/ayzr/OBSERVATORY_SECURITY.md):

  * The read-only client token lives in the proxy's environment
    (``AYZR_CLIENT_TOKEN``) and is added here. The browser never holds it, never
    puts it in a URL, and never stores it.
  * Any ``Authorization`` header the browser sends is dropped, so a page on
    another origin cannot borrow the proxy's credential.
  * Only GET/HEAD is proxied, and only to an explicit allowlist of read paths.
    POST/PUT/PATCH/DELETE and unknown paths are refused before any upstream
    call, so the proxy cannot be used as an open relay to write endpoints.
  * ``AYZR_BACKEND`` must resolve to loopback; a non-loopback target is refused
    at startup and per request, so the proxy cannot be turned into an SSRF
    vector against an arbitrary host.
  * The listener binds 127.0.0.1 by default.
  * Logs carry the method and path only -- never headers or bodies, which is
    where a token would appear.
"""
import http.server
import ipaddress
import os
import socketserver
import sys
import urllib.error
import urllib.parse
import urllib.request

WEB_ROOT = os.path.dirname(os.path.abspath(__file__))

DEFAULT_BACKEND = "http://127.0.0.1:8081"
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = "12000"

# Read-only routes the observatory page is allowed to reach. Exact match for the
# fixed paths; prefix match for the routes that take a query string.
GET_ALLOWLIST_EXACT = {
    "/api/v1/livez",
    "/api/v1/readyz",
    "/api/v1/health/v1",
    "/api/v1/candles",
    "/api/v1/sync/latest",
    "/api/v1/news",
    "/api/v1/news/analyze",
    "/api/v1/news/results",
    "/api/v1/news/quota",
    "/api/v1/history",
    "/api/v1/activity",
    "/api/v1/providers",
    "/api/v1/events",
    "/api/v1/events/detail",
    "/api/v1/ayzr/engines",
    "/api/v1/ayzr/trades",
    "/api/v1/ayzr/trades/detail",
}
GET_ALLOWLIST_PREFIX = (
    "/api/v1/candles?",
    "/api/v1/news?",
    "/api/v1/news/analyze?",
    "/api/v1/news/results?",
    "/api/v1/events?",
    "/api/v1/events/detail?",
    "/api/v1/activity?",
    "/api/v1/ayzr/trades?",
    "/api/v1/ayzr/trades/detail?",
)

ALLOWED_METHODS = ("GET", "HEAD")


def is_loopback_host(host: str) -> bool:
    """True if *host* names the local machine (so the proxy can never reach out)."""
    if not host:
        return False
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def validate_backend(url: str) -> str:
    """Return *url* (trailing slash stripped) if it is a loopback http(s) URL."""
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https"):
        raise SystemExit(
            f"refusing AYZR_BACKEND '{url}': scheme must be http or https")
    if not is_loopback_host(parsed.hostname or ""):
        raise SystemExit(
            f"refusing AYZR_BACKEND '{url}': host must be loopback "
            f"(127.0.0.0/8, ::1 or localhost). A remote backend is not "
            f"permitted: this proxy must not be an SSRF vector.")
    return url.rstrip("/")


def path_is_allowed(path_with_query: str) -> bool:
    """True if the request path (with query) is on the read-only allowlist."""
    # Reject control characters and any absolute-form URL: both are used to
    # smuggle a different host past a naive prefix check.
    if any(ord(c) < 0x20 for c in path_with_query):
        return False
    if "://" in path_with_query:
        return False
    if not path_with_query.startswith("/"):
        return False
    if path_with_query in GET_ALLOWLIST_EXACT:
        return True
    return any(path_with_query.startswith(p) for p in GET_ALLOWLIST_PREFIX)


def make_handler(backend: str, token: str):
    class Handler(http.server.SimpleHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "AYZRObservatory/1.0"

        def __init__(self, *a, **kw):
            super().__init__(*a, directory=WEB_ROOT, **kw)

        def log_message(self, fmt, *args):
            # Method + path only. Headers and bodies can carry the token, so
            # they are never logged.
            sys.stderr.write("  %s\n" % (fmt % args))

        def _reject(self, code: int, message: str):
            body = ('{"error":"true","code":"%s","message":"%s"}'
                    % (code, message)).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _proxy(self):
            # Only the path+query travels upstream; the scheme and host are
            # fixed here, so a crafted path cannot redirect the request.
            if not path_is_allowed(self.path):
                return self._reject(403, "path_not_allowed")

            url = backend + self.path
            # A browser-supplied Authorization is deliberately NOT forwarded;
            # the proxy owns the credential. Cookies are not forwarded either.
            req = urllib.request.Request(url, method="GET")
            if token:
                req.add_header("Authorization", "Bearer " + token)
            req.add_header("Accept", "application/json")
            try:
                with urllib.request.urlopen(req, timeout=15) as r:
                    data = r.read()
                    self.send_response(r.status)
                    for k, v in r.headers.items():
                        if k.lower() not in ("transfer-encoding", "connection",
                                             "content-length"):
                            self.send_header(k, v)
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
            except urllib.error.HTTPError as e:
                data = e.read()
                self.send_response(e.code)
                self.send_header(
                    "Content-Type",
                    e.headers.get("Content-Type", "application/json"))
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            except Exception:
                # Never surface the upstream exception text: it can name the
                # host and internals. One generic, actionable message.
                self._reject(502, "backend_unreachable")

        def do_GET(self):
            if self.path.startswith("/api/"):
                return self._proxy()
            return super().do_GET()

        def do_HEAD(self):
            if self.path.startswith("/api/"):
                return self._proxy()
            return super().do_HEAD()

        # Every mutating method is refused outright, for /api/* and for static
        # paths alike. The observatory is a read-only surface.
        def _refuse_mutation(self):
            return self._reject(405, "method_not_allowed")

        do_POST = _refuse_mutation
        do_PUT = _refuse_mutation
        do_PATCH = _refuse_mutation
        do_DELETE = _refuse_mutation

    return Handler


class Reuse(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def build_server(backend=None, host=None, port=None, token=None):
    """Construct the server without serving. Used by tests and __main__."""
    backend = validate_backend(
        backend if backend is not None
        else os.environ.get("AYZR_BACKEND", DEFAULT_BACKEND))
    host = host if host is not None else os.environ.get("AYZR_HOST", DEFAULT_HOST)
    port = int(port if port is not None else os.environ.get("PORT", DEFAULT_PORT))
    token = token if token is not None else os.environ.get("AYZR_CLIENT_TOKEN", "")
    if not is_loopback_host(host):
        raise SystemExit(
            f"refusing to bind {host}: the observatory binds loopback only. "
            f"Set AYZR_HOST to a loopback address.")
    return Reuse((host, port), make_handler(backend, token))


if __name__ == "__main__":
    httpd = build_server()
    print("AYZR observatory on http://%s:%d (loopback, read-only) -> %s"
          % (httpd.server_address[0], httpd.server_address[1],
             os.environ.get("AYZR_BACKEND", DEFAULT_BACKEND)))
    httpd.serve_forever()
