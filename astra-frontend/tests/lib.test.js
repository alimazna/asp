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

/* ---- analytical helpers added for the forecast / trade-plan pages ---- */

test("biasDirection maps only recognised backend biases", () => {
  assert.strictEqual(lib.biasDirection("UP"), "UP");
  assert.strictEqual(lib.biasDirection("DOWN"), "DOWN");
  assert.strictEqual(lib.biasDirection("RANGE"), "RANGE");
  assert.strictEqual(lib.biasDirection("UNKNOWN"), "UNKNOWN");
  assert.strictEqual(lib.biasDirection(""), "UNKNOWN");
  assert.strictEqual(lib.biasDirection(null), "UNKNOWN");
  assert.strictEqual(lib.biasDirection("SIDEWAYS"), "UNKNOWN");
});

test("closedCandleInfo: a candle is closed only after a full duration", () => {
  const open = 1792000000;
  const dur = lib.TF_SECONDS.H4;
  // 1 second before the next bar opens -> still the forming candle.
  const forming = lib.closedCandleInfo(open, "H4", (open + dur - 1) * 1000);
  assert.strictEqual(forming.closed, false);
  assert.strictEqual(forming.overdueSec, 0);
  assert.strictEqual(forming.overdueLabel, "in time");
  // Exactly at the boundary -> closed.
  const closed = lib.closedCandleInfo(open, "H4", (open + dur) * 1000);
  assert.strictEqual(closed.closed, true);
  assert.strictEqual(closed.nextOpenUtc, open + dur);
});

test("closedCandleInfo reports staleness as an overdue duration", () => {
  const open = 1792000000;
  const info = lib.closedCandleInfo(open, "M15", (open + 900 + 7 * 900) * 1000);
  assert.strictEqual(info.overdueSec, 7 * 900);
  assert.match(info.overdueLabel, /overdue/);
});

test("closedCandleInfo is null for unusable input", () => {
  assert.strictEqual(lib.closedCandleInfo(0, "M15", Date.now()), null);
  assert.strictEqual(lib.closedCandleInfo(null, "M15", Date.now()), null);
  assert.strictEqual(lib.closedCandleInfo(1792000000, "M7", Date.now()), null);
});

test("outcomeTally never invents a win rate without decided outcomes", () => {
  const empty = lib.outcomeTally([]);
  assert.strictEqual(empty.winRate, null);
  assert.strictEqual(empty.decided, 0);

  const undecided = lib.outcomeTally([
    { outcome_class: "UNKNOWN", realized_r: null }]);
  assert.strictEqual(undecided.winRate, null);

  const mixed = lib.outcomeTally([
    { outcome_class: "WIN", realized_r: 2 },
    { outcome_class: "LOSS", realized_r: -1 },
    { outcome_class: "BREAKEVEN", realized_r: 0 },
    { outcome_class: "WIN", realized_r: 3 },
  ]);
  assert.strictEqual(mixed.wins, 2);
  assert.strictEqual(mixed.losses, 1);
  assert.strictEqual(mixed.breakeven, 1);
  assert.strictEqual(mixed.decided, 4);
  // Breakeven is in the denominator but never the numerator.
  assert.strictEqual(mixed.winRate, 0.5);
  assert.strictEqual(mixed.avgR, 1);
});

test("outcomeClass maps losses to bad and unknown to neutral", () => {
  assert.strictEqual(lib.outcomeClass("WIN"), "ok");
  assert.strictEqual(lib.outcomeClass("LOSS"), "bad");
  assert.strictEqual(lib.outcomeClass("BREAKEVEN"), "neutral");
  assert.strictEqual(lib.outcomeClass("WHATEVER"), "neutral");
  assert.strictEqual(lib.outcomeLabel("win"), "WIN");
});

test("partitionPositions separates open from finished", () => {
  const parts = lib.partitionPositions([
    { position_id: "a", state: "OPEN" },
    { position_id: "b", state: "CLOSED_TARGET" },
    { position_id: "c", state: "CLOSED_STOP" },
  ]);
  assert.strictEqual(parts.open.length, 1);
  assert.strictEqual(parts.closed.length, 2);
  assert.deepStrictEqual(lib.partitionPositions(null),
    { open: [], closed: [] });
});

test("candleBatchIntegrity accepts a real timeframe-aligned series", () => {
  const m15 = 900;
  const base = 1790000000 - (1790000000 % m15);
  const bars = [0, 1, 2, 3].map((i) => ({ time: base + i * m15 }));
  const r = lib.candleBatchIntegrity(bars, "M15");
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.misaligned, 0);
  assert.strictEqual(r.minGapSec, m15);
});

test("candleBatchIntegrity tolerates sparse data but flags misaligned bars", () => {
  const m15 = 900;
  const base = 1790000000 - (1790000000 % m15);
  // A wide gap is legitimate (missing bars), so it stays trusted.
  const gappy = lib.candleBatchIntegrity(
    [{ time: base }, { time: base + 8 * m15 }], "M15");
  assert.strictEqual(gappy.ok, true);
  // Times that are not 15-minute aligned, or only seconds apart, are not a
  // real M15 feed. These are the exact shapes seen in the live M15 store.
  const synthetic = lib.candleBatchIntegrity([
    { time: 1792065000 },
    { time: 1792068271 },
    { time: 1792068276 },
    { time: 1792068300 },
  ], "M15");
  assert.strictEqual(synthetic.ok, false);
  assert.strictEqual(synthetic.misaligned, 3);
  assert.ok(synthetic.minGapSec < m15);
});

test("candleBatchIntegrity ignores prices and handles empty input", () => {
  // Non-integer prices must not be rejected: they are real for other symbols.
  const m1 = 60;
  const base = 1790000000 - (1790000000 % m1);
  const r = lib.candleBatchIntegrity(
    [{ time: base, close: 1901.37 }, { time: base + m1, close: 1902.5 }], "M1");
  assert.strictEqual(r.ok, true);
  assert.deepStrictEqual(lib.candleBatchIntegrity([], "M15"),
    { ok: true, checked: 0, misaligned: 0, minGapSec: null });
  assert.strictEqual(lib.candleBatchIntegrity(null, "NOPE").ok, true);
});

