#!/usr/bin/env python3
"""Feed REAL news into a running AYZR backend through the sender ingest path.

This exercises ``POST /api/v1/news/ingest`` with the sender token -- the same
route a real collector uses -- so the Android news list can be inspected on
genuine, categorized data.

What is real and what is not
----------------------------
* Headlines, sources, URLs and publish times come from a public Google News RSS
  query. Nothing is invented.
* The article BODY is not carried by RSS, so it is sent empty and the backend
  reports ``body_available=false``. The detail screen then shows the honest
  "article text unavailable" state instead of fabricated prose.
* ``gold_relevance`` is a routing tag, not a claim about the market: 'high' for
  gold-specific queries, 'medium' for macro/central-bank/geopolitical ones that
  move gold indirectly. Only 'high'/'medium' pass the app's "gold only" filter
  (``gold_relevance IN ('high','medium')``).
* ``category`` is a plain label stored verbatim; no inference is performed.

Usage
-----
    python3 tools/ayzr_agents/feed_news.py --base http://127.0.0.1:12000 \
        --token demo-sender-token-ayzr --per-query 8
"""
from __future__ import annotations

import argparse
import calendar
import html
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

# (RSS query, stored category, gold_relevance). The relevance tag routes an
# article into the app's gold-only filter; it does not assert market impact.
FEEDS: list[tuple[str, str, str]] = [
    ("gold price XAUUSD", "markets", "high"),
    ("gold futures COMEX", "markets", "high"),
    ("Federal Reserve interest rate", "monetary_policy", "high"),
    ("central bank interest rate decision", "monetary_policy", "medium"),
    ("US inflation CPI", "inflation", "high"),
    ("inflation expectations", "inflation", "medium"),
    ("central bank gold reserves", "central_banks", "medium"),
    ("US economic data jobs report", "economic_data", "medium"),
    ("geopolitics safe haven gold", "geopolitical", "medium"),
    ("dollar index US treasury yields", "markets", "medium"),
]

RSS = "https://news.google.com/rss/search?q={q}&hl=en-US&gl=US&ceid=US:en"


def log(msg: str) -> None:
    print(f"[feed-news] {msg}", flush=True)


def http(method: str, url: str, token: str | None = None,
         body: str | None = None, timeout: int = 30) -> tuple[int, str]:
    data = body.encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001 - surfaced verbatim to the operator
        return 0, str(e)


def unwrap(txt: str):
    """Backend success bodies are enveloped as {"data": ...}."""
    try:
        obj = json.loads(txt)
    except Exception:
        return {}
    return obj.get("data", obj)


def fetch(query: str, category: str, relevance: str,
          limit: int) -> list[dict]:
    url = RSS.format(q=urllib.parse.quote(query))
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        root = ET.fromstring(r.read())
    out: list[dict] = []
    for it in root.findall(".//item")[:limit]:
        title = html.unescape(it.findtext("title") or "").strip()
        link = (it.findtext("link") or "").strip()
        pub = (it.findtext("pubDate") or "").strip()
        src_el = it.find("source")
        source = (src_el.text or "").strip() if src_el is not None else ""
        # Google News appends " - <source>"; split it so headline and source do
        # not duplicate each other.
        if source and title.endswith(f" - {source}"):
            title = title[: -(len(source) + 3)].strip()
        try:
            epoch = calendar.timegm(
                time.strptime(pub, "%a, %d %b %Y %H:%M:%S %Z"))
        except ValueError:
            epoch = int(time.time())
        if not title or not source or not link:
            continue
        out.append({
            "headline": title,
            "source": source,
            "url": link,
            "category": category,
            "published_at": epoch,
            "body": "",           # RSS carries no body; never invented
            "language": "en",
            "gold_relevance": relevance,
        })
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base", default="http://127.0.0.1:12000")
    ap.add_argument("--token", default="demo-sender-token-ayzr",
                    help="sender token: authorizes POST /news/ingest")
    ap.add_argument("--read-token", default="demo-client-token-ayzr",
                    help="client token: authorizes GET /news and /news/analyze")
    ap.add_argument("--per-query", type=int, default=8,
                    help="articles taken per RSS query")
    ap.add_argument("--analyze", type=int, default=6,
                    help="newly stored articles to run through the analyzer")
    ap.add_argument("--analyze-limit", type=int, default=60,
                    help="how many stored articles to scan for un-analyzed ones")
    args = ap.parse_args()

    base = args.base.rstrip("/")
    st, _ = http("GET", f"{base}/api/v1/health/v1", args.token)
    if st != 200:
        log(f"backend not reachable at {base} (HTTP {st}); start it first")
        return 1

    seen: set[str] = set()
    articles: list[dict] = []
    for query, category, relevance in FEEDS:
        try:
            got = fetch(query, category, relevance, args.per_query)
        except Exception as e:  # noqa: BLE001 - one feed must not kill the run
            log(f"feed failed [{query}]: {e}")
            continue
        fresh = [a for a in got if a["headline"] not in seen]
        for a in fresh:
            seen.add(a["headline"])
        articles.extend(fresh)
        log(f"{category:15s} <- {len(fresh):2d} new / {len(got):2d} fetched  ({query})")

    if not articles:
        log("no articles fetched; nothing to send")
        return 1

    st, txt = http("POST", f"{base}/api/v1/news/ingest", args.token,
                   json.dumps({"articles": articles}))
    ack = unwrap(txt)
    log(f"ingest -> HTTP {st}  stored={ack.get('stored')} "
        f"duplicates={ack.get('duplicates')} rejected={ack.get('rejected')}")
    if st != 200:
        log(f"ingest body: {txt[:300]}")
        return 1

    # Analyze only articles the backend reports as un-analyzed, so re-runs do
    # not spend quota re-analyzing what is already stored.
    st, txt = http("GET", f"{base}/api/v1/news?limit={args.analyze_limit}",
                   args.read_token)
    items = unwrap(txt).get("news", []) if st == 200 else []
    todo = [it["article"]["article_id"] for it in items
            if it.get("analysis_status") in (None, "none", "")]
    log(f"list -> {len(items)} article(s); {len(todo)} un-analyzed")

    done = 0
    for aid in todo[: args.analyze]:
        st, txt = http("GET", f"{base}/api/v1/news/analyze?id={aid}",
                       args.read_token)
        status = unwrap(txt).get("analysis", {}).get("status")
        if st == 200:
            done += 1
            log(f"analyze {aid[:12]} -> status={status}")
        else:
            log(f"analyze {aid[:12]} -> HTTP {st}")
    log(f"analyzed {done} article(s)")

    # Final shape check: exactly what the Android list will render.
    st, txt = http("GET", f"{base}/api/v1/news?limit=50", args.read_token)
    items = unwrap(txt).get("news", []) if st == 200 else []
    log(f"FINAL: {len(items)} article(s) visible to the app")
    for it in items[:10]:
        a = it["article"]
        log(f"   [{a['category']:14s}] {a['headline'][:58]:58s} "
            f"| {a['source'][:18]:18s} | rel={a['gold_relevance']:6s} "
            f"| status={it['analysis_status']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
