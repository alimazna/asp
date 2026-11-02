#!/usr/bin/env python3
"""Mock news + economic-calendar source and 20-scenario news test runner.

Why this exists
---------------
The real news path has two halves:

  1. a *source* half -- Google-News RSS (``feed_news.py``) and the economic
     calendar the macro watcher polls; and
  2. a *backend* half -- ``POST /api/v1/news/ingest`` -> store -> analyze.

Exercising either half against the live internet spends the user's quota and is
non-deterministic. This tool replaces the source half with a local server that
speaks the *exact* wire formats the real collectors already parse, and adds a
runner that pushes 20 named news shapes through the real backend pipeline with
the mock LLM provider. No external request is made and no AI token is spent.

What is real and what is not
----------------------------
* The wire formats are real: the RSS XML matches what ``feed_news.py`` parses
  (``<item>/<title>/<link>/<pubDate>/<source>``) and the calendar JSON matches
  what ``CalendarFeed`` parses (``title/country/date/impact/forecast/previous``).
* The backend, its store, its dedup, its schema validation and its analysis
  pipeline are all real -- only the *provider* is mocked (``ASTRA_LLM_PROVIDER``).
* Every synthetic article is marked as test data: the headline is prefixed
  ``[TEST]`` and the source ends with ``(TEST)``. A scenario article can never
  be mistaken for a real report, and the backend stores it verbatim.

Safety
------
The source server binds loopback only and refuses a non-loopback bind. The
runner refuses to point at a non-loopback backend. Nothing here can reach
production.

Usage
-----
    # 1. start the backend (mock provider, no network needed):
    ASTRA_SENDER_TOKEN=s ASTRA_CLIENT_TOKEN=c ASTRA_LLM_PROVIDER=mock \
        ./build-ayzr/astra_server --data-dir /tmp/ayzr-news --port 8790 &

    # 2. run the 20 scenarios end to end and assert the pipeline holds:
    python3 tools/ayzr_agents/mock_news_api.py scenarios \
        --base http://127.0.0.1:8790 --sender-token s --client-token c

    # 3. or just serve the mock source for feed_news.py / the watcher:
    python3 tools/ayzr_agents/mock_news_api.py serve --port 8899
"""
from __future__ import annotations

import argparse
import calendar
import json
import re
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from xml.sax.saxutils import escape as xml_escape

# --------------------------------------------------------------------------
# 20 news scenarios. Each is a distinct shape the pipeline must survive. The
# `body` text is short, synthetic and self-labelled; it is never a real quote.
# `expect` drives the assertions: `gold` (passes the gold-only filter),
# `category`, and `direction_ok` (allowed analysis directions).
# --------------------------------------------------------------------------
GOLD_DIRECTIONS = {"bullish", "bearish", "sideways", "no_trade"}

SCENARIOS: list[dict] = [
    dict(id="inflation_beat", category="inflation", gold="high",
         headline="[TEST] CPI beats expectations as core inflation runs hot",
         body="[TEST DATA] A synthetic hotter-than-forecast CPI print.",
         expect=dict(gold=True, category="inflation")),
    dict(id="inflation_miss", category="inflation", gold="high",
         headline="[TEST] CPI undershoots as price pressures cool",
         body="[TEST DATA] A synthetic softer CPI print.",
         expect=dict(gold=True, category="inflation")),
    dict(id="fed_hawkish", category="monetary_policy", gold="high",
         headline="[TEST] Fed signals a higher-for-longer rate path",
         body="[TEST DATA] A synthetic hawkish policy statement.",
         expect=dict(gold=True, category="monetary_policy")),
    dict(id="fed_dovish", category="monetary_policy", gold="high",
         headline="[TEST] Fed opens the door to earlier rate cuts",
         body="[TEST DATA] A synthetic dovish policy statement.",
         expect=dict(gold=True, category="monetary_policy")),
    dict(id="nfp_strong", category="economic_data", gold="high",
         headline="[TEST] Payrolls surge past consensus",
         body="[TEST DATA] A synthetic strong labour report.",
         expect=dict(gold=True, category="economic_data")),
    dict(id="nfp_weak", category="economic_data", gold="high",
         headline="[TEST] Payrolls miss as hiring stalls",
         body="[TEST DATA] A synthetic weak labour report.",
         expect=dict(gold=True, category="economic_data")),
    dict(id="geopolitical_risk", category="geopolitical", gold="high",
         headline="[TEST] Escalation lifts demand for safe havens",
         body="[TEST DATA] A synthetic geopolitical escalation.",
         expect=dict(gold=True, category="geopolitical")),
    dict(id="usd_strength", category="markets", gold="high",
         headline="[TEST] Dollar index climbs to a multi-week high",
         body="[TEST DATA] A synthetic dollar-strength move.",
         expect=dict(gold=True, category="markets")),
    dict(id="yields_spike", category="markets", gold="high",
         headline="[TEST] Real yields jump after a weak auction",
         body="[TEST DATA] A synthetic yield spike.",
         expect=dict(gold=True, category="markets")),
    dict(id="cb_buying", category="physical_demand", gold="high",
         headline="[TEST] Central-bank gold buying stays brisk",
         body="[TEST DATA] A synthetic official-sector demand note.",
         expect=dict(gold=True, category="physical_demand")),
    dict(id="etf_outflows", category="positioning", gold="medium",
         headline="[TEST] Gold ETF holdings slip for a third week",
         body="[TEST DATA] A synthetic positioning note.",
         expect=dict(gold=True, category="positioning")),
    dict(id="mine_supply", category="physical_demand", gold="medium",
         headline="[TEST] Mine output rises on a new project",
         body="[TEST DATA] A synthetic supply note.",
         expect=dict(gold=True, category="physical_demand")),
    dict(id="china_demand", category="physical_demand", gold="medium",
         headline="[TEST] Jewellery demand firms in Asia",
         body="[TEST DATA] A synthetic consumer-demand note.",
         expect=dict(gold=True, category="physical_demand")),
    dict(id="ecb_decision", category="monetary_policy", gold="medium",
         headline="[TEST] ECB holds rates and flags slower growth",
         body="[TEST DATA] A synthetic ECB decision note.",
         expect=dict(gold=True, category="monetary_policy")),
    dict(id="boj_policy", category="monetary_policy", gold="medium",
         headline="[TEST] BOJ tweaks its yield-curve control",
         body="[TEST DATA] A synthetic BOJ note.",
         expect=dict(gold=True, category="monetary_policy")),
    dict(id="oil_shock", category="inflation", gold="medium",
         headline="[TEST] Oil spike revives inflation worries",
         body="[TEST DATA] A synthetic energy-shock note.",
         expect=dict(gold=True, category="inflation")),
    dict(id="equities_selloff", category="markets", gold="medium",
         headline="[TEST] Equities sell off as risk appetite fades",
         body="[TEST DATA] A synthetic risk-off note.",
         expect=dict(gold=True, category="markets")),
    # A non-gold story: it must be stored but withheld by the gold-only filter.
    dict(id="unrelated_sports", category="other", gold="low",
         headline="[TEST] A team wins a regional final",
         body="[TEST DATA] A synthetic non-market story.",
         expect=dict(gold=False, category="other")),
    # Ambiguous story: stored and analyzable, direction may be no_trade.
    dict(id="ambiguous_dollar_smile", category="markets", gold="medium",
         headline="[TEST] Dollar rises in both risk-on and risk-off regimes",
         body="[TEST DATA] A synthetic ambiguous-dollar note.",
         expect=dict(gold=True, category="markets")),
    # Same headline as inflation_beat but a different source: the backend must
    # dedup by canonical key, not by source.
    dict(id="dup_across_sources", category="inflation", gold="high",
         headline="[TEST] CPI beats expectations as core inflation runs hot",
         body="[TEST DATA] A duplicate wire copy from another outlet.",
         source="Second Wire (TEST)",
         expect=dict(gold=True, category="inflation", duplicate_of="inflation_beat")),
]

DEFAULT_SOURCE = "AYZR Test Wire (TEST)"


# --------------------------------------------------------------------------
# Local mock source server: RSS + economic calendar.
# --------------------------------------------------------------------------
def _rss_body(items: list[dict]) -> str:
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<rss version=\"2.0\"><channel>"]
    for it in items:
        pub = time.strftime("%a, %d %b %Y %H:%M:%S GMT",
                            time.gmtime(it["published_at"]))
        out.append("<item>")
        out.append(f"<title>{xml_escape(it['headline'] + ' - ' + it['source'])}</title>")
        out.append(f"<link>{xml_escape(it['url'])}</link>")
        out.append(f"<pubDate>{pub}</pubDate>")
        out.append(f"<source url=\"{xml_escape(it['url'])}\">"
                   f"{xml_escape(it['source'])}</source>")
        out.append("</item>")
    out.append("</channel></rss>")
    return "".join(out)


def _calendar_body() -> str:
    # forexfactory-style weekly JSON. `date` carries an explicit offset so the
    # parser converts to UTC instead of guessing the host timezone.
    now = time.time()
    def iso(offset_hours: float) -> str:
        t = time.gmtime(now + offset_hours * 3600)
        return time.strftime("%Y-%m-%dT%H:%M:%S", t) + "-04:00"
    events = [
        {"title": "[TEST] US CPI y/y", "country": "USD", "date": iso(-2),
         "impact": "High", "forecast": "3.1", "previous": "3.2"},
        {"title": "[TEST] FOMC Rate Decision", "country": "USD", "date": iso(4),
         "impact": "High", "forecast": "4.50", "previous": "4.50"},
        {"title": "[TEST] Non-Farm Payrolls", "country": "USD", "date": iso(28),
         "impact": "High", "forecast": "180K", "previous": "175K"},
        {"title": "[TEST] Crude Oil Inventories", "country": "USD", "date": iso(-1),
         "impact": "Low", "forecast": "-1.2M", "previous": "-0.8M"},
    ]
    return json.dumps(events)


class _MockSourceHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # quieter logs
        sys.stderr.write("  mock-source %s\n" % (fmt % args))

    def _send(self, body: str, ctype: str) -> None:
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):  # noqa: N802
        if self.path.startswith("/rss"):
            items = []
            for s in SCENARIOS:
                if s["id"] == "dup_across_sources":
                    continue
                items.append(dict(
                    headline=s["headline"],
                    source=s.get("source", DEFAULT_SOURCE),
                    url=f"https://test.invalid/{s['id']}",
                    category=s["category"],
                    published_at=int(time.time()) - 60 * (len(items) + 1),
                ))
            self._send(_rss_body(items), "application/rss+xml; charset=utf-8")
        elif self.path.startswith("/calendar"):
            self._send(_calendar_body(), "application/json")
        else:
            self.send_error(404)


def serve(host: str, port: int) -> None:
    if host not in ("127.0.0.1", "::1", "localhost"):
        raise SystemExit("refusing to bind a non-loopback host: " + host)
    srv = ThreadingHTTPServer((host, port), _MockSourceHandler)
    print(f"mock news source on http://{host}:{port} "
          f"(/rss, /calendar). Ctrl-C to stop.")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


# --------------------------------------------------------------------------
# Backend client + scenario runner.
# --------------------------------------------------------------------------
FAILS: list[str] = []
CHECKS = 0


def check(cond: bool, label: str) -> None:
    global CHECKS
    CHECKS += 1
    if cond:
        print(f"[PASS] {label}")
    else:
        print(f"[FAIL] {label}")
        FAILS.append(label)


def http(method: str, url: str, token: str | None = None,
         body: dict | None = None, timeout: float = 15.0):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, {"raw": raw}
    except Exception as e:  # noqa: BLE001
        return 0, {"error": str(e)}


def unwrap(obj: dict) -> dict:
    return obj.get("data", obj) if isinstance(obj, dict) else {}


def _assert_loopback(base: str) -> None:
    m = re.match(r"^https?://([^/:]+)", base)
    host = m.group(1) if m else ""
    if host not in ("127.0.0.1", "::1", "localhost"):
        raise SystemExit("refusing to drive a non-loopback backend: " + host)


def _run_no_trade_check(base: str, sender: str, client: str) -> None:
    """Ingest one ambiguous story into a no_trade-mode backend and assert the
    analyzer reports no_trade with every price level null."""
    _assert_loopback(base)
    base = base.rstrip("/")
    article = dict(
        headline="[TEST] Ambiguous story with no clear gold direction",
        source=DEFAULT_SOURCE,
        url="https://test.invalid/no_trade_probe",
        category="markets",
        published_at=int(time.time()) - 60,
        body="[TEST DATA] A deliberately ambiguous note.",
        language="en",
        gold_relevance="medium",
    )
    st, body = http("POST", f"{base}/api/v1/news/ingest", sender,
                    {"articles": [article]})
    check(st == 200, "no_trade: ingest HTTP 200")
    aid = (unwrap(body).get("results") or [{}])[0].get("article_id", "")
    st, body = http("GET", f"{base}/api/v1/news/analyze?id={aid}", client)
    ai = unwrap(body).get("analysis") or {}
    check(st == 200, "no_trade: analyze HTTP 200")
    check(ai.get("direction") == "no_trade",
          f"no_trade: direction is no_trade (got {ai.get('direction')!r})")
    levels = ai.get("levels") or {}
    check(all(levels.get(k) is None
              for k in ("entry", "stop_loss", "take_profit")),
          "no_trade: all price levels null")
    # A no_trade view carries no direction, so it cannot support or contradict a
    # position: any open trade must read as neutral.
    for it in (unwrap(body).get("open_trade_impacts") or []):
        check(it.get("relation") == "neutral",
              "no_trade: open trade impact is neutral")


def run_scenarios(base: str, sender: str, client: str,
                  nt_base: str = "", nt_sender: str = "",
                  nt_client: str = "") -> int:
    _assert_loopback(base)
    base = base.rstrip("/")

    st, _ = http("GET", f"{base}/api/v1/health")
    if st != 200:
        print(f"backend not reachable at {base} (HTTP {st})")
        return 2

    by_id = {s["id"]: s for s in SCENARIOS}
    ids: dict[str, str] = {}

    for s in SCENARIOS:
        article = dict(
            headline=s["headline"],
            source=s.get("source", DEFAULT_SOURCE),
            url=f"https://test.invalid/{s['id']}",
            category=s["category"],
            published_at=int(time.time()) - 120,
            body=s["body"],
            language="en",
            gold_relevance=s["gold"],
        )
        st, body = http("POST", f"{base}/api/v1/news/ingest", sender,
                        {"articles": [article]})
        ack = unwrap(body)
        exp = s["expect"]
        check(st == 200, f"{s['id']}: ingest HTTP 200")
        if "duplicate_of" in exp:
            check(ack.get("duplicates", 0) == 1,
                  f"{s['id']}: cross-source duplicate deduped "
                  f"(got duplicates={ack.get('duplicates')})")
        else:
            check(ack.get("stored", 0) == 1,
                  f"{s['id']}: stored (got stored={ack.get('stored')})")
            res = (ack.get("results") or [{}])[0]
            ids[s["id"]] = res.get("article_id", "")

        # Replay must be idempotent.
        st2, body2 = http("POST", f"{base}/api/v1/news/ingest", sender,
                          {"articles": [article]})
        check(unwrap(body2).get("duplicates", 0) == 1,
              f"{s['id']}: replay is a duplicate")

    # The gold-only filter must keep gold items and withhold the rest.
    st, body = http("GET", f"{base}/api/v1/news?limit=200&gold_only=1", client)
    check(st == 200, "news list (gold_only) HTTP 200")
    gold_ids = {a.get("article", {}).get("article_id")
                for a in unwrap(body).get("news", [])}
    for s in SCENARIOS:
        if "duplicate_of" in s["expect"]:
            continue
        present = ids.get(s["id"], "") in gold_ids
        if s["expect"]["gold"]:
            check(present, f"{s['id']}: present under gold-only filter")
        else:
            check(not present, f"{s['id']}: withheld by gold-only filter")

    # Analyze every stored article with the mock provider. The AI layer must be
    # present, must not overwrite the ASTRA block, and must never invent levels
    # for a non-actionable direction.
    for s in SCENARIOS:
        if "duplicate_of" in s["expect"]:
            continue
        aid = ids.get(s["id"], "")
        if not aid:
            continue
        st, body = http("GET", f"{base}/api/v1/news/analyze?id={aid}", client)
        data = unwrap(body)
        ai = data.get("analysis") or data.get("ai") or {}
        check(st == 200, f"{s['id']}: analyze HTTP 200")
        direction = ai.get("direction")
        check(direction in GOLD_DIRECTIONS,
              f"{s['id']}: direction is a valid token (got {direction!r})")
        check("astra" in json.dumps(data).lower(),
              f"{s['id']}: ASTRA block still present alongside AI layer")
        levels = ai.get("levels") or {}
        if direction == "no_trade":
            check(all(levels.get(k) is None
                      for k in ("entry", "stop_loss", "take_profit")),
                  f"{s['id']}: no_trade invents no price levels")
        # The news-to-open-trade linkage must always be present (a list), so the
        # client can tell "no open trades" from "field missing". The valid-mode
        # backend seeds one BUY trade, so this is a populated list here.
        impacts = data.get("open_trade_impacts")
        check(isinstance(impacts, list),
              f"{s['id']}: open_trade_impacts present as a list (got {type(impacts).__name__})")
        for it in (impacts or []):
            check(it.get("relation") in ("supportive", "contradicting", "neutral"),
                  f"{s['id']}: impact relation is a valid token")
            check(bool(it.get("explanation_ar")),
                  f"{s['id']}: impact carries an Arabic explanation")
            check("ربح مؤكد" not in (it.get("explanation_ar") or ""),
                  f"{s['id']}: impact explanation claims no outcome")
            # The seeded open trade is BUY, so a bullish news view must read as
            # supportive and a bearish view as contradicting.
            if direction == "bullish":
                check(it.get("relation") == "supportive",
                      f"{s['id']}: bullish vs seeded BUY is supportive")
            elif direction == "bearish":
                check(it.get("relation") == "contradicting",
                      f"{s['id']}: bearish vs seeded BUY is contradicting")

    # History must reflect every persisted analysis, across a restart.
    st, body = http("GET", f"{base}/api/v1/news/results?limit=200", client)
    check(st == 200, "news results HTTP 200")
    results = unwrap(body).get("results", [])
    check(len(results) >= 19,
          f"news results persisted for all scenarios (got {len(results)})")

    # Ambiguity -> explicit no-trade. The provider mode is a server-wide
    # setting, so this needs its own backend. Skip honestly if the harness did
    # not supply one rather than claiming a check that never ran.
    if nt_base and nt_sender and nt_client:
        _run_no_trade_check(nt_base, nt_sender, nt_client)

    # The public routes must reject a wrong credential.
    st, _ = http("POST", f"{base}/api/v1/news/ingest", client,
                 {"articles": [{"headline": "x", "source": "y"}]})
    check(st == 401, "client token cannot ingest news (401)")
    st, _ = http("GET", f"{base}/api/v1/news", sender)
    check(st == 401, "sender token cannot read news (401)")

    print(f"\n{CHECKS} check(s), {len(FAILS)} failed")
    if FAILS:
        print("RESULT: FAIL")
        return 1
    print("RESULT: PASS")
    return 0


def _spawn(exe: str, data_dir: str, port: int, mode: str, sender: str,
           client: str, seed_trade: str = "") -> "subprocess.Popen":
    import os
    import subprocess
    env = dict(os.environ)
    env.update({
        "ASTRA_SENDER_TOKEN": sender,
        "ASTRA_CLIENT_TOKEN": client,
        "ASTRA_ADMIN_TOKEN": "admin-" + sender,
        "ASTRA_LLM_PROVIDER": "mock",
        "ASTRA_MOCK_LLM_MODE": mode,
    })
    if seed_trade:
        # Seed one deterministic open trade so the news-to-open-trade linkage is
        # exercised with a real trade, not an empty list.
        env["ASTRA_TEST_SEED_TRADE"] = seed_trade
    log = open(f"/tmp/ayzr-news-{mode}-{port}.log", "w")
    proc = subprocess.Popen(
        [exe, "--host", "127.0.0.1", "--port", str(port),
         "--data-dir", data_dir],
        stdout=log, stderr=subprocess.STDOUT, env=env)
    for _ in range(100):
        try:
            import socket
            with socket.create_connection(("127.0.0.1", port), 0.3):
                return proc
        except OSError:
            time.sleep(0.1)
    return proc


def selftest(exe: str) -> int:
    """Spawn two loopback backends (valid + no_trade) and run every scenario.
    Self-contained: no pre-started server, no external network, no AI token."""
    import shutil
    import tempfile
    if not shutil.which(exe) and not __import__("os").path.exists(exe):
        print(f"backend executable not found: {exe}")
        return 2
    d1 = tempfile.mkdtemp(prefix="ayzr-news-valid-")
    d2 = tempfile.mkdtemp(prefix="ayzr-news-notrade-")
    import socket
    def free_port() -> int:
        s = socket.socket(); s.bind(("127.0.0.1", 0))
        p = s.getsockname()[1]; s.close(); return p
    p1, p2 = free_port(), free_port()
    procs = [
        _spawn(exe, d1, p1, "valid", "sv", "cv", seed_trade="BUY"),
        _spawn(exe, d2, p2, "no_trade", "sn", "cn"),
    ]
    try:
        rc = run_scenarios(f"http://127.0.0.1:{p1}", "sv", "cv",
                           f"http://127.0.0.1:{p2}", "sn", "cn")
    finally:
        for pr in procs:
            pr.terminate()
        for pr in procs:
            try:
                pr.wait(timeout=5)
            except Exception:
                pr.kill()
        for d in (d1, d2):
            shutil.rmtree(d, ignore_errors=True)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("serve", help="run the mock news/calendar source")
    sp.add_argument("--host", default="127.0.0.1")
    sp.add_argument("--port", type=int, default=8899)

    sr = sub.add_parser("scenarios", help="run the 20 scenarios against a backend")
    sr.add_argument("--base", default="http://127.0.0.1:8790")
    sr.add_argument("--sender-token", required=True)
    sr.add_argument("--client-token", required=True)
    sr.add_argument("--no-trade-base", default="",
                    help="optional backend started with ASTRA_MOCK_LLM_MODE=no_trade")
    sr.add_argument("--no-trade-sender-token", default="")
    sr.add_argument("--no-trade-client-token", default="")

    st = sub.add_parser("selftest",
                        help="spawn two loopback backends and run every scenario")
    st.add_argument("--server", default="./build-ayzr/astra_server")

    args = ap.parse_args()
    if args.cmd == "serve":
        serve(args.host, args.port)
        return 0
    if args.cmd == "selftest":
        return selftest(args.server)
    return run_scenarios(args.base, args.sender_token, args.client_token,
                         args.no_trade_base, args.no_trade_sender_token,
                         args.no_trade_client_token)


if __name__ == "__main__":
    raise SystemExit(main())
