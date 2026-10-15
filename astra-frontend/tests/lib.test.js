/* Unit tests for ASTRA frontend pure helpers (astra-frontend/assets/lib.js).
   Run: node --test astra-frontend/tests/  (from the repo root) */

const test = require("node:test");
const assert = require("node:assert");
const path = require("node:path");

const lib = require(path.join(__dirname, "..", "assets", "lib.js"));

test("nine timeframes, coarsest first", () => {
  assert.deepStrictEqual(lib.TIMEFRAMES,
    ["MN1", "W1", "D1", "H4", "H1", "M30", "M15", "M5", "M1"]);
  assert.strictEqual(lib.TIMEFRAMES.length, 9);
});

test("every timeframe has an analytical role", () => {
  lib.TIMEFRAMES.forEach((tf) => {
    assert.ok(lib.TF_ROLE[tf], "role missing for " + tf);
    assert.ok(lib.TF_ROLE[tf].label);
  });
  const inTiers = lib.TF_TIERS.reduce((a, t) => a.concat(t.tfs), []);
  assert.strictEqual(inTiers.length, 9);
  lib.TIMEFRAMES.forEach((tf) => assert.ok(inTiers.includes(tf)));
});

test("esc escapes html metacharacters", () => {
  assert.strictEqual(lib.esc('<a href="x">&'), "&lt;a href=&quot;x&quot;&gt;&amp;");
  assert.strictEqual(lib.esc(null), "");
  assert.strictEqual(lib.esc(0), "0");
});

test("fmtEpochUTC formats seconds and rejects invalid input", () => {
  assert.strictEqual(lib.fmtEpochUTC(0), null);
  assert.strictEqual(lib.fmtEpochUTC(null), null);
  assert.strictEqual(lib.fmtEpochUTC("nope"), null);
  assert.match(lib.fmtEpochUTC(1792068300), /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC$/);
});

test("fmtMillisUTC and fmtAge are null-safe", () => {
  assert.strictEqual(lib.fmtMillisUTC(null), null);
  assert.match(lib.fmtMillisUTC(1792069721152), /UTC$/);
  assert.strictEqual(lib.fmtAge(null), null);
  assert.strictEqual(lib.fmtAge(-5), null);
  assert.strictEqual(lib.fmtAge(30 * 1000), "30s ago");
  assert.strictEqual(lib.fmtAge(5 * 60 * 1000), "5m ago");
});

test("statusClass never maps unknown to ok", () => {
  assert.strictEqual(lib.statusClass("UNKNOWN"), "neutral");
  assert.strictEqual(lib.statusClass(""), "neutral");
  assert.strictEqual(lib.statusClass(null), "neutral");
  assert.strictEqual(lib.statusClass("DEGRADED"), "warn");
  assert.strictEqual(lib.statusClass("STALE"), "warn");
  assert.strictEqual(lib.statusClass("FRESH"), "ok");
  assert.strictEqual(lib.statusClass("OFFLINE"), "bad");
  assert.strictEqual(lib.statusClass("UNAVAILABLE"), "bad");
});

test("isValidBar enforces finite ordered OHLC and a timestamp", () => {
  assert.ok(lib.isValidBar({ time: 1, open: 1, high: 2, low: 0.5, close: 1.5 }));
  assert.ok(!lib.isValidBar({ time: 1, open: 1, high: 0.5, low: 0.5, close: 1.5 }), "high < low");
  assert.ok(!lib.isValidBar({ time: 1, open: 3, high: 2, low: 0.5, close: 1.5 }), "open > high");
  assert.ok(!lib.isValidBar({ time: 0, open: 1, high: 2, low: 0.5, close: 1.5 }), "zero time");
  assert.ok(!lib.isValidBar({ time: 1, open: NaN, high: 2, low: 0.5, close: 1.5 }), "NaN");
  assert.ok(!lib.isValidBar(null));
});

test("partitionBars separates malformed rows and sorts chronologically", () => {
  const bars = [
    { time: 3, open: 1, high: 2, low: 0.5, close: 1.5 },
    { time: 1, open: 1, high: 2, low: 0.5, close: 1.5 },
    { time: 2, open: 9, high: 2, low: 0.5, close: 1.5 },
    { time: 2, open: 1, high: 2, low: 0.5, close: 1.5 },
  ];
  const p = lib.partitionBars(bars);
  assert.strictEqual(p.valid.length, 3);
  assert.strictEqual(p.rejected.length, 1);
  assert.deepStrictEqual(p.valid.map((b) => b.time), [1, 2, 3]);
  assert.deepStrictEqual(lib.partitionBars(null), { valid: [], rejected: [] });
});

test("chartScale maps min to bottom and max to top", () => {
  const bars = [{ low: 100, high: 200 }, { low: 120, high: 180 }];
  const s = lib.chartScale(bars, 100, 10);
  assert.strictEqual(s.min, 100);
  assert.strictEqual(s.max, 200);
  assert.ok(Math.abs(s.y(100) - 90) < 1e-9, "min -> pad + usable");
  assert.ok(Math.abs(s.y(200) - 10) < 1e-9, "max -> pad");
});

test("chartScale handles a flat series without dividing by zero", () => {
  const s = lib.chartScale([{ low: 50, high: 50 }], 100, 10);
  assert.strictEqual(s.range, 1);
  assert.ok(Number.isFinite(s.y(50)));
});

test("gridValues returns n+1 evenly spaced values", () => {
  assert.deepStrictEqual(lib.gridValues(0, 4, 4), [0, 1, 2, 3, 4]);
  assert.deepStrictEqual(lib.gridValues(10, 20, 2), [10, 15, 20]);
});

test("coverageSummary counts observed, graded and fresh", () => {
  const tfs = [
    { observed: true, decision_grade: true, freshness: { is_fresh: true } },
    { observed: true, decision_grade: false, freshness: { is_fresh: false } },
    { observed: false, decision_grade: false, freshness: null },
  ];
  assert.deepStrictEqual(lib.coverageSummary(tfs),
    { total: 3, observed: 2, decisionGrade: 1, fresh: 1 });
  assert.deepStrictEqual(lib.coverageSummary(null),
    { total: 0, observed: 0, decisionGrade: 0, fresh: 0 });
});

test("buildQuery encodes and skips empty values", () => {
  assert.strictEqual(lib.buildQuery({ tf: "M15", limit: 300 }), "?tf=M15&limit=300");
  assert.strictEqual(lib.buildQuery({ tf: "M15", x: null, y: "" }), "?tf=M15");
  assert.strictEqual(lib.buildQuery({}), "");
  assert.strictEqual(lib.buildQuery(null), "");
});

test("unwrap returns data and throws on error envelopes", () => {
  assert.strictEqual(lib.unwrap({ api: "v1", data: { a: 1 } }).a, 1);
  assert.throws(() => lib.unwrap({ error: "true", message: "boom" }), /boom/);
  assert.throws(() => lib.unwrap({ api: "v1" }), /no data/);
  assert.throws(() => lib.unwrap(null), /malformed/);
});
