/* ASTRA frontend pure helpers.
   No DOM access, no network: everything here is deterministic and unit-tested
   under `node --test` (see astra-frontend/tests). Loaded before app.js and
   exposed as window.ASTRA (and module.exports for Node). */

(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.ASTRA = Object.assign(root.ASTRA || {}, api);
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  /* Canonical timeframe order, coarsest first — matches the backend contract. */
  const TIMEFRAMES = ["MN1", "W1", "D1", "H4", "H1", "M30", "M15", "M5", "M1"];

  /* Analytical role per timeframe (project structure, not invented data). */
  const TF_ROLE = {
    MN1: { tier: "context", label: "Higher-level context" },
    W1:  { tier: "context", label: "Higher-level context" },
    D1:  { tier: "context", label: "Higher-level context" },
    H4:  { tier: "structure", label: "Structural authority" },
    H1:  { tier: "intermediate", label: "Intermediate analysis" },
    M30: { tier: "intermediate", label: "Intermediate analysis" },
    M15: { tier: "operational", label: "Primary operational" },
    M5:  { tier: "detail", label: "Lower-timeframe detail" },
    M1:  { tier: "detail", label: "Lower-timeframe detail" },
  };

  const TF_TIERS = [
    { id: "context", title: "Higher-level context", tfs: ["MN1", "W1", "D1"] },
    { id: "structure", title: "Structural authority", tfs: ["H4"] },
    { id: "intermediate", title: "Intermediate analysis", tfs: ["H1", "M30"] },
    { id: "operational", title: "Primary operational / setup", tfs: ["M15"] },
    { id: "detail", title: "Lower-timeframe detail", tfs: ["M5", "M1"] },
  ];

  function esc(value) {
    return String(value === null || value === undefined ? "" : value)
      .replace(/[&<>"']/g, (ch) => ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
      }[ch]));
  }

  /* Format an epoch (seconds) as an explicit UTC stamp, or null when absent. */
  function fmtEpochUTC(seconds) {
    if (seconds === null || seconds === undefined) return null;
    const n = Number(seconds);
    if (!Number.isFinite(n) || n <= 0) return null;
    return new Date(n * 1000).toISOString().replace("T", " ").slice(0, 19) + " UTC";
  }

  /* Format an epoch in milliseconds (backend `*_millis` fields). */
  function fmtMillisUTC(ms) {
    if (ms === null || ms === undefined) return null;
    const n = Number(ms);
    if (!Number.isFinite(n) || n <= 0) return null;
    return new Date(n).toISOString().replace("T", " ").slice(0, 19) + " UTC";
  }

  /* Compact "time ago" for freshness labels. Returns null when unknown. */
  function fmtAge(millis) {
    if (millis === null || millis === undefined) return null;
    const n = Number(millis);
    if (!Number.isFinite(n) || n < 0) return null;
    const s = Math.floor(n / 1000);
    if (s < 60) return s + "s ago";
    const m = Math.floor(s / 60);
    if (m < 60) return m + "m ago";
    const h = Math.floor(m / 60);
    if (h < 48) return h + "h ago";
    return Math.floor(h / 24) + "d ago";
  }

  /* Fixed-precision number; null-safe. */
  function fmtNum(value, digits) {
    if (value === null || value === undefined) return null;
    const n = Number(value);
    if (!Number.isFinite(n)) return null;
    return n.toFixed(digits === undefined ? 2 : digits);
  }

  /* Map a backend state/quality word to a semantic class. Unknown never
     becomes "ok" — that is a safety requirement of this product. */
  function statusClass(state) {
    const s = String(state === null || state === undefined ? "" : state).toLowerCase();
    if (s === "ok" || s === "fresh" || s === "ready" || s === "healthy" ||
        s === "valid" || s === "up" || s === "online" || s === "available" ||
        s === "decision_grade") return "ok";
    if (s === "degraded" || s === "stale" || s === "warn" || s === "warning" ||
        s === "starting" || s === "partial" || s === "uncalibrated") return "warn";
    if (s === "error" || s === "unavailable" || s === "invalid" ||
        s === "offline" || s === "failed" || s === "down" || s === "blocked")
      return "bad";
    return "neutral";
  }

  /* Whether a candle record has finite, ordered OHLC values. */
  function isValidBar(bar) {
    if (!bar || typeof bar !== "object") return false;
    const o = bar.open, h = bar.high, l = bar.low, c = bar.close;
    for (const v of [o, h, l, c]) {
      if (typeof v !== "number" || !Number.isFinite(v)) return false;
    }
    if (h < l) return false;
    if (o > h || o < l) return false;
    if (c > h || c < l) return false;
    if (typeof bar.time !== "number" || !Number.isFinite(bar.time) || bar.time <= 0)
      return false;
    return true;
  }

  /* Split a raw bar array into valid records and rejected ones (kept so the
     UI can report a data-integrity count instead of silently dropping rows). */
  function partitionBars(bars) {
    const valid = [], rejected = [];
    (Array.isArray(bars) ? bars : []).forEach((b) => {
      (isValidBar(b) ? valid : rejected).push(b);
    });
    valid.sort((a, b) => a.time - b.time);
    return { valid, rejected };
  }

  /* Linear price scale for the candle chart. Pure geometry, no canvas. */
  function chartScale(bars, height, pad) {
    const p = pad === undefined ? 12 : pad;
    const usable = Math.max(height - 2 * p, 1);
    const lows = bars.map((b) => b.low);
    const highs = bars.map((b) => b.high);
    const min = Math.min.apply(null, lows);
    const max = Math.max.apply(null, highs);
    const range = (max - min) || 1;
    return {
      min: min,
      max: max,
      range: range,
      usable: usable,
      pad: p,
      y: function (value) { return p + usable * (1 - (value - min) / range); },
    };
  }

  /* Evenly spaced gridline values between min and max. */
  function gridValues(min, max, count) {
    const n = count === undefined ? 4 : count;
    const out = [];
    for (let i = 0; i <= n; i++) out.push(min + ((max - min) * i) / n);
    return out;
  }

  /* Coverage rollup for the timeframe array from /api/v1/timeframes. */
  function coverageSummary(timeframes) {
    const list = Array.isArray(timeframes) ? timeframes : [];
    let observed = 0, graded = 0, fresh = 0;
    list.forEach((t) => {
      if (t && t.observed === true) observed++;
      if (t && t.decision_grade === true) graded++;
      if (t && t.freshness && t.freshness.is_fresh === true) fresh++;
    });
    return { total: list.length, observed: observed, decisionGrade: graded, fresh: fresh };
  }

  /* Encode a query object, skipping null/undefined values. */
  function buildQuery(params) {
    const parts = [];
    Object.keys(params || {}).forEach((k) => {
      const v = params[k];
      if (v === null || v === undefined || v === "") return;
      parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
    });
    return parts.length ? "?" + parts.join("&") : "";
  }

  /* Unwrap the v1 envelope; throws on backend error envelopes. */
  function unwrap(payload) {
    if (!payload || typeof payload !== "object") {
      throw new Error("malformed response");
    }
    if (payload.error === true || payload.error === "true") {
      throw new Error(payload.message || payload.code || "backend error");
    }
    if (!("data" in payload)) throw new Error("response has no data field");
    return payload.data;
  }

  return {
    TIMEFRAMES: TIMEFRAMES,
    TF_ROLE: TF_ROLE,
    TF_TIERS: TF_TIERS,
    esc: esc,
    fmtEpochUTC: fmtEpochUTC,
    fmtMillisUTC: fmtMillisUTC,
    fmtAge: fmtAge,
    fmtNum: fmtNum,
    statusClass: statusClass,
    isValidBar: isValidBar,
    partitionBars: partitionBars,
    chartScale: chartScale,
    gridValues: gridValues,
    coverageSummary: coverageSummary,
    buildQuery: buildQuery,
    unwrap: unwrap,
  };
});
