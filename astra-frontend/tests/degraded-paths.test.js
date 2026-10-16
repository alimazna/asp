/* Degraded-path integration tests for the ASTRA frontend.

   These render real views from assets/app.js in a minimal DOM harness against a
   stubbed fetch, and assert the *honesty* contract: when the backend is down,
   returns no data, or returns data that is not a real MT5 series, the UI says so
   and never fabricates a value.

   Run from the repo root:  node --test astra-frontend/tests/
   Everything here is test-only; no fixture reaches production. */

"use strict";

const test = require("node:test");
const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const harness = require(path.join(__dirname, "dom-harness.js"));

const ROOT = path.join(__dirname, "..");
const LIB_SRC = fs.readFileSync(path.join(ROOT, "assets", "lib.js"), "utf8");
const APP_SRC = fs.readFileSync(path.join(ROOT, "assets", "app.js"), "utf8");

/* Boot lib.js + app.js inside a sandbox backed by the DOM harness. Returns the
   harness, the document, and the exposed test surface. */
function boot(opts) {
  const h = harness.install({ routes: (opts && opts.routes) || {} }, opts);
  const sandbox = h.sandbox;
  sandbox.globalThis = sandbox;
  sandbox.self = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(LIB_SRC, sandbox, { filename: "lib.js" });
  // lib.js attaches to its global (globalThis); app.js reads window.ASTRA, so
  // bridge the two. In a real page they are the same object.
  h.window.ASTRA = sandbox.ASTRA;
  vm.runInContext(APP_SRC, sandbox, { filename: "app.js" });
  const surface = h.window.__ASTRA_TEST__;
  assert.ok(surface && surface.views, "app.js must expose its views for testing");
  return { h: h, doc: h.document, surface: surface };
}

/* Render one view into a fresh mount and wait for its Promise.allSettled chain
   to settle (a couple of macrotasks is enough for the stubbed fetch). */
function render(env, viewName) {
  const mount = env.doc.createElement("div");
  env.doc.body.appendChild(mount);
  env.surface.views[viewName](mount);
  return new Promise((resolve) => setImmediate(() => setImmediate(resolve)))
    .then(() => mount);
}

const textOf = (node) => node.textContent;
const hasText = (node, needle) => textOf(node).indexOf(needle) !== -1;

/* True when any element renders a bare percentage as a value. The honest
   "no decided outcome yet — not 0%" note is prose, not a metric value, so it is
   excluded by requiring the element's own text to be exactly the percentage. */
function hasBarePercentValue(node) {
  let found = false;
  node.walk((el) => {
    if (el.children.length === 0 && /^\s*\d+(\.\d+)?%\s*$/.test(el.textContent)) found = true;
  });
  return found;
}

/* ------------------------------------------------------------ dashboard */

test("dashboard: backend unreachable reports it and fabricates nothing", async () => {
  const env = boot({ routes: {} }); // every fetch rejects
  const mount = await render(env, "dashboard");
  const text = textOf(mount);
  assert.ok(hasText(mount, "The backend is unreachable from this browser"),
    "unreachable banner must be shown");
  assert.ok(hasText(mount, "no data is being fabricated"),
    "the banner must state that nothing is fabricated");
  assert.ok(hasText(mount, "Coverage unavailable"),
    "coverage must fall back to an explicit unavailable block");
  assert.ok(!hasBarePercentValue(mount), "no fabricated success metric");
  assert.ok(hasText(mount, "no decided outcome yet — not 0%"),
    "an empty outcome set must read as undecided, not 0%");
});

test("dashboard: degraded health is surfaced with its reasons", async () => {
  const env = boot({
    routes: {
      "/api/v1/health": {
        status: 200,
        json: {
          api: "v1",
          data: {
            aggregate: "DEGRADED",
            decision_grade_data: false,
            unknown_is_not_safe: true,
            degraded_reasons: ["M15 not decision-grade", "H4 stream missing"],
          },
        },
      },
      "/api/v1/system/state": { status: 200, json: { api: "v1", data: { ready: false, mode: "DEGRADED" } } },
      "/api/v1/timeframes": { status: 200, json: { api: "v1", data: [] } },
      "/api/v1/analysis/latest": { status: 200, json: { api: "v1", data: { context: {} } } },
      "/api/v1/bridge/status": { status: 200, json: { api: "v1", data: {} } },
      "/api/v1/shadow/outcomes": { status: 200, json: { api: "v1", data: [] } },
      "/api/v1/candles": { status: 200, json: { api: "v1", data: { bars: [] } } },
    },
  });
  const mount = await render(env, "dashboard");
  assert.ok(hasText(mount, "Backend aggregate is DEGRADED"),
    "the degraded banner must name the aggregate");
  assert.ok(hasText(mount, "decision-grade data is NOT available"),
    "the banner must state that decision-grade data is unavailable");
  assert.ok(hasText(mount, "H4 stream missing"),
    "backend-reported degraded reasons must be listed");
  assert.ok(hasText(mount, "no decided outcome yet"),
    "an empty outcome set must read as 'no decided outcome yet', not 0%");
});

test("dashboard: M15 rows that are not MT5-aligned are excluded from coverage", async () => {
  const env = boot({
    routes: {
      "/api/v1/health": { status: 200, json: { api: "v1", data: { aggregate: "OK", decision_grade_data: true } } },
      "/api/v1/system/state": { status: 200, json: { api: "v1", data: { ready: true, mode: "OK" } } },
      "/api/v1/timeframes": {
        status: 200,
        json: { api: "v1", data: [
          { timeframe: "M15", observed: true, decision_grade: true, quality: { state: "VALID" } },
          { timeframe: "H4", observed: true, decision_grade: true, quality: { state: "VALID" } },
        ] },
      },
      "/api/v1/analysis/latest": { status: 200, json: { api: "v1", data: { context: {} } } },
      "/api/v1/bridge/status": { status: 200, json: { api: "v1", data: {} } },
      "/api/v1/shadow/outcomes": { status: 200, json: { api: "v1", data: [] } },
      "/api/v1/candles": {
        status: 200,
        json: { api: "v1", data: { bars: [
          { time: 1792065000, open: 1, high: 2, low: 0.5, close: 1.5 },
          { time: 1792068271, open: 1, high: 2, low: 0.5, close: 1.5 },
        ] } },
      },
    },
  });
  const mount = await render(env, "dashboard");
  assert.ok(hasText(mount, "M15 excluded: non-MT5 rows"),
    "M15 must be excluded from the observed count when its rows are not aligned");
});

/* ---------------------------------------------------------------- chart */

test("chart: non-aligned candles are refused, not plotted as real", async () => {
  const env = boot({
    routes: {
      "/api/v1/candles": {
        status: 200,
        json: { api: "v1", data: { bars: [
          { time: 1792065000, open: 1, high: 2, low: 0.5, close: 1.5 },
          { time: 1792068276, open: 1, high: 2, low: 0.5, close: 1.5 },
          { time: 1792068300, open: 1, high: 2, low: 0.5, close: 1.5 },
        ] } },
      },
    },
  });
  const mount = await render(env, "chart");
  assert.ok(hasText(mount, "not a real MT5 series"),
    "the chart must state the series is not a real MT5 series");
  assert.ok(hasText(mount, "nothing is plotted"),
    "the chart must state nothing is plotted");
});

test("chart: an empty backend response draws nothing rather than fabricating", async () => {
  const env = boot({
    routes: { "/api/v1/candles": { status: 200, json: { api: "v1", data: { bars: [] } } } },
  });
  const mount = await render(env, "chart");
  assert.ok(hasText(mount, "returned no closed bars"),
    "an empty series must be reported honestly");
  assert.ok(hasText(mount, "rather than fabricating values"),
    "the empty state must say values are not fabricated");
});

test("chart: a failed candle request is shown as unavailable", async () => {
  const env = boot({ routes: {} });
  const mount = await render(env, "chart");
  assert.ok(hasText(mount, "Candle request failed"),
    "a fetch failure must be surfaced");
  assert.ok(hasText(mount, "unavailable"),
    "the badge must read unavailable");
});

/* ------------------------------------------------------------- forecast */

test("forecast: no analysis snapshot means no forecast is shown", async () => {
  const env = boot({ routes: {} });
  const mount = await render(env, "forecast");
  assert.ok(hasText(mount, "no forecast can be shown"),
    "a missing analysis snapshot must block the forecast");
});

test("forecast: unknown H4 bias reads as not available, never as a direction", async () => {
  const env = boot({
    routes: {
      "/api/v1/analysis/latest": {
        status: 200,
        json: { api: "v1", data: { context: { h4_bias: "UNKNOWN", m15_trigger: "NONE" }, timestamp: null } },
      },
      "/api/v1/timeframes": { status: 200, json: { api: "v1", data: [] } },
      "/api/v1/candles": { status: 200, json: { api: "v1", data: { bars: [] } } },
    },
  });
  const mount = await render(env, "forecast");
  assert.ok(hasText(mount, "H4 lean: not available"),
    "an unknown bias must read as not available");
  assert.ok(!hasText(mount, "H4 lean: UP"), "no direction may be invented");
  assert.ok(!hasText(mount, "H4 lean: DOWN"), "no direction may be invented");
  assert.ok(hasText(mount, "The backend holds no H4 bars"),
    "the missing H4 series must be reported");
});

test("forecast: M15 with non-aligned rows is withheld, not shown as a real bar", async () => {
  const env = boot({
    routes: {
      "/api/v1/analysis/latest": {
        status: 200,
        json: { api: "v1", data: { context: { h4_bias: "UP", m15_trigger: "LONG" }, timestamp: null } },
      },
      "/api/v1/timeframes": {
        status: 200,
        json: { api: "v1", data: [
          { timeframe: "M15", has_closed_bar: true, last_successful_update: 1792068300, decision_grade: true, quality: { state: "VALID" } },
        ] },
      },
      "/api/v1/candles": (function () {
        // H4 request returns real bars; the M15 request returns non-aligned rows.
        return { status: 200, json: (url) => ({
          api: "v1",
          data: { bars: url.indexOf("tf=M15") !== -1
            ? [{ time: 1792068300, open: 1, high: 2, low: 0.5, close: 1.5 },
               { time: 1792068276, open: 1, high: 2, low: 0.5, close: 1.5 }]
            : [{ time: 1792022400, open: 1, high: 2, low: 0.5, close: 1.5 }] },
        }) };
      })(),
    },
  });
  const mount = await render(env, "forecast");
  assert.ok(hasText(mount, "M15 is reported as having no trustworthy closed bar"),
    "an untrusted M15 watermark must be withheld with an explanation");
  assert.ok(hasText(mount, "not MT5-aligned"),
    "the reason must name the MT5-alignment failure");
});

/* ----------------------------------------------------------------- plan */

test("plan: no outcomes reads as undecided, not as a 0% win rate", async () => {
  const env = boot({
    routes: {
      "/api/v1/analysis/latest": { status: 200, json: { api: "v1", data: { context: {}, signal: {} } } },
      "/api/v1/shadow/outcomes": { status: 200, json: { api: "v1", data: [] } },
      "/api/v1/shadow/positions": { status: 200, json: { api: "v1", data: [] } },
    },
  });
  const mount = await render(env, "plan");
  assert.ok(!hasBarePercentValue(mount),
    "an empty outcome set must never render a bare percentage");
  assert.ok(hasText(mount, "no decided outcome yet — not 0%"),
    "the win-rate tile must read as undecided");
});

/* --------------------------------------------------------------- system */

test("system: unreachable dependencies are shown as unavailable", async () => {
  const env = boot({ routes: {} });
  const mount = await render(env, "system");
  const text = textOf(mount);
  assert.ok(/unavailable|not available|unreachable/i.test(text),
    "the system view must show unavailable states when the backend is down");
  assert.ok(!/\bOK\b/.test(text.replace(/Shadow-only[^.]*\./g, "")),
    "no OK status may be shown without backend evidence");
});
