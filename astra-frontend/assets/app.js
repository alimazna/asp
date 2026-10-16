/* ASTRA web application.
 *
 * A dependency-free, routed single-page application that renders ONLY real
 * backend responses. Every panel has explicit loading / available / degraded /
 * unavailable states; unknown is never shown as safe.
 */

"use strict";

const A = window.ASTRA;                 // pure helpers from lib.js
const BACKEND = (window.ASTRA_BACKEND_BASE || "").replace(/\/+$/, "");
const REFRESH_MS = window.ASTRA_REFRESH_MS || 30000;
const REQUEST_TIMEOUT_MS = 12000;
/* An empty base URL means "call the same origin this page was served from".
   That is only allowed when the build explicitly opts in (e.g. a local
   `wrangler pages dev` proxy), so a production build that forgot to set
   BACKEND_BASE still fails loudly instead of silently hitting the wrong host. */
const SAME_ORIGIN = BACKEND === "" && window.ASTRA_ALLOW_SAME_ORIGIN === true;
const CONFIGURED = (Boolean(BACKEND) && !BACKEND.includes("REPLACE_WITH_BACKEND_URL"))
  || SAME_ORIGIN;

const state = {
  route: "dashboard",
  health: null,
  system: null,
  lastUpdated: null,
  chartTf: localStorage.getItem("astra.chartTf") || "M15",
  matrixTf: "M15",
  historyLimit: 50,
  navCollapsed: localStorage.getItem("astra.navCollapsed") === "1",
  healthChecked: false,
};

/* -------------------------------------------------------------- api client */

function api(path, params) {
  if (!CONFIGURED) {
    return Promise.reject(new Error("Backend base URL is not configured for this build."));
  }
  const url = BACKEND + path + A.buildQuery(params || {});
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  return fetch(url, { headers: { Accept: "application/json" }, signal: controller.signal })
    .then((res) =>
      res.json().catch(() => {
        throw new Error("Invalid response (HTTP " + res.status + ")");
      })
    )
    .then((payload) => A.unwrap(payload))
    .catch((err) => {
      if (err && err.name === "AbortError") throw new Error("Request timed out");
      throw err;
    })
    .finally(() => clearTimeout(timer));
}

/* --------------------------------------------------------------- dom utils */

function h(tag, attrs, children) {
  const el = document.createElement(tag);
  if (attrs) {
    Object.keys(attrs).forEach((k) => {
      const v = attrs[k];
      if (v === null || v === undefined || v === false) return;
      if (k === "class") el.className = v;
      else if (k === "html") el.innerHTML = v;
      else if (k === "text") el.textContent = v;
      else if (k === "dataset") Object.keys(v).forEach((d) => { el.dataset[d] = v[d]; });
      else if (k === "on") Object.keys(v).forEach((ev) => el.addEventListener(ev, v[ev]));
      else if (k === "for") el.htmlFor = v;
      else if (v === true) el.setAttribute(k, "");
      else el.setAttribute(k, String(v));
    });
  }
  const list = Array.isArray(children) ? children
    : (children === null || children === undefined ? [] : [children]);
  list.forEach((c) => {
    if (c === null || c === undefined || c === false) return;
    el.appendChild(typeof c === "string" || typeof c === "number"
      ? document.createTextNode(String(c)) : c);
  });
  return el;
}

function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

function valueOrUnavailable(value, unavailableText) {
  if (value === null || value === undefined || value === "") {
    return h("span", { class: "badge neutral", text: unavailableText || "not available" });
  }
  return h("span", { text: String(value) });
}

function badge(status, label) {
  return h("span", {
    class: "badge " + A.statusClass(status),
    text: label !== undefined ? label : (status || "UNKNOWN"),
  });
}

function metric(label, valueNode, note) {
  return h("div", { class: "metric" }, [
    h("span", { class: "metric__label", text: label }),
    valueNode,
    note ? h("div", { class: "metric__note", text: note }) : null,
  ]);
}

function metricText(label, value, note, small) {
  const v = h("div", { class: "metric__value" + (small ? " metric__value--sm" : "") });
  v.appendChild(valueOrUnavailable(value));
  return metric(label, v, note);
}

function kv(rows) {
  const tbody = h("tbody", null, rows.map((r) => h("tr", null, [
    h("th", { scope: "row", text: r[0] }),
    h("td", null, r[1]),
  ])));
  return h("table", { class: "kv" }, tbody);
}

function card(title, bodyNode, headExtra) {
  return h("section", { class: "card" }, [
    h("div", { class: "card__head" }, [
      h("h2", { text: title }),
      h("div", { class: "spacer" }),
      headExtra || null,
    ]),
    bodyNode,
  ]);
}

function stateBlock(title, hint, kind) {
  return h("div", { class: "state" + (kind ? " state--" + kind : "") }, [
    h("div", { class: "state__title", text: title }),
    hint ? h("div", { class: "state__hint", text: hint }) : null,
  ]);
}

function loadingBlock(label) {
  return h("div", { class: "state" }, [
    h("span", { class: "spinner" }),
    h("div", { class: "state__hint", text: label || "Loading…" }),
  ]);
}

function callout(kind, text) {
  return h("div", { class: "callout" + (kind ? " callout--" + kind : "") }, [
    h("span", { class: "callout__icon", html: ICONS.alert }),
    h("span", { text: text }),
  ]);
}

/* ------------------------------------------------------------------ icons */

const ICONS = {
  dashboard: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="7" height="9"/><rect x="14" y="3" width="7" height="5"/><rect x="14" y="12" width="7" height="9"/><rect x="3" y="16" width="7" height="5"/></svg>',
  chart: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v18h18"/><path d="M7 15l3-4 3 3 4-6"/></svg>',
  layers: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l9 5-9 5-9-5 9-5z"/><path d="M3 13l9 5 9-5"/></svg>',
  signal: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12h4l3-8 4 16 3-8h4"/></svg>',
  history: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 4v4h4"/><path d="M12 8v4l3 2"/></svg>',
  feed: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 11a9 9 0 0 1 9 9"/><path d="M4 4a16 16 0 0 1 16 16"/><circle cx="5" cy="19" r="1.5"/></svg>',
  flask: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M9 3h6"/><path d="M10 3v6l-5 9a2 2 0 0 0 2 3h10a2 2 0 0 0 2-3l-5-9V3"/></svg>',
  shield: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6l8-3z"/><path d="M9 12l2 2 4-4"/></svg>',
  audit: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 3h9l5 5v13H5z"/><path d="M14 3v5h5"/><path d="M8 13h8M8 17h5"/></svg>',
  settings: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19 12a7 7 0 0 0-.1-1l2-1.5-2-3.4-2.3 1a7 7 0 0 0-1.7-1L14.5 3h-5l-.4 2.6a7 7 0 0 0-1.7 1l-2.3-1-2 3.4L5.1 11a7 7 0 0 0 0 2l-2 1.5 2 3.4 2.3-1a7 7 0 0 0 1.7 1L9.5 21h5l.4-2.6a7 7 0 0 0 1.7-1l2.3 1 2-3.4-2-1.5c.1-.3.1-.7.1-1z"/></svg>',
  alert: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l9 16H3z"/><path d="M12 10v4M12 17h.01"/></svg>',
  refresh: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M20 11a8 8 0 1 0-2.3 6"/><path d="M20 5v6h-6"/></svg>',
  menu: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg>',
  collapse: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M15 6l-6 6 6 6"/></svg>',
};

/* ------------------------------------------------------------- navigation */

const NAV = [
  { group: "Overview", items: [
    { id: "dashboard", label: "Dashboard", icon: "dashboard", title: "Dashboard",
      subtitle: "Market state, H4 forecast and system health at a glance" },
  ]},
  { group: "Market", items: [
    { id: "forecast", label: "Forecast", icon: "signal", title: "Market Forecast",
      subtitle: "Directional read from the H4 structural timeframe and the closed M15 trigger" },
    { id: "plan", label: "Trade Plan", icon: "shield", title: "Trade Plan & Outcomes",
      subtitle: "Backend entry / stop / target levels and the recorded shadow outcome record" },
    { id: "chart", label: "Market Chart", icon: "chart", title: "Market Chart",
      subtitle: "Closed-candle series for the selected instrument and timeframe" },
    { id: "analysis", label: "Analysis", icon: "layers", title: "Multi-Timeframe Analysis",
      subtitle: "Nine-timeframe coverage and analytical structure" },
    { id: "latest", label: "Latest Analysis", icon: "signal", title: "Latest Analysis",
      subtitle: "Most recent decision-support output from the backend" },
    { id: "history", label: "History", icon: "history", title: "Analysis History",
      subtitle: "Recorded analysis results exposed by the backend" },
  ]},
  { group: "Operations", items: [
    { id: "marketdata", label: "Market Data", icon: "feed", title: "Market Data & MT5",
      subtitle: "Ingestion coverage, watermarks and sender synchronization" },
    { id: "research", label: "Research & Shadow", icon: "flask", title: "Research & Shadow",
      subtitle: "Shadow positions, outcomes and research status" },
    { id: "system", label: "Risk & System", icon: "shield", title: "Risk & System",
      subtitle: "Health, readiness, risk bounds and operational warnings" },
    { id: "audit", label: "Audit", icon: "audit", title: "Audit",
      subtitle: "Recent audit records and active incidents" },
  ]},
  { group: "Workspace", items: [
    { id: "settings", label: "Settings", icon: "settings", title: "Settings",
      subtitle: "Local display preferences (stored in this browser only)" },
  ]},
];

const NAV_FLAT = NAV.reduce((acc, g) => acc.concat(g.items), []);
const BOTTOM_NAV = ["dashboard", "forecast", "plan", "chart", "system"];
const routeMeta = (id) => NAV_FLAT.find((n) => n.id === id) || NAV_FLAT[0];

/* ------------------------------------------------------------------ chart */
/* Lightweight canvas candlestick renderer: 2D only, no WebGL, DPR-aware,
   redrawn on demand (never a render loop). */

function renderChart(canvas, bars, opts) {
  const suspicious = Boolean(opts && opts.suspicious);
  const dpr = window.devicePixelRatio || 1;
  const cssW = Math.max(canvas.clientWidth || 320, 240);
  const cssH = canvas.clientHeight || 320;
  canvas.width = Math.round(cssW * dpr);
  canvas.height = Math.round(cssH * dpr);
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);
  if (!bars || bars.length === 0) {
    if (suspicious) {
      const cssRoot = getComputedStyle(document.documentElement);
      ctx.fillStyle = cssRoot.getPropertyValue("--bad").trim() || "#E0575C";
      ctx.font = "12px " + (cssRoot.getPropertyValue("--font-sans").trim() || "sans-serif");
      ctx.fillText("Not plotted: stored candles are not a real MT5 series.", 14, cssH / 2);
    }
    return;
  }

  const css = getComputedStyle(document.documentElement);
  const cUp = css.getPropertyValue("--chart-up").trim() || "#3FB27F";
  const cDown = css.getPropertyValue("--chart-down").trim() || "#E0575C";
  const cGrid = css.getPropertyValue("--chart-grid").trim() || "#1B2B3A";
  const cAxis = css.getPropertyValue("--chart-axis").trim() || "#7C8B99";

  const padL = 8, padR = 58, padT = 12, padB = 22;
  const plotW = Math.max(cssW - padL - padR, 40);
  const plotH = Math.max(cssH - padT - padB, 40);
  const scale = A.chartScale(bars, plotH + 2 * padT, padT);
  const bw = plotW / bars.length;
  const xOf = (i) => padL + i * bw + bw / 2;
  const yOf = (v) => scale.y(v);

  ctx.font = "11px " + (css.getPropertyValue("--font-mono").trim() || "monospace");
  ctx.textBaseline = "middle";
  A.gridValues(scale.min, scale.max, 4).forEach((gv) => {
    const gy = yOf(gv);
    ctx.strokeStyle = cGrid;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padL, gy);
    ctx.lineTo(padL + plotW, gy);
    ctx.stroke();
    ctx.fillStyle = cAxis;
    ctx.textAlign = "left";
    ctx.fillText(gv.toFixed(2), padL + plotW + 6, gy);
  });

  bars.forEach((b, i) => {
    const x = xOf(i);
    const up = b.close >= b.open;
    const color = up ? cUp : cDown;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, yOf(b.high));
    ctx.lineTo(x, yOf(b.low));
    ctx.stroke();
    ctx.fillStyle = color;
    const top = yOf(Math.max(b.open, b.close));
    const bh = Math.max(Math.abs(yOf(b.open) - yOf(b.close)), 1);
    const w = Math.max(bw * 0.62, 1);
    ctx.fillRect(x - w / 2, top, w, bh);
  });

  ctx.fillStyle = cAxis;
  ctx.textBaseline = "top";
  ctx.textAlign = "left";
  const stamp = (s) => new Date(s * 1000).toISOString().slice(5, 16).replace("T", " ");
  ctx.fillText(stamp(bars[0].time), padL, padT + plotH + 5);
  ctx.textAlign = "right";
  ctx.fillText(stamp(bars[bars.length - 1].time), padL + plotW, padT + plotH + 5);
}

/* Interactive crosshair tooltip, bound once per canvas. */
function bindChartHover(canvas, getBars) {
  const tip = canvas.parentElement.querySelector(".chart-tip");
  if (!tip) return;
  const move = (ev) => {
    const bars = getBars();
    if (!bars || !bars.length) { tip.classList.remove("on"); return; }
    const rect = canvas.getBoundingClientRect();
    const x = (ev.touches ? ev.touches[0].clientX : ev.clientX) - rect.left;
    const padL = 8, padR = 58;
    const plotW = Math.max(rect.width - padL - padR, 40);
    const idx = Math.max(0, Math.min(bars.length - 1,
      Math.floor((x - padL) / (plotW / bars.length))));
    const b = bars[idx];
    tip.innerHTML = "<strong>" + A.esc(A.fmtEpochUTC(b.time)) + "</strong><br>"
      + "O " + b.open + "  H " + b.high + "<br>L " + b.low + "  C " + b.close
      + (b.tick_volume != null ? "<br>vol " + b.tick_volume : "");
    const cw = plotW / bars.length;
    tip.style.left = (padL + idx * cw + cw / 2) + "px";
    tip.style.top = "16px";
    tip.classList.add("on");
  };
  const leave = () => tip.classList.remove("on");
  canvas.addEventListener("mousemove", move);
  canvas.addEventListener("mouseleave", leave);
  canvas.addEventListener("touchmove", move, { passive: true });
  canvas.addEventListener("touchend", leave);
}

/* ------------------------------------------------------------------ views */
/* Each view returns a DOM node. Async views mount a placeholder first and
   replace their body when data resolves. */

const views = {};

/* --- A. Dashboard --- */
views.dashboard = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading system state…"));

  Promise.allSettled([
    api("/api/v1/health"),
    api("/api/v1/system/state"),
    api("/api/v1/timeframes"),
    api("/api/v1/analysis/latest"),
    api("/api/v1/bridge/status"),
    api("/api/v1/shadow/outcomes"),
  ]).then(([health, system, tfs, analysis, bridge, outcomes]) => {
    clear(body);
    const healthD = health.status === "fulfilled" ? health.value : null;
    const systemD = system.status === "fulfilled" ? system.value : null;
    const tfsD = tfs.status === "fulfilled" ? tfs.value : null;
    const analysisD = analysis.status === "fulfilled" ? analysis.value : null;
    const bridgeD = bridge.status === "fulfilled" ? bridge.value : null;
    const outcomesD = outcomes.status === "fulfilled" && Array.isArray(outcomes.value)
      ? outcomes.value : [];
    const tally = A.outcomeTally(outcomesD);

    if (!healthD && !systemD) {
      body.appendChild(callout("bad",
        "The backend is unreachable from this browser. Values below are unavailable; "
        + "no data is being fabricated."));
    }

    const cov = A.coverageSummary(tfsD);

    /* degraded banner — honest, prominent */
    if (healthD && healthD.aggregate && healthD.aggregate !== "OK") {
      body.appendChild(callout(healthD.decision_grade_data ? "ok" : "warn",
        "Backend aggregate is " + healthD.aggregate
        + (healthD.decision_grade_data ? "" : " — decision-grade data is NOT available")
        + (healthD.unknown_is_not_safe ? ". Unknown states are treated as not safe." : ".")));
    }

    /* H4 forecast hero — the primary directional read, from the backend.
       Built only from responses already fetched for this view, so the
       dashboard adds no extra polling. */
    if (analysisD) {
      const ctx = analysisD.context || {};
      const bias = A.biasDirection(ctx.h4_bias);
      const lean = bias === "UP" ? { kind: "ok", arrow: "▲", word: "UP" }
        : bias === "DOWN" ? { kind: "bad", arrow: "▼", word: "DOWN" }
        : bias === "RANGE" ? { kind: "warn", arrow: "◆", word: "RANGE" }
        : { kind: "warn", arrow: "•", word: "unavailable" };
      const tfRec = Array.isArray(tfsD)
        ? tfsD.find((x) => x && x.timeframe === "H4") : null;
      const h4HasClosed = Boolean(tfRec && tfRec.has_closed_bar === true);
      const lastOpen = h4HasClosed && Number.isFinite(Number(tfRec.last_successful_update))
        ? Number(tfRec.last_successful_update) : null;
      const info = lastOpen ? A.closedCandleInfo(lastOpen, "H4", Date.now()) : null;

      const hero = h("section", { class: "card forecast-hero" }, [
        h("div", { class: "forecast-hero__main" }, [
          h("span", { class: "forecast-hero__label", text: "H4 market forecast (structural authority)" }),
          h("div", { class: "forecast-hero__lean badge " + lean.kind }, [
            h("span", { class: "forecast-hero__arrow", text: lean.arrow }),
            h("span", { text: lean.word }),
          ]),
          h("div", { class: "forecast-hero__note", text: bias === "UNKNOWN"
            ? "The backend has not reported an H4 structure bias yet, so no direction is shown."
            : "Read directly from the backend's H4 structure bias. Not a prediction of our own." }),
        ]),
        h("div", { class: "forecast-hero__facts" }, kv([
          ["H4 bias", valueOrUnavailable(ctx.h4_bias)],
          ["M15 trigger", valueOrUnavailable(ctx.m15_trigger)],
          ["Last closed H4 candle", lastOpen
            ? h("span", { class: "mono", text: A.fmtEpochUTC(lastOpen) })
            : valueOrUnavailable(null)],
          ["Next H4 bar", info ? h("span", { text: info.overdueLabel }) : valueOrUnavailable(null)],
        ])),
        h("div", { class: "forecast-hero__actions" }, [
          h("button", { class: "btn", on: { click: () => navigate("forecast") },
            text: "Open full forecast" }),
          h("button", { class: "btn", on: { click: () => navigate("plan") },
            text: "Trade plan & outcomes" }),
        ]),
      ]);
      body.appendChild(hero);
    }

    /* metric strip */
    const metrics = h("div", { class: "grid grid--metrics" });
    metrics.appendChild(metricText("Instrument", "XAUUSD", "configured read target"));
    const lastBar = analysisD && analysisD.timestamp ? analysisD.timestamp : null;
    metrics.appendChild(metricText("Latest analysis", lastBar, "backend analysis timestamp", true));
    metrics.appendChild(metricText("Timeframe coverage",
      cov.observed + " / " + cov.total + " observed",
      cov.decisionGrade + " decision-grade"));
    metrics.appendChild(metricText("Data grade",
      healthD ? (healthD.decision_grade_data ? "DECISION GRADE" : "DEGRADED") : null,
      "decision_grade_data"));
    metrics.appendChild(metricText("Backend",
      systemD ? (systemD.ready ? "READY" : systemD.mode || "UNKNOWN") : (healthD ? "reachable" : "unreachable"),
      "system/state"));
    metrics.appendChild(metricText("Bridge / sender",
      bridgeD ? bridgeD.bridge_process_state : null, "MT5 bridge process"));
    metrics.appendChild(metricText("Shadow success rate",
      tally.winRate == null ? null : (tally.winRate * 100).toFixed(0) + "%",
      tally.winRate == null ? "no decided outcome yet — not 0%"
        : tally.decided + " decided of " + tally.total + " recorded",
      true));
    body.appendChild(metrics);

    /* coverage matrix + warnings */
    const row = h("div", { class: "grid grid--2-1" });
    row.appendChild(card("Timeframe Coverage",
      tfsD ? matrixNode(tfsD, false) : stateBlock("Coverage unavailable",
        "The /api/v1/timeframes endpoint did not respond.")));

    const warnBody = h("div", { class: "card__body stack" });
    if (healthD && Array.isArray(healthD.degraded_reasons) && healthD.degraded_reasons.length) {
      warnBody.appendChild(h("div", { class: "note", text: "Degraded reasons reported by the backend:" }));
      warnBody.appendChild(h("ul", { class: "stack" },
        healthD.degraded_reasons.slice(0, 9).map((r) =>
          h("li", { class: "mono", style: "font-size:var(--fs-xs);color:var(--text-muted)", text: r }))));
    } else if (healthD) {
      warnBody.appendChild(callout("ok", "No degraded reasons reported."));
    } else {
      warnBody.appendChild(stateBlock("No warnings available", "Backend health was not reachable."));
    }
    row.appendChild(card("Operational Warnings", warnBody));
    body.appendChild(row);

    /* shortcuts */
    const sc = h("div", { class: "card__body row" });
    [["forecast", "Open Market Forecast"], ["plan", "Trade Plan & Outcomes"],
     ["chart", "Open Market Chart"], ["latest", "View Latest Analysis"],
     ["marketdata", "Market Data & MT5"], ["system", "Risk & System"]].forEach(([id, label]) => {
      sc.appendChild(h("button", { class: "btn", on: { click: () => navigate(id) }, text: label }));
    });
    body.appendChild(card("Shortcuts", sc));
  });
};

/* --- coverage matrix node (shared) --- */
function matrixNode(tfs, withRoles) {
  const byTf = {};
  (Array.isArray(tfs) ? tfs : []).forEach((t) => { if (t && t.timeframe) byTf[t.timeframe] = t; });
  const wrap = h("div", { class: "card__body stack" });
  const grid = h("div", { class: "matrix" });
  A.TIMEFRAMES.forEach((tf) => {
    const t = byTf[tf];
    const observed = t ? t.observed === true : false;
    const graded = t ? t.decision_grade === true : false;
    const qstate = t && t.quality ? t.quality.state : "UNKNOWN";
    const cell = h("button", {
      class: "mcell",
      dataset: { grade: graded ? "yes" : "no", observed: observed ? "true" : "false" },
      title: tf + " — " + (t ? (observed ? "observed" : "not observed") : "no state observed"),
      on: { click: () => openTfDetail(tf) },
    }, [
      h("div", { class: "mcell__tf", text: tf }),
      h("span", { class: "dot mcell__dot " + A.statusClass(qstate) }),
      h("div", { class: "mcell__state", text: observed ? qstate : "UNOBSERVED" }),
      withRoles && A.TF_ROLE[tf] ? h("div", { class: "mcell--role", text: A.TF_ROLE[tf].label }) : null,
    ]);
    grid.appendChild(cell);
  });
  wrap.appendChild(grid);
  if (withRoles) {
    const tiers = h("div", { class: "tier" });
    A.TF_TIERS.forEach((tier) => {
      tiers.appendChild(h("div", { class: "tier__row" }, [
        h("div", { class: "tier__tfs" }, tier.tfs.map((tf) => badge(null, tf))),
        h("div", { class: "tier__role", text: tier.title }),
      ]));
    });
    wrap.appendChild(h("div", { class: "note", style: "margin-bottom:var(--sp-3)",
      text: "Analytical structure — roles are project-defined; states are backend-reported." }));
    wrap.appendChild(tiers);
  }
  return wrap;
}

/* --- B. Market Chart --- */
views.chart = function (mount) {
  const statusLine = h("p", { class: "note" });
  const meta = h("span", { class: "badge neutral", text: "—" });
  const canvas = h("canvas", { class: "chart-canvas", role: "img",
    "aria-label": "Closed candlestick chart" });
  const tip = h("div", { class: "chart-tip" });
  const wrap = h("div", { class: "chart-wrap" }, [canvas, tip]);

  const rail = h("div", { class: "tfrail", role: "group", "aria-label": "Timeframe" });
  A.TIMEFRAMES.slice().reverse().forEach((tf) => {
    const b = h("button", {
      type: "button", text: tf,
      "aria-pressed": tf === state.chartTf ? "true" : "false",
      on: { click: () => { state.chartTf = tf; localStorage.setItem("astra.chartTf", tf);
        syncRail(rail, tf); load(); } },
    });
    rail.appendChild(b);
  });

  let bars = [];
  bindChartHover(canvas, () => bars);

  const stack = h("div", { class: "stack" });
  stack.appendChild(card("Timeframe", h("div", { class: "card__body" }, [
    h("div", { class: "row row--between" }, [
      rail,
      h("span", { class: "row", style: "gap:var(--sp-2)" }, [
        meta,
        h("button", { class: "btn btn--ghost btn--icon", title: "Refresh", "aria-label": "Refresh chart",
          html: ICONS.refresh, on: { click: load } }),
      ]),
    ]),
    statusLine,
  ])));
  stack.appendChild(card("Closed Candles", h("div", { class: "card__body card__body--flush" }, wrap)));
  mount.appendChild(stack);

  function load() {
    statusLine.textContent = "Loading closed candles for " + state.chartTf + "…";
    meta.className = "badge neutral"; meta.textContent = "loading";
    api("/api/v1/candles", { tf: state.chartTf, limit: 300 }).then((data) => {
      const part = A.partitionBars(data && data.bars);
      const integ = A.candleBatchIntegrity(part.valid, state.chartTf);
      /* A batch that is not timeframe-aligned cannot be a real MT5 series.
         Refuse to present it as real rather than mislabel fabricated rows. */
      const trusted = integ.ok;
      bars = trusted ? part.valid : [];
      const latest = bars.length ? bars[bars.length - 1].time : null;
      meta.className = "badge " + (trusted ? (bars.length ? "ok" : "neutral") : "bad");
      meta.textContent = trusted ? bars.length + " closed bars" : "unsourced data";
      if (!trusted) {
        statusLine.textContent = "This timeframe's stored candles are not a real "
          + "MT5 series (open times are not " + state.chartTf
          + "-aligned), so nothing is plotted. " + integ.misaligned + " of "
          + integ.checked + " record(s) failed the closed-candle integrity check.";
      } else {
        statusLine.textContent = bars.length
          ? "Closed candles from the backend (read-only). Latest: "
            + (A.fmtEpochUTC(latest) || "—")
            + (part.rejected.length ? "  ·  " + part.rejected.length + " malformed record(s) rejected" : "")
          : "The backend returned no closed bars for " + state.chartTf
            + ". Nothing is drawn rather than fabricating values.";
      }
      renderChart(canvas, bars, { suspicious: !trusted });
    }).catch((err) => {
      bars = [];
      meta.className = "badge bad"; meta.textContent = "unavailable";
      statusLine.textContent = "Candle request failed: " + err.message;
      renderChart(canvas, []);
    });
  }
  load();
  window.addEventListener("resize", () => renderChart(canvas, bars, {}));
};

function syncRail(rail, active) {
  Array.prototype.forEach.call(rail.children, (b) => {
    b.setAttribute("aria-pressed", b.textContent === active ? "true" : "false");
  });
}

/* --- C. Multi-timeframe analysis --- */
views.analysis = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading timeframe coverage…"));

  api("/api/v1/timeframes").then((tfs) => {
    clear(body);
    const cov = A.coverageSummary(tfs);
    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Timeframes", String(cov.total), "contract"),
      metricText("Observed", String(cov.observed), "backend has a state"),
      metricText("Decision-grade", String(cov.decisionGrade), "quality gate passed"),
      metricText("Fresh", String(cov.fresh), "within max age"),
    ]));
    body.appendChild(callout(cov.decisionGrade === 0 ? "warn" : "ok",
      cov.decisionGrade === 0
        ? "No timeframe currently passes the decision-grade gate. Downstream analysis is therefore blocked; this is reported honestly rather than implied."
        : cov.decisionGrade + " of " + cov.total + " timeframes are decision-grade."));
    body.appendChild(card("Nine-Timeframe Structure", matrixNode(tfs, true)));

    const table = h("div", { class: "table-wrap" });
    const thead = h("thead", null, h("tr", null, [
      h("th", { text: "TF" }), h("th", { text: "Role" }), h("th", { text: "Observed" }),
      h("th", { text: "Closed bar" }), h("th", { text: "Quality" }),
      h("th", { text: "Decision grade" }), h("th", { text: "Freshness" }),
      h("th", { text: "Last update" }), h("th", { text: "Capability impact" }),
    ]));
    const tbody = h("tbody");
    (Array.isArray(tfs) ? tfs : []).forEach((t) => {
      const fresh = t.freshness;
      tbody.appendChild(h("tr", null, [
        h("td", { class: "mono", text: t.timeframe }),
        h("td", { text: (A.TF_ROLE[t.timeframe] || {}).label || "—" }),
        h("td", null, badge(t.observed ? "ok" : "neutral", t.observed ? "yes" : "no")),
        h("td", null, badge(t.has_closed_bar ? "ok" : "neutral", t.has_closed_bar ? "yes" : "no")),
        h("td", null, badge(t.quality ? t.quality.state : "UNKNOWN")),
        h("td", null, badge(t.decision_grade ? "ok" : "warn", t.decision_grade ? "yes" : "no")),
        h("td", null, fresh ? badge(fresh.state, fresh.state + (fresh.is_fresh ? "" : "")) : badge("UNKNOWN", "n/a")),
        h("td", { class: "num", text: A.fmtEpochUTC(t.last_successful_update) || "—" }),
        h("td", { text: (t.capability_impact || []).map((c) => c.capability + ":" + c.impact).join(", ") || "—" }),
      ]));
    });
    table.appendChild(thead); table.appendChild(tbody);
    body.appendChild(card("Per-Timeframe Detail", h("div", { class: "card__body card__body--flush" }, table)));
  }).catch((err) => {
    clear(body);
    body.appendChild(callout("bad", "Timeframe coverage unavailable: " + err.message));
  });
};

/* --- D. Latest analysis --- */
views.latest = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading latest analysis…"));

  Promise.allSettled([api("/api/v1/analysis/latest"), api("/api/v1/signals/latest"),
    api("/api/v1/probability/latest"), api("/api/v1/risk/latest")]).then(([a, sig, prob, risk]) => {
    clear(body);
    const d = a.status === "fulfilled" ? a.value : null;
    if (!d) {
      body.appendChild(callout("bad", "Latest analysis is unavailable from the backend."));
      return;
    }
    const s = d.signal || {};
    const meta = d.meta || {};
    const hasValue = s.direction && s.direction !== "NONE";

    body.appendChild(callout(meta.degraded ? "warn" : (hasValue ? "ok" : "warn"),
      meta.degraded
        ? "Analysis is flagged degraded by the backend."
        : (hasValue ? "A decision-support result is available."
          : "No actionable signal is present. The backend reports direction NONE; values below are shown as unavailable rather than estimated.")));

    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Direction", s.direction || null, "signal.direction"),
      metricText("Horizon", s.horizon, "signal.horizon", true),
      metricText("Probability", s.probability == null ? null
        : (Number(s.probability) * 100).toFixed(1) + "%",
        s.probability_calibrated ? "calibrated" : "uncalibrated — not a probability estimate", true),
      metricText("Score", A.fmtNum(s.score, 3), meta.score_is_probability ? "score is a probability" : "score is NOT a probability", true),
      metricText("Model", s.model_version, "signal.model_version", true),
      metricText("Timestamp", d.timestamp, "analysis timestamp", true),
    ]));

    const grid = h("div", { class: "grid grid--2" });
    grid.appendChild(card("Signal", h("div", { class: "card__body" }, kv([
      ["Direction", badge(s.direction || "UNKNOWN")],
      ["Horizon", valueOrUnavailable(s.horizon)],
      ["Probability", s.probability == null ? h("span", { class: "badge neutral", text: "not available" })
        : h("span", null, [(Number(s.probability) * 100).toFixed(1) + "% ",
            badge(s.probability_calibrated ? "ok" : "warn", s.probability_calibrated ? "calibrated" : "uncalibrated")])],
      ["Confidence lo", valueOrUnavailable(A.fmtNum(s.confidence_lo, 3))],
      ["Confidence hi", valueOrUnavailable(A.fmtNum(s.confidence_hi, 3))],
      ["Score", valueOrUnavailable(A.fmtNum(s.score, 4))],
      ["Score is probability", badge(meta.score_is_probability ? "ok" : "neutral", String(meta.score_is_probability === true))],
      ["Features contributing", valueOrUnavailable((s.features_contributing || []).join(", ") || null, "none reported")],
    ]))));
    const c = d.context || {};
    grid.appendChild(card("Context", h("div", { class: "card__body" }, kv([
      ["Regime", badge(c.regime || "UNKNOWN")],
      ["H4 bias", valueOrUnavailable(c.h4_bias)],
      ["M15 trigger", valueOrUnavailable(c.m15_trigger)],
      ["MTF agreement", c.mtf_agreement == null ? h("span", { class: "badge neutral", text: "not available" })
        : h("span", { text: (Number(c.mtf_agreement) * 100).toFixed(0) + "%" })],
      ["Volatility", badge(c.volatility_state || "UNKNOWN")],
    ]))));
    body.appendChild(grid);

    body.appendChild(card("Levels & Risk Bounds", h("div", { class: "card__body" }, kv([
      ["Entry", valueOrUnavailable(A.fmtNum((d.levels || {}).entry, 2))],
      ["Stop loss", valueOrUnavailable(A.fmtNum((d.levels || {}).stop_loss, 2))],
      ["Take profit", valueOrUnavailable(A.fmtNum((d.levels || {}).take_profit, 2))],
      ["Reward / risk", valueOrUnavailable(A.fmtNum((d.levels || {}).reward_risk, 2))],
      ["Suggested risk %", valueOrUnavailable(A.fmtNum((d.levels || {}).suggested_risk_pct, 2))],
    ]))));

    const metaBody = h("div", { class: "card__body" }, kv([
      ["Coverage tier", valueOrUnavailable(meta.coverage_tier)],
      ["Data freshness", meta.data_freshness_sec == null ? h("span", { class: "badge neutral", text: "not available" })
        : h("span", { text: A.fmtAge(Number(meta.data_freshness_sec) * 1000) })],
      ["Degraded", badge(meta.degraded ? "warn" : "ok", String(meta.degraded === true))],
      ["Disclaimer", h("span", { style: "font-family:var(--font-sans);font-size:var(--fs-xs)", text: meta.disclaimer || "—" })],
    ]));
    body.appendChild(card("Source & Freshness", metaBody));

    const sigD = sig.status === "fulfilled" ? sig.value : null;
    const probD = prob.status === "fulfilled" ? prob.value : null;
    const riskD = risk.status === "fulfilled" ? risk.value : null;
    body.appendChild(card("Signal / Probability / Risk Endpoints",
      h("div", { class: "card__body" }, kv([
        ["signals/latest", sigD ? (sigD.available ? badge("ok", "available") : badge("neutral", "unavailable"))
          : badge("bad", "error")],
        ["signals reason", h("span", { style: "font-family:var(--font-sans);font-size:var(--fs-xs)", text: (sigD && sigD.reason) || "—" })],
        ["probability/latest", probD ? (probD.available ? badge("ok", "available") : badge("neutral", "unavailable"))
          : badge("bad", "error")],
        ["probability reason", h("span", { style: "font-family:var(--font-sans);font-size:var(--fs-xs)", text: (probD && probD.reason) || "—" })],
        ["risk/latest", riskD ? (riskD.available ? badge("ok", "available") : badge("neutral", "unavailable"))
          : badge("bad", "error")],
        ["Open positions", riskD ? String(riskD.open_positions) : "—"],
        ["Proposal", riskD ? (riskD.proposal_available ? badge("ok", "available") : badge("neutral", riskD.proposal_reason || "none")) : "—"],
      ]))));
  });
};


/* --- D2. Market forecast --------------------------------------------------
   A directional reading of the H4 (structural authority) timeframe, plus the
   closed M15 trigger. Every value comes from the backend. When the backend has
   no structure yet every field renders as unavailable, and the panel says so
   instead of guessing a direction. */
views.forecast = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Reading H4 structure and closed candles…"));

  Promise.allSettled([
    api("/api/v1/analysis/latest"),
    api("/api/v1/timeframes"),
    api("/api/v1/candles", { tf: "H4", limit: 60 }),
    api("/api/v1/candles", { tf: "M15", limit: 60 }),
  ]).then(([a, tfs, h4, m15]) => {
    clear(body);
    const d = a.status === "fulfilled" ? a.value : null;
    if (!d) {
      body.appendChild(callout("bad",
        "The backend did not return an analysis snapshot, so no forecast can be shown."));
      return;
    }
    const c = d.context || {};
    const bias = A.biasDirection(c.h4_bias);
    const trigger = String(c.m15_trigger || "NONE").toUpperCase();

    /* Honest headline: derived from the reported bias, with its reasons. */
    let headline, kind;
    if (bias === "UP") { headline = "H4 lean: UP (bullish structure)"; kind = "ok"; }
    else if (bias === "DOWN") { headline = "H4 lean: DOWN (bearish structure)"; kind = "bad"; }
    else if (bias === "RANGE") { headline = "H4 lean: RANGE (no directional edge)"; kind = "warn"; }
    else { headline = "H4 lean: not available"; kind = "warn"; }

    body.appendChild(callout(kind, headline +
      " — derived from the backend's H4 structure bias, not from a formula of our own."));

    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("H4 bias", c.h4_bias, "context.h4_bias (structural authority)"),
      metricText("M15 trigger", trigger, "context.m15_trigger (reflexive)", true),
      metricText("Regime", c.regime, "context.regime", true),
      metricText("MTF agreement", c.mtf_agreement == null ? null
        : (Number(c.mtf_agreement) * 100).toFixed(0) + "%", "context.mtf_agreement", true),
      metricText("Signal direction", d.signal ? d.signal.direction : null,
        "signal.direction", true),
      metricText("Timestamp", d.timestamp, "analysis timestamp", true),
    ]));

    /* Closed-candle timing per timeframe: proves we never treat the forming
       candle as closed, and surfaces coverage gaps as an overdue duration. */
    const tfList = tfs.status === "fulfilled" && Array.isArray(tfs.value)
      ? tfs.value : [];
    const h4Bars = h4.status === "fulfilled" && h4.value && Array.isArray(h4.value.bars)
      ? h4.value.bars : [];
    /* The M15 store currently holds non-MT5 rows. A watermark derived from them
       (as M15 currently is) would report a synthetic timestamp as a real closed
       bar. Only trust M15's closed-bar time when a live sample of its candles is
       genuinely M15-aligned. */
    const m15Sample = m15.status === "fulfilled" && m15.value
      && Array.isArray(m15.value.bars) ? m15.value.bars : null;
    const m15Trusted = m15Sample === null
      ? true : A.candleBatchIntegrity(m15Sample, "M15").ok;
    const now = Date.now();
    const tfRows = A.TIMEFRAMES.map((tf) => {
      const rec = tfList.find((x) => x && x.timeframe === tf);
      const hasClosed = Boolean(rec && rec.has_closed_bar === true)
        && (tf !== "M15" || m15Trusted);
      /* last_successful_update is a closed-bar open time (UTC seconds) only when
         has_closed_bar is true; otherwise it is a millisecond freshness value.
         Read it only in the first case so the two units can never be mixed. */
      const lastOpen = hasClosed && Number.isFinite(Number(rec.last_successful_update))
        ? Number(rec.last_successful_update) : null;
      const info = lastOpen ? A.closedCandleInfo(lastOpen, tf, now) : null;
      return [tf,
        badge(rec && rec.quality ? rec.quality.state : "UNKNOWN"),
        hasClosed ? badge("ok", "yes") : badge("warn", "no"),
        lastOpen ? h("span", { class: "mono", text: A.fmtEpochUTC(lastOpen) })
          : h("span", { class: "badge neutral", text: "no closed bar" }),
        info ? h("span", { text: info.overdueLabel }) : valueOrUnavailable(null),
        rec && rec.decision_grade ? badge("ok", "yes") : badge("neutral", "no")];
    });
    body.appendChild(card("Closed-candle status by timeframe",
      h("div", { class: "card__body" }, h("div", { class: "table-wrap" },
        h("table", { class: "data" }, [
          h("thead", null, h("tr", null, ["Timeframe", "Quality", "Closed bar?",
            "Last closed bar (UTC)", "Next-bar timing", "Decision grade"]
            .map((x) => h("th", { text: x })))),
          h("tbody", null, tfRows.map((r) => h("tr", null, [
            h("td", { text: r[0] }), h("td", null, r[1]), h("td", null, r[2]),
            h("td", null, r[3]), h("td", null, r[4]), h("td", null, r[5]),
          ]))),
        ])))));
    if (!m15Trusted) {
      body.appendChild(callout("warn",
        "M15 is reported as having no trustworthy closed bar: the stored M15 candles "
        + "are not MT5-aligned, so its watermark is withheld rather than shown as a "
        + "real market reading."));
    }

    if (h4Bars.length) {
      const last = h4Bars[h4Bars.length - 1];
      const prev = h4Bars.length > 1 ? h4Bars[h4Bars.length - 2] : null;
      const delta = prev ? Number(last.close) - Number(prev.close) : null;
      body.appendChild(card("Latest closed H4 candle",
        h("div", { class: "card__body" }, kv([
          ["Open time (UTC)", h("span", { text: A.fmtEpochUTC(last.time) })],
          ["Open", h("span", { text: A.fmtNum(last.open, 2) })],
          ["High", h("span", { text: A.fmtNum(last.high, 2) })],
          ["Low", h("span", { text: A.fmtNum(last.low, 2) })],
          ["Close", h("span", { text: A.fmtNum(last.close, 2) })],
          ["Change vs prior close", delta == null ? valueOrUnavailable(null)
            : h("span", { text: (delta >= 0 ? "+" : "") + A.fmtNum(delta, 2) })],
          ["Tick volume", h("span", { text: String(last.tick_volume) })],
          ["Source", h("span", { text: last.source_id || "—" })],
        ]))));
    } else {
      body.appendChild(card("Latest closed H4 candle",
        h("div", { class: "card__body" }, stateBlock("No H4 candles",
          "The backend holds no H4 bars, so no directional read is possible."))));
    }

    body.appendChild(card("How this page reads the market",
      h("div", { class: "card__body" }, h("div", { class: "stack" }, [
        h("div", { class: "note", text: "The H4 timeframe is the structural authority: its bias is the primary directional read." }),
        h("div", { class: "note", text: "M15 is the operational trigger; it confirms or refuses the H4 lean." }),
        h("div", { class: "note", text: "A forming candle is never used. A row is closed only once a full timeframe duration has elapsed." }),
        h("div", { class: "note", text: "ASTRA is shadow-only: this is decision support, never an order instruction." }),
      ]))));
  });
};

/* --- D3. Trade plan -------------------------------------------------------
   Shows the backend's proposed levels (entry / stop / target / reward-risk)
   and the recorded shadow outcomes. ASTRA never opens a real order, so this is
   a plan the owner can read and a record of what WOULD have happened. */
views.plan = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading the current plan and outcome record…"));

  Promise.allSettled([
    api("/api/v1/analysis/latest"),
    api("/api/v1/risk/latest"),
    api("/api/v1/shadow/positions"),
    api("/api/v1/shadow/outcomes"),
  ]).then(([a, risk, pos, out]) => {
    clear(body);
    const d = a.status === "fulfilled" ? a.value : null;
    const levels = (d && d.levels) || {};
    const dir = d && d.signal ? String(d.signal.direction || "NONE").toUpperCase() : "NONE";
    const hasPlan = Boolean(levels.entry != null || levels.stop_loss != null ||
      levels.take_profit != null);

    const rD = risk.status === "fulfilled" ? risk.value : null;
    const positions = pos.status === "fulfilled" && Array.isArray(pos.value) ? pos.value : [];
    const outcomes = out.status === "fulfilled" && Array.isArray(out.value) ? out.value : [];
    const tally = A.outcomeTally(outcomes);
    const parts = A.partitionPositions(positions);

    /* Headline: an honest statement of plan availability. */
    if (!d) {
      body.appendChild(callout("bad",
        "The backend is unreachable, so no plan or outcome record can be shown."));
    } else if (!hasPlan) {
      body.appendChild(callout("warn",
        dir === "NONE"
          ? "No trade plan is available: the backend reports no directional signal."
          : "No trade plan is available yet: the backend has not produced entry/stop/target levels."));
    } else {
      body.appendChild(callout("ok",
        "The backend has produced a plan. Levels are shown exactly as reported; nothing here is estimated."));
    }

    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Direction", dir, "signal.direction"),
      metricText("Entry", A.fmtNum(levels.entry, 2), "levels.entry", true),
      metricText("Stop loss", A.fmtNum(levels.stop_loss, 2), "levels.stop_loss", true),
      metricText("Target", A.fmtNum(levels.take_profit, 2), "levels.take_profit", true),
      metricText("Reward / risk", A.fmtNum(levels.reward_risk, 2), "levels.reward_risk", true),
      metricText("Suggested risk %", A.fmtNum(levels.suggested_risk_pct, 2),
        "levels.suggested_risk_pct", true),
    ]));

    /* The win-rate tile. Null when nothing has been decided yet. */
    const rateText = tally.winRate == null ? null
      : (tally.winRate * 100).toFixed(0) + "%";
    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Recorded outcomes", String(tally.decided),
        tally.total + " total row(s)", true),
      metricText("Win rate", rateText,
        tally.winRate == null ? "no decided outcome yet — not 0%"
          : tally.decided + " decided (win / loss / breakeven)", true),
      metricText("Average realized R", A.fmtNum(tally.avgR, 3),
        "mean of realized_r across outcomes", true),
      metricText("Open shadow positions", rD ? String(rD.open_positions) : null,
        "risk.latest", true),
    ]));

    body.appendChild(card("Plan status (why it is or is not actionable)",
      h("div", { class: "card__body" }, kv([
        ["Shadow only", badge("neutral", "true")],
        ["Plan available", hasPlan ? badge("ok", "available") : badge("neutral", "not available")],
        ["Entry method", valueOrUnavailable(levels.sl_method || levels.tp_method)],
        ["Plan reference time", d && d.timestamp
          ? h("span", { class: "mono", text: String(d.timestamp) })
          : valueOrUnavailable(null)],
        ["Risk bounded by guardian", rD ? badge(rD.risk_bounded_by_guardian ? "ok" : "warn",
          String(rD.risk_bounded_by_guardian === true)) : valueOrUnavailable(null)],
        ["Aggregate open risk", rD ? h("span", { text: A.fmtNum(rD.aggregate_open_risk_fraction, 4) }) : valueOrUnavailable(null)],
        ["Proposal", rD ? (rD.proposal_available ? badge("ok", "available")
          : badge("neutral", rD.proposal_reason || "none")) : valueOrUnavailable(null)],
      ]))));

    /* Open positions. */
    const openWrap = h("div", { class: "card__body card__body--flush" });
    if (!parts.open.length) {
      openWrap.appendChild(stateBlock("No open shadow position",
        "The backend reports no position currently open."));
    } else {
      openWrap.appendChild(positionTable(parts.open));
    }
    body.appendChild(card("Open shadow positions", openWrap));

    /* Closed outcomes: success or failure per trade, exactly as recorded. */
    const closedWrap = h("div", { class: "card__body card__body--flush" });
    if (!positions.length && !outcomes.length) {
      closedWrap.appendChild(stateBlock("No outcomes recorded yet",
        "The backend has not recorded any completed shadow trade."));
    } else if (outcomes.length) {
      closedWrap.appendChild(outcomeTable(outcomes));
    } else {
      closedWrap.appendChild(positionTable(parts.closed));
    }
    body.appendChild(card("Outcome record (did the plan win or lose?)", closedWrap,
      h("span", { class: "badge " + (tally.winRate == null ? "neutral" : "ok"),
        text: tally.winRate == null ? "rate unavailable"
          : (tally.winRate * 100).toFixed(0) + "% win rate" })));

    body.appendChild(callout("warn",
      "ASTRA is shadow-only. It never places, modifies, or closes a real order," +
      " and it never transmits MT5 credentials. Everything here is a record of" +
      " decision support."));
  });
};

function positionTable(rows) {
  const table = h("table", { class: "data" }, [
    h("thead", null, h("tr", null, ["Position", "Dir", "State", "Entry", "Stop",
      "Target", "R", "Opened (UTC)"].map((x) => h("th", { text: x })))),
    h("tbody", null, rows.map((p) => h("tr", null, [
      h("td", { class: "mono", text: String(p.position_id || "—") }),
      h("td", null, badge(p.direction || "UNKNOWN")),
      h("td", null, badge(p.state || "UNKNOWN")),
      h("td", { class: "num", text: A.fmtNum(p.entry_price, 2) || "—" }),
      h("td", { class: "num", text: A.fmtNum(p.stop_price, 2) || "—" }),
      h("td", { class: "num", text: A.fmtNum(p.target_price, 2) || "—" }),
      h("td", { class: "num", text: A.fmtNum(p.realized_r, 3) || "—" }),
      h("td", { class: "mono", text: p.opened_at ? String(p.opened_at) : "—" }),
    ]))),
  ]);
  return h("div", { class: "table-wrap" }, table);
}

function outcomeTable(rows) {
  const table = h("table", { class: "data" }, [
    h("thead", null, h("tr", null, ["Outcome", "Dir", "Result", "Entry", "Exit",
      "Realized R", "Bars", "Recorded (UTC)"].map((x) => h("th", { text: x })))),
    h("tbody", null, rows.map((o) => h("tr", null, [
      h("td", { class: "mono", text: String(o.outcome_id || "—") }),
      h("td", null, badge(o.direction || "UNKNOWN")),
      h("td", null, badge(A.outcomeClass(o.outcome_class), A.outcomeLabel(o.outcome_class))),
      h("td", { class: "num", text: A.fmtNum(o.entry_price, 2) || "—" }),
      h("td", { class: "num", text: A.fmtNum(o.exit_price, 2) || "—" }),
      h("td", { class: "num", text: A.fmtNum(o.realized_r, 3) || "—" }),
      h("td", { class: "num", text: o.bars_held == null ? "—" : String(o.bars_held) }),
      h("td", { class: "mono", text: o.recorded_at ? String(o.recorded_at) : "—" }),
    ]))),
  ]);
  return h("div", { class: "table-wrap" }, table);
}

/* --- E. History --- */
views.history = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  const headExtra = h("span", { class: "badge neutral", text: "—" });
  const tableWrap = h("div", { class: "card__body card__body--flush" });
  const limitSel = h("select", { class: "select", "aria-label": "Rows to load" },
    [25, 50, 100, 200].map((n) => h("option", { value: n, text: n + " rows" })));
  limitSel.value = String(state.historyLimit);
  limitSel.addEventListener("change", () => { state.historyLimit = Number(limitSel.value); load(); });

  body.appendChild(card("Analysis History", tableWrap,
    h("span", { class: "row", style: "gap:var(--sp-2)" }, [limitSel, headExtra])));
  mount.appendChild(h("div", { class: "note", style: "margin-top:var(--sp-3)",
    text: "History reflects only records the backend has actually stored. An empty result means no analysis has been recorded yet." }));

  function load() {
    clear(tableWrap);
    tableWrap.appendChild(loadingBlock("Loading history…"));
    headExtra.className = "badge neutral"; headExtra.textContent = "loading";
    api("/api/v1/analysis/history", { limit: state.historyLimit }).then((rows) => {
      clear(tableWrap);
      const list = Array.isArray(rows) ? rows : [];
      headExtra.className = "badge " + (list.length ? "ok" : "neutral");
      headExtra.textContent = list.length + " records";
      if (list.length === 0) {
        tableWrap.appendChild(stateBlock("No analysis history recorded",
          "The backend returned an empty history array. Nothing is shown rather than implying history exists."));
        return;
      }
      const table = h("table", { class: "data" });
      table.appendChild(h("thead", null, h("tr", null, [
        h("th", { text: "Timestamp" }), h("th", { text: "Symbol" }),
        h("th", { text: "Direction" }), h("th", { text: "Probability" }),
        h("th", { text: "Score" }), h("th", { text: "Model" }),
      ])));
      const tb = h("tbody");
      list.forEach((r) => {
        const s = (r && r.signal) || r || {};
        tb.appendChild(h("tr", null, [
          h("td", { class: "mono", text: (r && r.timestamp) || "—" }),
          h("td", { text: (r && r.symbol) || "—" }),
          h("td", null, badge(s.direction || "UNKNOWN")),
          h("td", { class: "num", text: s.probability == null ? "—" : (Number(s.probability) * 100).toFixed(1) + "%" }),
          h("td", { class: "num", text: A.fmtNum(s.score, 3) || "—" }),
          h("td", { text: s.model_version || "—" }),
        ]));
      });
      table.appendChild(tb);
      tableWrap.appendChild(h("div", { class: "table-wrap" }, table));
    }).catch((err) => {
      clear(tableWrap);
      headExtra.className = "badge bad"; headExtra.textContent = "error";
      tableWrap.appendChild(stateBlock("History unavailable", err.message, "error"));
    });
  }
  load();
};

/* --- F. Market data / MT5 --- */
views.marketdata = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading ingestion state…"));

  Promise.allSettled([api("/api/v1/bridge/status"), api("/api/v1/timeframes"),
    api("/api/v1/candles", { tf: "M15", limit: 1 })]).then(([b, tfs, latestBar]) => {
    clear(body);
    const bd = b.status === "fulfilled" ? b.value : null;
    const tfsD = tfs.status === "fulfilled" ? tfs.value : null;
    const bar = latestBar.status === "fulfilled" ? latestBar.value : null;
    const cov = A.coverageSummary(tfsD);

    if (bd && bd.bridge_process_state === "OFFLINE") {
      body.appendChild(callout("warn",
        "No MT5 bridge/sender has connected to this server yet. Candle ingestion will "
        + "begin when the Windows sender authenticates and pushes closed candles. "
        + "Server-observed state is shown below; sender-side details (MT5 terminal, "
        + "symbol, local queue) can only be read from the Windows application itself."));
    }

    const bars = bar && Array.isArray(bar.bars) ? bar.bars : [];
    /* The M15 store currently holds non-MT5 rows, so a "last closed candle"
       taken at face value would report a synthetic timestamp as real. Only
       surface the timestamp when the batch passes the integrity check. */
    const m15Integ = A.candleBatchIntegrity(bars, "M15");
    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Bridge state", bd ? bd.bridge_process_state : null, "server-observed", true),
      metricText("MT5 ready", bd ? String(bd.mt5_ready === true) : null, "bridge report", true),
      metricText("Resolved symbol", (bd && bd.resolved_symbol) || null, "broker symbol", true),
      metricText("Timeframes observed", cov.observed + " / " + cov.total, "with a closed bar", true),
      metricText("Last closed candle (M15)",
        bars.length && m15Integ.ok ? A.fmtEpochUTC(bars[bars.length - 1].time) : null,
        m15Integ.ok ? "from candle store" : "withheld: stored M15 rows are not MT5-aligned",
        true),
      metricText("Transport", bd ? bd.transport : null, "loopback only: " + (bd ? String(bd.loopback_only) : "—"), true),
    ]));

    if (bars.length && !m15Integ.ok) {
      body.appendChild(callout("warn",
        "The stored M15 candles are not a real MT5 series (" + m15Integ.misaligned
        + " of " + m15Integ.checked + " open times are not M15-aligned), so no M15 "
        + "timestamp is shown as a genuine market reading. The chart page withholds "
        + "these rows for the same reason."));
    }

    const grid = h("div", { class: "grid grid--2" });
    grid.appendChild(card("Server-Observed Bridge Status", h("div", { class: "card__body" },
      bd ? kv([
        ["Process state", badge(bd.bridge_process_state)],
        ["Startup stage", badge(bd.startup_stage)],
        ["Handshake OK", badge(bd.handshake_ok ? "ok" : "warn", String(bd.handshake_ok === true))],
        ["MT5 ready", badge(bd.mt5_ready ? "ok" : "warn", String(bd.mt5_ready === true))],
        ["Resolved symbol", valueOrUnavailable(bd.resolved_symbol)],
        ["Broker", valueOrUnavailable(bd.broker)],
        ["Server", valueOrUnavailable(bd.server)],
        ["Managed by app", String(bd.managed_by_application === true)],
        ["Requires manual cmd", String(bd.requires_manual_cmd === true)],
        ["Package available", String(bd.package_available === true)],
        ["Last successful request", valueOrUnavailable(A.fmtEpochUTC(bd.last_successful_request))],
        ["Last error", valueOrUnavailable(bd.last_error, "none reported")],
      ]) : stateBlock("Bridge status unavailable", "The endpoint did not respond."))));

    const ingBody = h("div", { class: "card__body stack" });
    ingBody.appendChild(h("div", { class: "note",
      text: "Per-timeframe ingestion coverage from the authoritative store." }));
    ingBody.appendChild(matrixNode(tfsD, false));
    grid.appendChild(card("Ingestion Coverage", ingBody));
    body.appendChild(grid);

    const wm = h("div", { class: "table-wrap" });
    const table = h("table", { class: "data" });
    table.appendChild(h("thead", null, h("tr", null, [
      h("th", { text: "Timeframe" }), h("th", { text: "Last update" }),
      h("th", { text: "Freshness" }), h("th", { text: "Age" }),
      h("th", { text: "Max age" }), h("th", { text: "Decision grade" }),
    ])));
    const tb = h("tbody");
    (Array.isArray(tfsD) ? tfsD : []).forEach((t) => {
      const f = t.freshness;
      tb.appendChild(h("tr", null, [
        h("td", { class: "mono", text: t.timeframe }),
        h("td", { class: "mono", text: A.fmtEpochUTC(t.last_successful_update) || "—" }),
        h("td", null, f ? badge(f.state) : badge("UNKNOWN", "unobserved")),
        h("td", { class: "num", text: f ? (A.fmtAge(f.age_millis) || "—") : "—" }),
        h("td", { class: "num", text: f && f.max_age_millis != null ? Math.round(f.max_age_millis / 1000) + "s" : "—" }),
        h("td", null, badge(t.decision_grade ? "ok" : "warn", t.decision_grade ? "yes" : "no")),
      ]));
    });
    table.appendChild(tb);
    wm.appendChild(table);
    body.appendChild(card("Watermarks & Freshness", h("div", { class: "card__body card__body--flush" }, wm)));
  });
};

/* --- G. Research & shadow --- */
views.research = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading research & shadow state…"));

  Promise.allSettled([api("/api/v1/research/status"), api("/api/v1/shadow/positions"),
    api("/api/v1/shadow/outcomes"), api("/api/v1/governance/status")]).then(([rs, pos, out, gov]) => {
    clear(body);
    const r = rs.status === "fulfilled" ? rs.value : null;
    const p = pos.status === "fulfilled" ? pos.value : null;
    const o = out.status === "fulfilled" ? out.value : null;
    const g = gov.status === "fulfilled" ? gov.value : null;

    body.appendChild(callout("ok",
      "Research output never grants execution authority. ASTRA remains shadow-only; "
      + "no orders are placed and no trade results are simulated."));

    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Research mode", r ? r.mode : null, "research/status", true),
      metricText("Experiments", r ? String(r.experiment_count) : null, "recorded", true),
      metricText("Failures", r ? String(r.failure_count) : null, "recorded", true),
      metricText("Shadow positions", p ? String(p.length) : null, "recorded", true),
      metricText("Shadow outcomes", o ? String(o.length) : null, "recorded", true),
      metricText("Live trading authorised", g ? String(g.live_trading_authorised === true) : null, "governance", true),
    ]));

    const grid = h("div", { class: "grid grid--2" });
    grid.appendChild(card("Research Status", h("div", { class: "card__body" },
      r ? kv([
        ["Available", badge(r.available ? "ok" : "neutral", String(r.available === true))],
        ["Mode", badge(r.mode)],
        ["Note", h("span", { style: "font-family:var(--font-sans);font-size:var(--fs-xs)", text: r.note || "—" })],
        ["Experiment count", String(r.experiment_count)],
        ["Failure count", String(r.failure_count)],
      ]) : stateBlock("Research status unavailable", "The endpoint did not respond."))));

    grid.appendChild(card("Governance", h("div", { class: "card__body" },
      g ? kv([
        ["Live trading authorised", badge(g.live_trading_authorised ? "bad" : "ok",
          String(g.live_trading_authorised === true))],
        ["Pending approvals", String(g.pending_count)],
        ["History entries", String(Array.isArray(g.history) ? g.history.length : 0)],
      ]) : stateBlock("Governance unavailable", "The endpoint did not respond."))));
    body.appendChild(grid);

    const listCard = (title, arr, cols) => {
      if (!Array.isArray(arr) || arr.length === 0) {
        return card(title, stateBlock("No records", "The backend returned an empty list."));
      }
      const t = h("table", { class: "data" });
      t.appendChild(h("thead", null, h("tr", null, cols.map((c) => h("th", { text: c })))));
      const tb = h("tbody");
      arr.forEach((row) => {
        tb.appendChild(h("tr", null, cols.map((c) => {
          const v = row[c.toLowerCase().replace(/[^a-z0-9]+/g, "_")];
          return h("td", { class: "num", text: v == null ? "—" : String(v) });
        })));
      });
      t.appendChild(tb);
      return card(title, h("div", { class: "card__body card__body--flush" },
        h("div", { class: "table-wrap" }, t)));
    };
    body.appendChild(listCard("Shadow Positions", p, ["Id", "Symbol", "Direction", "Status"]));
    body.appendChild(listCard("Shadow Outcomes", o, ["Id", "Symbol", "Outcome", "Status"]));
  });
};

/* --- H. Risk & system --- */
views.system = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading system state…"));

  Promise.allSettled([api("/api/v1/health"), api("/api/v1/health/v1"),
    api("/api/v1/system/state"), api("/api/v1/risk/latest"), api("/api/v1/timeframes")]).then(
    ([health, hv1, sys, risk, tfs]) => {
    clear(body);
    const hd = health.status === "fulfilled" ? health.value : null;
    const v1 = hv1.status === "fulfilled" ? hv1.value : null;
    const sd = sys.status === "fulfilled" ? sys.value : null;
    const rd = risk.status === "fulfilled" ? risk.value : null;
    const tfsD = tfs.status === "fulfilled" ? tfs.value : null;
    const cov = A.coverageSummary(tfsD);

    const anyDegraded = (hd && hd.aggregate !== "OK") || (v1 && v1.status !== "ok")
      || (sd && !sd.ready);
    body.appendChild(callout(anyDegraded ? "warn" : "ok",
      anyDegraded
        ? "The backend reports a degraded or not-ready condition. A green indicator here would be misleading, so this is shown as a warning."
        : "Backend reports a healthy, ready state."));

    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Aggregate", hd ? hd.aggregate : null, "health.aggregate", true),
      metricText("Decision-grade data", hd ? String(hd.decision_grade_data === true) : null,
        "health.decision_grade_data", true),
      metricText("Unknown is not safe", hd ? String(hd.unknown_is_not_safe === true) : null,
        "health.unknown_is_not_safe", true),
      metricText("Ready", sd ? String(sd.ready === true) : null, "system.ready", true),
      metricText("Mode", sd ? sd.mode : null, "system.mode", true),
      metricText("Shadow only", sd ? String(sd.shadow_only === true) : null, "system.shadow_only", true),
      metricText("Uptime", v1 && v1.uptime_sec != null ? Math.round(v1.uptime_sec / 60) + " min" : null,
        "health/v1", true),
      metricText("Timeframes observed", cov.observed + " / " + cov.total, "coverage", true),
    ]));

    const grid = h("div", { class: "grid grid--2" });
    grid.appendChild(card("Health", h("div", { class: "card__body" }, hd ? kv([
      ["Aggregate", badge(hd.aggregate)],
      ["Decision-grade data", badge(hd.decision_grade_data ? "ok" : "warn", String(hd.decision_grade_data === true))],
      ["Unknown is not safe", badge(hd.unknown_is_not_safe ? "warn" : "ok", String(hd.unknown_is_not_safe === true))],
      ["Observed at", valueOrUnavailable(A.fmtMillisUTC(hd.observed_at))],
      ["v1 status", v1 ? badge(v1.status) : badge("bad", "error")],
      ["v1 bridge", v1 ? badge(v1.bridge) : badge("bad", "error")],
      ["v1 version", v1 ? valueOrUnavailable(v1.version) : null],
    ]) : stateBlock("Health unavailable", "The endpoint did not respond."))));

    grid.appendChild(card("System State", h("div", { class: "card__body" }, sd ? kv([
      ["Mode", badge(sd.mode)],
      ["Ready", badge(sd.ready ? "ok" : "warn", String(sd.ready === true))],
      ["Shadow only", badge(sd.shadow_only ? "ok" : "bad", String(sd.shadow_only === true))],
      ["Bridge state", badge(sd.bridge_state)],
      ["Startup stage", badge(sd.startup_stage)],
    ]) : stateBlock("System state unavailable", "The endpoint did not respond."))));
    body.appendChild(grid);

    const riskBody = h("div", { class: "card__body" }, rd ? kv([
      ["Available", badge(rd.available ? "ok" : "neutral", String(rd.available === true))],
      ["Open positions", String(rd.open_positions)],
      ["Aggregate open risk fraction", String(rd.aggregate_open_risk_fraction)],
      ["Risk bounded by guardian", badge(rd.risk_bounded_by_guardian ? "ok" : "warn",
        String(rd.risk_bounded_by_guardian === true))],
      ["Proposal available", badge(rd.proposal_available ? "ok" : "neutral", String(rd.proposal_available === true))],
      ["Proposal reason", valueOrUnavailable(rd.proposal_reason, "—")],
    ]) : stateBlock("Risk unavailable", "The endpoint did not respond."));
    body.appendChild(card("Risk Bounds (shadow)", riskBody));

    const degBody = h("div", { class: "card__body" });
    if (hd && Array.isArray(hd.degraded_reasons) && hd.degraded_reasons.length) {
      degBody.appendChild(h("ul", { class: "stack" }, hd.degraded_reasons.map((r) =>
        h("li", { class: "mono", style: "font-size:var(--fs-xs);color:var(--text-muted)", text: r }))));
    } else {
      degBody.appendChild(stateBlock("No degraded reasons", "Backend reported none, or health was unavailable."));
    }
    body.appendChild(card("Operational Warnings", degBody));
  });
};

/* --- I. Audit --- */
views.audit = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);
  body.appendChild(loadingBlock("Loading audit records…"));

  api("/api/v1/audit/recent").then((d) => {
    clear(body);
    const records = Array.isArray(d.audit_records) ? d.audit_records : [];
    const incidents = Array.isArray(d.active_incidents) ? d.active_incidents : [];
    body.appendChild(h("div", { class: "grid grid--metrics" }, [
      metricText("Records", String(d.count != null ? d.count : records.length), "audit/recent", true),
      metricText("Stream size", String(d.audit_stream_size != null ? d.audit_stream_size : "—"), "backend buffer", true),
      metricText("Active incidents", String(incidents.length), "open", true),
    ]));
    body.appendChild(callout("ok",
      "Only public audit metadata is shown. Authentication tokens and credentials are never exposed in this interface."));

    if (records.length === 0) {
      body.appendChild(card("Audit Records", stateBlock("No audit records",
        "The backend returned an empty audit stream.")));
    } else {
      const t = h("table", { class: "data" });
      t.appendChild(h("thead", null, h("tr", null, [
        h("th", { text: "Timestamp" }), h("th", { text: "Kind" }),
        h("th", { text: "Actor" }), h("th", { text: "Detail" }),
      ])));
      const tb = h("tbody");
      records.forEach((r) => {
        tb.appendChild(h("tr", null, [
          h("td", { class: "mono", text: r.timestamp != null ? (A.fmtEpochUTC(r.timestamp) || String(r.timestamp)) : "—" }),
          h("td", { text: r.kind || r.type || "—" }),
          h("td", { text: r.actor || "—" }),
          h("td", { text: r.detail || r.message || "—" }),
        ]));
      });
      t.appendChild(tb);
      body.appendChild(card("Audit Records", h("div", { class: "card__body card__body--flush" },
        h("div", { class: "table-wrap" }, t))));
    }

    body.appendChild(card("Active Incidents", incidents.length
      ? h("div", { class: "card__body" }, h("ul", { class: "stack" }, incidents.map((i) =>
          h("li", { class: "mono", style: "font-size:var(--fs-xs)", text: typeof i === "string" ? i : JSON.stringify(i) }))))
      : stateBlock("No active incidents", "The backend reports no open incidents.")));
  }).catch((err) => {
    clear(body);
    body.appendChild(callout("bad", "Audit unavailable: " + err.message));
  });
};

/* --- J. Settings (local-only preferences) --- */
views.settings = function (mount) {
  const body = h("div", { class: "stack" });
  mount.appendChild(body);

  const tfSel = h("select", { class: "select", id: "setChartTf", "aria-label": "Default chart timeframe" },
    A.TIMEFRAMES.map((tf) => h("option", { value: tf, text: tf })));
  tfSel.value = state.chartTf;
  tfSel.addEventListener("change", () => {
    state.chartTf = tfSel.value;
    localStorage.setItem("astra.chartTf", tfSel.value);
    saved.textContent = "Saved: default chart timeframe = " + tfSel.value;
  });

  const refreshSel = h("select", { class: "select", id: "setRefresh", "aria-label": "Auto-refresh interval" },
    [[0, "Manual only"], [15000, "15 seconds"], [30000, "30 seconds"], [60000, "1 minute"], [300000, "5 minutes"]]
      .map(([v, t]) => h("option", { value: v, text: t })));
  refreshSel.value = String(localStorage.getItem("astra.refreshMs") || REFRESH_MS);
  refreshSel.addEventListener("change", () => {
    localStorage.setItem("astra.refreshMs", refreshSel.value);
    saved.textContent = "Saved: auto-refresh = " + refreshSel.options[refreshSel.selectedIndex].text
      + " (applies on reload)";
  });

  const navToggle = h("input", { type: "checkbox", id: "setNavCollapsed" });
  navToggle.checked = state.navCollapsed;
  navToggle.addEventListener("change", () => {
    state.navCollapsed = navToggle.checked;
    localStorage.setItem("astra.navCollapsed", navToggle.checked ? "1" : "0");
    applyNavMode();
    saved.textContent = "Saved: sidebar collapsed = " + navToggle.checked;
  });

  const saved = h("p", { class: "note", role: "status", "aria-live": "polite" });

  body.appendChild(callout("ok",
    "These settings are stored only in this browser's localStorage. They never "
    + "transmit credentials and cannot alter backend behaviour."));

  const form = h("div", { class: "card__body stack" }, [
    h("div", null, [h("label", { class: "metric__label", for: "setChartTf", text: "Default chart timeframe" }), tfSel]),
    h("div", null, [h("label", { class: "metric__label", for: "setRefresh", text: "Auto-refresh interval" }), refreshSel]),
    h("label", { class: "row", style: "gap:var(--sp-3);cursor:pointer" }, [navToggle,
      h("span", { text: "Start with the sidebar collapsed" })]),
    saved,
  ]);
  body.appendChild(card("Display Preferences", form));

  const info = h("div", { class: "card__body" }, kv([
    ["Backend base URL", valueOrUnavailable(BACKEND || null, "not configured")],
    ["Configuration", badge(CONFIGURED ? "ok" : "bad", CONFIGURED ? "configured" : "unconfigured")],
    ["Auto-refresh", String(localStorage.getItem("astra.refreshMs") || REFRESH_MS) + " ms"],
    ["Secrets in this bundle", badge("ok", "none")],
    ["Mode", badge("ok", "shadow only")],
  ]));
  body.appendChild(card("Runtime Information", info));
};

/* ------------------------------------------------------- timeframe detail */
function openTfDetail(tf) {
  const dlg = h("div", { class: "scrim", style: "opacity:1;pointer-events:auto", role: "dialog",
    "aria-modal": "true", "aria-label": tf + " snapshot" });
  const box = h("div", { class: "card", style: "position:fixed;z-index:60;left:50%;top:50%;transform:translate(-50%,-50%);width:min(560px,92vw);max-height:86vh;overflow:auto" });
  box.appendChild(h("div", { class: "card__head" }, [
    h("h2", { text: tf + " — Snapshot" }),
    h("div", { class: "spacer" }),
    h("button", { class: "btn btn--ghost btn--icon", "aria-label": "Close", text: "✕", on: { click: close } }),
  ]));
  const bd = h("div", { class: "card__body" }, loadingBlock("Loading snapshot…"));
  box.appendChild(bd);
  dlg.appendChild(box);
  dlg.addEventListener("click", (e) => { if (e.target === dlg) close(); });
  document.addEventListener("keydown", onKey);
  function onKey(e) { if (e.key === "Escape") close(); }
  function close() { document.removeEventListener("keydown", onKey); dlg.remove(); }
  document.body.appendChild(dlg);
  box.querySelector("button").focus();

  api("/api/v1/timeframes/" + encodeURIComponent(tf) + "/snapshot").then((s) => {
    clear(bd);
    bd.appendChild(kv([
      ["Timeframe", h("span", { class: "mono", text: s.timeframe })],
      ["Role", valueOrUnavailable((A.TF_ROLE[tf] || {}).label)],
      ["Observed", badge(s.observed ? "ok" : "neutral", String(s.observed === true))],
      ["Has closed bar", badge(s.has_closed_bar ? "ok" : "neutral", String(s.has_closed_bar === true))],
      ["Quality", badge(s.quality ? s.quality.state : "UNKNOWN")],
      ["Decision grade", badge(s.decision_grade ? "ok" : "warn", String(s.decision_grade === true))],
      ["Freshness", s.freshness ? badge(s.freshness.state) : badge("UNKNOWN", "n/a")],
      ["Age", valueOrUnavailable(s.freshness ? A.fmtAge(s.freshness.age_millis) : null)],
      ["Last update", valueOrUnavailable(A.fmtEpochUTC(s.last_successful_update))],
      ["Open", valueOrUnavailable(A.fmtNum(s.open, 2))],
      ["High", valueOrUnavailable(A.fmtNum(s.high, 2))],
      ["Low", valueOrUnavailable(A.fmtNum(s.low, 2))],
      ["Close", valueOrUnavailable(A.fmtNum(s.close, 2))],
      ["Open time", valueOrUnavailable(A.fmtEpochUTC(s.open_time))],
      ["Sequence", valueOrUnavailable(s.sequence)],
      ["Capability impact", valueOrUnavailable((s.capability_impact || []).map((c) => c.capability + ":" + c.impact).join(", ") || null, "none")],
    ]));
  }).catch((err) => {
    clear(bd);
    bd.appendChild(stateBlock("Snapshot unavailable", err.message, "error"));
  });
}

/* ---------------------------------------------------------------- shell */

function applyNavMode() {
  const app = document.getElementById("app");
  if (window.innerWidth <= 980) {
    app.dataset.nav = "closed";
  } else {
    app.dataset.nav = state.navCollapsed ? "collapsed" : "expanded";
  }
}

function buildShell() {
  const app = document.getElementById("app");

  const brandText = h("div", { class: "brand__text" }, [
    h("div", { class: "brand__name", text: "ASTRA" }),
    h("span", { class: "brand__sub", text: "Market Intelligence" }),
  ]);
  const brand = h("div", { class: "brand" }, [
    h("img", { class: "brand__mark", src: "/assets/astra-symbol.svg", alt: "" }),
    brandText,
  ]);

  const nav = h("nav", { class: "nav", "aria-label": "Primary" });
  NAV.forEach((g) => {
    const grp = h("div", { class: "nav__group" }, [
      h("div", { class: "nav__label", text: g.group }),
    ]);
    g.items.forEach((item) => {
      grp.appendChild(h("button", {
        class: "nav__item", dataset: { route: item.id }, type: "button",
        on: { click: () => { navigate(item.id); closeDrawer(); } },
      }, [
        h("span", { class: "nav__icon", html: ICONS[item.icon] }),
        h("span", { class: "nav__text", text: item.label }),
      ]));
    });
    nav.appendChild(grp);
  });

  const collapseBtn = h("button", { class: "sidebar__collapse", type: "button",
    "aria-label": "Collapse or expand sidebar",
    on: { click: () => {
      state.navCollapsed = !state.navCollapsed;
      localStorage.setItem("astra.navCollapsed", state.navCollapsed ? "1" : "0");
      applyNavMode();
    } } }, [
    h("span", { class: "nav__icon", html: ICONS.collapse }),
    h("span", { text: "Collapse" }),
  ]);

  const sidebar = h("aside", { class: "sidebar", id: "sidebar" }, [
    brand, nav, h("div", { class: "sidebar__foot" }, collapseBtn),
  ]);

  const scrim = h("div", { class: "scrim", on: { click: closeDrawer } });

  const menuBtn = h("button", { class: "topbar__menu", type: "button",
    "aria-label": "Open navigation", "aria-controls": "sidebar", html: ICONS.menu,
    on: { click: openDrawer } });

  const title = h("div", { class: "topbar__title" }, [
    h("h1", { id: "viewTitle", text: "Dashboard" }),
    h("p", { id: "viewSubtitle", text: "" }),
  ]);
  const ctx = h("div", { class: "topbar__ctx" }, [
    h("span", null, [h("span", { class: "k", text: "Instrument" }), "XAUUSD"]),
    h("span", null, [h("span", { class: "k", text: "Mode" }), h("span", { id: "ctxMode", text: "—" })]),
    h("span", { class: "health" }, [h("span", { class: "dot neutral", id: "healthDot" }),
      h("span", { id: "healthText", text: "connecting…" })]),
  ]);
  const refreshBtn = h("button", { class: "btn btn--ghost btn--icon", title: "Refresh",
    "aria-label": "Refresh current view", html: ICONS.refresh, on: { click: () => renderRoute(true) } });

  const topbar = h("header", { class: "topbar" }, [menuBtn, title, h("div", { class: "topbar__spacer" }), ctx, refreshBtn]);

  const content = h("main", { class: "content", id: "viewMount", tabindex: "-1" });
  const footer = h("footer", { class: "footer" }, [
    h("span", { text: "Shadow-only market intelligence. No order execution." }),
    h("span", { id: "footerUpdated", text: "never" }),
  ]);

  const bottomnav = h("nav", { class: "bottomnav", "aria-label": "Primary mobile" },
    BOTTOM_NAV.map((id) => {
      const m = routeMeta(id);
      return h("button", { class: "bottomnav__item", dataset: { route: id }, type: "button",
        on: { click: () => navigate(id) } }, [
        h("span", { html: ICONS[m.icon] }),
        h("span", { text: m.label.split(" ")[0] }),
      ]);
    }));

  const main = h("div", { class: "main" }, [topbar, content, footer]);
  clear(app);
  app.appendChild(h("a", { class: "skip-link", href: "#viewMount", text: "Skip to content" }));
  app.appendChild(sidebar);
  app.appendChild(scrim);
  app.appendChild(main);
  app.appendChild(bottomnav);
}

function openDrawer() {
  document.getElementById("app").dataset.nav = "open";
  const first = document.querySelector("#sidebar .nav__item");
  if (first) first.focus();
}
function closeDrawer() {
  const app = document.getElementById("app");
  if (window.innerWidth <= 980) app.dataset.nav = "closed";
}

/* --------------------------------------------------------------- routing */

function currentRoute() {
  const raw = (location.hash || "").replace(/^#\/?/, "");
  const id = raw.split("?")[0];
  return NAV_FLAT.some((n) => n.id === id) ? id : "dashboard";
}

function navigate(id) {
  if (!NAV_FLAT.some((n) => n.id === id)) id = "dashboard";
  if (currentRoute() === id) { renderRoute(); return; }
  location.hash = "#/" + id;
}

function syncNavActive() {
  const id = state.route;
  Array.prototype.forEach.call(document.querySelectorAll("[data-route]"), (el) => {
    if (el.dataset.route === id) el.setAttribute("aria-current", "page");
    else el.removeAttribute("aria-current");
  });
  const meta = routeMeta(id);
  document.getElementById("viewTitle").textContent = meta.title;
  document.getElementById("viewSubtitle").textContent = meta.subtitle;
  document.title = "ASTRA — " + meta.title;
}

function renderRoute(forceRefresh) {
  state.route = currentRoute();
  syncNavActive();
  const mount = document.getElementById("viewMount");
  clear(mount);
  const view = views[state.route] || views.dashboard;
  view(mount, forceRefresh);
  mount.focus({ preventScroll: true });
}

/* ------------------------------------------------------------ health poll */

function pollStatus() {
  Promise.allSettled([api("/api/v1/health"), api("/api/v1/system/state")]).then(([h, s]) => {
    const hd = h.status === "fulfilled" ? h.value : null;
    const sd = s.status === "fulfilled" ? s.value : null;
    state.health = hd;
    state.system = sd;
    const dotEl = document.getElementById("healthDot");
    const txtEl = document.getElementById("healthText");
    const modeEl = document.getElementById("ctxMode");
    if (!CONFIGURED) {
      dotEl.className = "dot bad"; txtEl.textContent = "not configured";
      if (modeEl) modeEl.textContent = "—";
    } else if (!hd && !sd) {
      dotEl.className = "dot bad"; txtEl.textContent = "backend unreachable";
      if (modeEl) modeEl.textContent = "UNAVAILABLE";
    } else {
      const cls = hd ? A.statusClass(hd.aggregate) : A.statusClass(sd && sd.mode);
      dotEl.className = "dot " + cls;
      txtEl.textContent = hd ? ("backend " + (hd.aggregate === "OK" ? "online" : hd.aggregate.toLowerCase())) : "backend online";
      if (modeEl) modeEl.textContent = (sd && sd.mode) || "—";
    }
    state.lastUpdated = Date.now();
    document.getElementById("footerUpdated").textContent =
      "updated " + A.fmtMillisUTC(state.lastUpdated);
  });
}

/* ------------------------------------------------------------------ init */

function init() {
  buildShell();
  applyNavMode();
  window.addEventListener("resize", applyNavMode);
  window.addEventListener("hashchange", () => renderRoute());
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) pollStatus();
  });
  renderRoute();
  pollStatus();

  /* Auto-refresh only while the tab is visible; interval is user-configurable
     (0 disables polling). Refreshing is paused for reduced-motion/lightweight
     hosts by simply keeping the interval coarse. */
  const configured = Number(localStorage.getItem("astra.refreshMs") || REFRESH_MS);
  if (configured > 0) {
    setInterval(() => { if (!document.hidden) { pollStatus(); renderRoute(); } }, configured);
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
