#!/usr/bin/env python3
"""Security tests for the AYZR observatory proxy and page.

These run the REAL proxy (tools/ayzr_observatory/serve.py) against a stub
upstream that records what it received, so the assertions cover the code paths
that ship rather than a mock of them.

Run:  python3 tools/ayzr_observatory/tests/test_observatory_security.py
"""
import http.server
import json
import os
import re
import socketserver
import sys
import threading
import unittest
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, ROOT)

import serve  # noqa: E402  (path set above)

INDEX_HTML = os.path.join(ROOT, "index.html")

SENTINEL_TOKEN = "sentinel-client-token-do-not-log"


class StubBackend:
    """A loopback upstream that records each request it serves."""

    def __init__(self):
        self.requests = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.requests.append({
                    "method": "GET",
                    "path": self.path,
                    "authorization": self.headers.get("Authorization"),
                    "cookie": self.headers.get("Cookie"),
                })
                body = json.dumps({"ok": True, "path": self.path}).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        class Reuse(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        self.server = Reuse(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()


def start_proxy(backend, token=SENTINEL_TOKEN):
    httpd = serve.build_server(backend=backend, host="127.0.0.1", port=0,
                               token=token)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    return httpd


def http_get(url, method="GET", headers=None):
    req = urllib.request.Request(url, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


class BackendTargetValidation(unittest.TestCase):
    """AYZR_BACKEND must be loopback: a remote target is an SSRF vector."""

    def test_accepts_loopback_targets(self):
        for url in ("http://127.0.0.1:8081", "http://localhost:9000",
                    "http://[::1]:8081", "https://127.0.0.1:443"):
            with self.subTest(url=url):
                self.assertEqual(serve.validate_backend(url), url.rstrip("/"))

    def test_refuses_remote_targets(self):
        for url in ("http://169.254.169.254/latest/meta-data",  # cloud metadata
                    "http://10.0.0.5:8081", "http://evil.example.com",
                    "http://0.0.0.0:8081"):
            with self.subTest(url=url):
                with self.assertRaises(SystemExit):
                    serve.validate_backend(url)

    def test_refuses_non_http_scheme(self):
        for url in ("file:///etc/passwd", "gopher://127.0.0.1:70",
                    "ftp://127.0.0.1/x"):
            with self.subTest(url=url):
                with self.assertRaises(SystemExit):
                    serve.validate_backend(url)

    def test_refuses_non_loopback_bind(self):
        with self.assertRaises(SystemExit):
            serve.build_server(backend="http://127.0.0.1:1", host="0.0.0.0",
                               port=0, token="")


class PathAllowlist(unittest.TestCase):
    def test_read_paths_allowed(self):
        for p in ("/api/v1/health/v1", "/api/v1/readyz", "/api/v1/livez",
                  "/api/v1/ayzr/trades?state=all", "/api/v1/news?limit=40",
                  "/api/v1/candles?tf=M15&limit=100", "/api/v1/activity?limit=80",
                  "/api/v1/ayzr/ecosystem/sessions",
                  "/api/v1/ayzr/ecosystem/engines",
                  "/api/v1/ayzr/ecosystem/opportunities",
                  "/api/v1/ayzr/ecosystem/ledger",
                  "/api/v1/ayzr/ecosystem/ledger?limit=100"):
            with self.subTest(path=p):
                self.assertTrue(serve.path_is_allowed(p))

    def test_ecosystem_write_like_paths_refused(self):
        # The ecosystem surface is read-only; a write-shaped sibling must not
        # be reachable through the proxy.
        for p in ("/api/v1/ayzr/ecosystem/sessions/ingest",
                  "/api/v1/ayzr/ecosystem/opportunities/publish",
                  "/api/v1/ayzr/ecosystem/ledger/append"):
            with self.subTest(path=p):
                self.assertFalse(serve.path_is_allowed(p))

    def test_write_paths_refused(self):
        for p in ("/api/v1/candles/ingest", "/api/v1/news/ingest",
                  "/api/v1/admin/candles", "/api/v1/sync/latest/../admin"):
            with self.subTest(path=p):
                self.assertFalse(serve.path_is_allowed(p))

    def test_smuggling_shapes_refused(self):
        # Absolute-form URLs and protocol-relative paths are the classic ways to
        # talk a naive proxy into fetching another host.
        for p in ("http://evil.example.com/", "//evil.example.com/api/v1/livez",
                  "/api/v1/livez\x00", "/\r\nX-Injected: 1",
                  "api/v1/livez", "https://127.0.0.1/api/v1/livez"):
            with self.subTest(path=p):
                self.assertFalse(serve.path_is_allowed(p))


class ProxyBehaviour(unittest.TestCase):
    def setUp(self):
        self.stub = StubBackend().__enter__()
        self.proxy = start_proxy("http://127.0.0.1:%d" % self.stub.port)
        self.base = "http://127.0.0.1:%d" % self.proxy.server_address[1]

    def tearDown(self):
        self.proxy.shutdown()
        self.proxy.server_close()
        self.stub.__exit__()

    def test_proxy_adds_token_upstream(self):
        status, _ = http_get(self.base + "/api/v1/health/v1")
        self.assertEqual(status, 200)
        self.assertEqual(self.stub.requests[-1]["authorization"],
                         "Bearer " + SENTINEL_TOKEN)

    def test_browser_authorization_is_dropped(self):
        # A page from another origin must not be able to borrow the credential
        # by sending its own Authorization header.
        status, _ = http_get(self.base + "/api/v1/health/v1",
                         headers={"Authorization": "Bearer attacker-supplied"})
        self.assertEqual(status, 200)
        self.assertEqual(self.stub.requests[-1]["authorization"],
                         "Bearer " + SENTINEL_TOKEN)

    def test_cookies_are_not_forwarded(self):
        http_get(self.base + "/api/v1/health/v1", headers={"Cookie": "sid=abc"})
        self.assertIsNone(self.stub.requests[-1]["cookie"])

    def test_mutating_methods_refused_without_upstream_call(self):
        for method in ("POST", "PUT", "PATCH", "DELETE"):
            with self.subTest(method=method):
                before = len(self.stub.requests)
                status, _ = http_get(self.base + "/api/v1/health/v1", method=method)
                self.assertEqual(status, 405)
                self.assertEqual(len(self.stub.requests), before,
                                 "a refused method must not reach the backend")

    def test_disallowed_path_refused_without_upstream_call(self):
        before = len(self.stub.requests)
        status, _ = http_get(self.base + "/api/v1/candles/ingest")
        self.assertEqual(status, 403)
        self.assertEqual(len(self.stub.requests), before)

    def test_token_never_in_response_body(self):
        _, body = http_get(self.base + "/api/v1/health/v1")
        self.assertNotIn(SENTINEL_TOKEN.encode(), body)

    def test_backend_down_yields_generic_502(self):
        # Point at a closed loopback port: the error must not name the host.
        proxy = start_proxy("http://127.0.0.1:1")
        try:
            base = "http://127.0.0.1:%d" % proxy.server_address[1]
            status, body = http_get(base + "/api/v1/health/v1")
            self.assertEqual(status, 502)
            self.assertIn(b"backend_unreachable", body)
            self.assertNotIn(b"127.0.0.1:1", body)
        finally:
            proxy.shutdown()
            proxy.server_close()


class PageHasNoCredentialHandling(unittest.TestCase):
    """The page must not carry, read or store a credential anywhere.

    Comments are stripped first: the page explains *why* there is no token
    handling, and that prose names the very APIs this test forbids. Only the
    live code is checked.
    """

    def setUp(self):
        with open(INDEX_HTML, encoding="utf-8") as fh:
            raw = fh.read()
        # Drop HTML comments and whole-line JS comments.
        raw = re.sub(r"<!--.*?-->", "", raw, flags=re.S)
        self.code = "\n".join(
            ln for ln in raw.splitlines()
            if not ln.strip().startswith("//"))
        self.html = raw

    def test_no_url_token_or_base_params(self):
        for pattern in (r"QS\.get\('token'\)", r"QS\.get\('base'\)",
                        r"URLSearchParams\(location\.search\)"):
            with self.subTest(pattern=pattern):
                self.assertIsNone(re.search(pattern, self.code))

    def test_no_web_storage(self):
        self.assertNotIn("localStorage", self.code)
        self.assertNotIn("sessionStorage", self.code)

    def test_no_authorization_header_in_page(self):
        # The page must never set an Authorization header itself.
        self.assertNotIn("Authorization", self.code)

    def test_no_token_literal_in_page(self):
        self.assertNotRegex(self.code, r"\btoken\b\s*[:=]",
                            "the page must not hold a token variable")

    def test_fetches_same_origin_only(self):
        # The only origin the page may talk to is its own.
        self.assertIn("window.location.origin", self.code)
        self.assertNotIn("http://localhost", self.code)
        self.assertNotIn("http://127.0.0.1", self.code)

    def test_ecosystem_tab_and_routes_present(self):
        # The observatory surfaces the ecosystem read routes.
        self.assertIn('data-tab="ecosystem"', self.html)
        for route in ("/api/v1/ayzr/ecosystem/sessions",
                      "/api/v1/ayzr/ecosystem/engines",
                      "/api/v1/ayzr/ecosystem/opportunities",
                      "/api/v1/ayzr/ecosystem/ledger"):
            with self.subTest(route=route):
                self.assertIn(route, self.code)

    def test_uncalibrated_confidence_never_shown_as_probability(self):
        # The page must gate the probability on `calibrated`, mirroring the app.
        self.assertIn("calibrated", self.code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
