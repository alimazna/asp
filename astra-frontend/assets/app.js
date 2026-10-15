/* ASTRA web dashboard.

Renders real backend responses only. Every state is explicit:
  * loading  — request in flight;
  * ok       — value present;
  * stale    — backend reachable but the payload is stale/absent;
  * unavailable — backend unreachable or returned an error.

The frontend never manufactures prices, signals, probabilities or candles.
*/

"use strict";

const BACKEND = (window.ASTRA_BACKEND_BASE || "").replace(/\/+$/, "");
const REFRESH_MS = window.ASTRA_REFRESH_MS || 30000;
const TIMEFRAMES = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"];

const $ = (id) => document.getElementById(id);

function api(path, timeoutMs = 12000) {
  if (!BACKEND || BACKEND.includes("REPLACE_WITH_BACKEND_URL")) {
    return Promise.reject(new Error("backend base URL is not configured"));
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  return fetch(BACKEND + path, {
    headers: { Accept: "application/json" },
    signal: controller.signal,
  })
    .then((res) => res.json().catch(() => ({ rawStatus: res.status })))
    .finally(() => clearTimeout(timer));
}

function esc(value) {
  const string = String(value ?? "");
  return string.replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[ch]));
}

function fmtTime(epochSeconds) {
  if (epochSeconds == null || Number.isNaN(epochSeconds)) return "—";
  return new Date(epochSeconds * 1000).toISOString().replace("T", " ").slice(0, 19)
    + " UTC";
}

function badge(state) {
  const s = String(state || "").toLowerCase();
  const cls = s.includes("stale") ? "warn"
    : (s.includes("unavail") || s.includes("invalid") || s.includes("error")) ? "bad"
    : (s.includes("valid") || s === "ok" || s === "fresh" || s === "ready") ? "ok"
    : "neutral";
  return `<span class="badge ${cls}">${esc(state || "UNKNOWN")}</span>`;
}

function setHealth(state, text) {
  const dot = $("healthDot");
  dot.className = "dot " + state;
  $("healthText").textContent = text;
}

function resetPanel() { setHealth("", "connecting…"); }

// --- system + health -------------------------------------------------------

async function loadSystem() {
  const [state, health] = await Promise.allSettled([
    api("/api/v1/system/state"),
    api("/api/v1/health"),
  ]);
  const stateData = state.status === "fulfilled" ? state.value.data : null;
  const healthData = health.status === "fulfilled" ? health.value.data : null;
  const anyError = (state.status === "rejected" && health.status === "rejected");

  if (anyError) {
    setHealth("bad", "backend unavailable");
    $("sysMode").textContent = "UNAVAILABLE";
    $("dataGrade").innerHTML = '<span class="badge bad">unreachable</span>';
    return;
  }

  let mode = "(unknown)";
  let status = "warn";
  let statusText = "backend online";
  if (stateData) {
    mode = stateData.mode || "UNKNOWN";
    if (stateData.ready) status = "ok";
    if (!stateData.ready) status = "warn";
    $("sysMode").textContent = esc(mode);
    const shadow = stateData.shadow_only === true;
    $("shadowPill").textContent = shadow ? "shadow-only" : "NOT shadow-only";
    $("shadowPill").style.color = shadow ? "var(--green)" : "var(--red)";
  } else if (health.status === "fulfilled") {
    mode = "STARTING";
    $("sysMode").textContent = "STARTING";
  }

  if (healthData) {
    const grade = healthData.decision_grade_data === true ? "ok" : "degraded";
    $("dataGrade").innerHTML = badge(grade);
    if (!healthData.decision_grade_data) status = "warn";
    if (stateData && !stateData.ready) status = "warn";
  } else {
    $("dataGrade").innerHTML = '<span class="badge neutral">unseen</span>';
  }

  if (healthData && healthData.aggregate === "DEGRADED") statusText = "degraded";
  setHealth(status, statusText);
}

// --- analysis + context ----------------------------------------------------

async function loadAnalysis() {
  const result = await api("/api/v1/analysis/latest").catch(() => null);
  const el = $("analysisBody");
  if (!result || result.error) {
    el.className = "empty";
    el.textContent = "No analysis available yet (backend returns no value).";
    return;
  }
  const data = result.data || {};
  const s = data.signal || {};
  const prob = (s.probability == null || Number.isNaN(Number(s.probability)))
    ? null : Number(s.probability);
  const cal = s.probability_calibrated === true;

  let html = `<table>
    <tr><th>Direction</th><td>${badge(s.direction || "UNKNOWN")}</td></tr>
    <tr><th>Horizon</th><td>${esc(s.horizon || "—")}</td></tr>
    <tr><th>Probability</th>
      <td>${prob == null
        ? '<span class="unavail-mark">not available</span>'
        : (prob * 100).toFixed(1) + "%"
          + (cal ? "" : ' <span class="badge warn">uncalibrated</span>')}</td></tr>
    <tr><th>Score</th><td>${s.score == null ? "—" : esc(s.score)}</td></tr>
    <tr><th>Model</th><td>${esc(s.model_version || "—")}</td></tr>
    <tr><th>Timestamp</th><td>${esc(data.timestamp || "—")}</td></tr>
  </table>`;
  el.className = "";
  el.innerHTML = html;
}

async function loadContext() {
  const result = await api("/api/v1/context/latest").catch(() => null);
  const el = $("contextBody");
  if (!result || result.error) {
    el.className = "empty";
    el.textContent = "No context available yet.";
    return;
  }
  const c = (result.data && result.data.context) || {};
  if (!c || Object.keys(c).length === 0) {
    el.className = "empty";
    el.textContent = "No context available yet.";
    return;
  }
  el.className = "";
  el.innerHTML = `<table>
    <tr><th>Regime</th><td>${badge(c.regime)}</td></tr>
    <tr><th>H4 bias</th><td>${esc(c.h4_bias || "—")}</td></tr>
    <tr><th>M15 trigger</th><td>${esc(c.m15_trigger || "—")}</td></tr>
    <tr><th>MTF agreement</th><td>${c.mtf_agreement == null
      ? "—" : (c.mtf_agreement * 100).toFixed(0) + "%"}</td></tr>
    <tr><th>Volatility</th><td>${esc(c.volatility_state || "—")}</td></tr>
  </table>`;
}

// --- candles ---------------------------------------------------------------

let chartTf = "M15";

function initTimeframeSelect() {
  const select = $("tfSelect");
  for (const tf of TIMEFRAMES) {
    const option = document.createElement("option");
    option.value = tf;
    option.textContent = tf;
    select.appendChild(option);
  }
  select.value = chartTf;
  select.addEventListener("change", () => {
    chartTf = select.value;
    loadAndDrawChart();
  });
}

async function loadAndDrawChart() {
  const statusEl = $("chartStatus");
  const meta = $("chartMeta");
  statusEl.textContent = "loading…";
  const result = await api(`/api/v1/candles?tf=${encodeURIComponent(chartTf)}&limit=120`)
    .catch(() => null);
  if (!result || result.error || !result.data) {
    statusEl.textContent = "No candle data available from the backend.";
    meta.textContent = "";
    drawChart([]);
    return;
  }
  const bars = (result.data.bars || []).filter((b) =>
    [b.open, b.high, b.low, b.close].every((v) =>
      typeof v === "number" && Number.isFinite(v)));
  meta.textContent = `${result.data.symbol || "?"} · ${chartTf} · ${bars.length} closed bar(s)`
    + (result.data.count != null ? ` (count=${result.data.count})` : "");
  if (bars.length === 0) {
    statusEl.textContent = "Backend returned no closed bars for this timeframe.";
  } else {
    statusEl.textContent = "Real closed candles (read-only). Latest: "
      + fmtTime(bars[bars.length - 1].time);
  }
  drawChart(bars);
}

function drawChart(bars) {
  const canvas = $("chart");
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(rect.width, 320);
  const height = 260;
  canvas.width = width * dpr;
  canvas.height = height * dpr;
  canvas.style.height = height + "px";
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  if (bars.length === 0) return;

  const lows = bars.map((b) => b.low);
  const highs = bars.map((b) => b.high);
  const min = Math.min(...lows);
  const max = Math.max(...highs);
  const pad = 12;
  const range = max - min || 1;

  const bw = width / bars.length;
  const y = (v) => pad + (height - 2 * pad) * (1 - (v - min) / range);

  ctx.strokeStyle = "#2a3760";
  ctx.lineWidth = 1;
  // gridlines
  for (let i = 1; i < 4; i++) {
    const gy = pad + ((height - 2 * pad) / 4) * i;
    ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke();
  }

  bars.forEach((b, i) => {
    const x = i * bw + bw / 2;
    const up = b.close >= b.open;
    const color = up ? "#2fbf71" : "#e5484d";
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(x, y(b.high));
    ctx.lineTo(x, y(b.low));
    ctx.stroke();

    ctx.fillStyle = color;
    const bodyTop = y(Math.max(b.open, b.close));
    const bodyH = Math.max(Math.abs(y(b.open) - y(b.close)), 1.2);
    ctx.fillRect(x - bw * 0.34, bodyTop, bw * 0.68, bodyH);
  });
}

// --- transport -------------------------------------------------------------

async function loadTransport() {
  const result = await api("/api/v1/bridge/status").catch(() => null);
  const el = $("transportBody");
  if (!result || result.error) {
    el.className = "empty";
    el.textContent = "Bridge status currently unavailable on the backend.";
    return;
  }
  const d = result.data || {};
  el.className = "";
  el.innerHTML = `<table>
    <tr><th>Bridge state</th><td>${badge(d.bridge_process_state || "UNKNOWN")}</td></tr>
    <tr><th>Loopback only</th><td>${String(d.loopback_only)}</td></tr>
    <tr><th>Resolved symbol</th><td>${esc(d.resolved_symbol || "—")}</td></tr>
    <tr><th>Broker</th><td>${esc(d.broker || "—")}</td></tr>
    <tr><th>Server</th><td>${esc(d.server || "—")}</td></tr>
    <tr><th>Last error</th><td class="stale-mark">${esc(d.last_error || "none")}</td></tr>
  </table>`;
}

// --- loop ------------------------------------------------------------------

async function refresh() {
  $("lastUpdated").textContent = "updating…";
  try {
    await Promise.allSettled([
      loadSystem(),
      loadAnalysis(),
      loadContext(),
      loadTransport(),
      loadAndDrawChart(),
    ]);
  } catch (err) {
    setHealth("bad", "error");
    console.warn("ASTRA frontend refresh error:", err);
  }
  $("lastUpdated").textContent = "updated " + fmtTime(Math.floor(Date.now() / 1000));
}

// guard: production build must never talk to localhost unless explicitly
// configured this way (config.example.js is the default placeholder).
if (!BACKEND) {
  setHealth("bad", "backend base URL not configured");
  $("sysMode").textContent = "UNCONFIGURED";
}

document.addEventListener("DOMContentLoaded", () => {
  resetPanel();
  initTimeframeSelect();
  refresh();
  setInterval(refresh, REFRESH_MS);
});