/**
 * CandleChart — a lightweight canvas candlestick renderer.
 * No WebGL, no chart library. Handles pan (drag / one-finger), zoom (wheel /
 * pinch), crosshair with OHLC readout, last-price dashed line and axes.
 * Renders only CLOSED bars served by the API — it never draws a forming bar.
 */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { Candle } from "api/types";
import { formatUtcShort, UNAVAILABLE } from "api/honors";

export interface CandleChartProps {
  bars: Candle[];
  symbol?: string;
  timeframe?: string;
  /** Called on hover/pan with the candle under the crosshair (or null). */
  onHoverCandle?: (c: Candle | null) => void;
  /** CSS color tokens, read from the theme at draw time by default. */
  theme?: "dark" | "light";
}

const PAD_R = 66; // price axis width
const PAD_B = 26; // time axis height
const PAD_T = 8;
const PAD_L = 8;
const MIN_VISIBLE = 8;

function cssVar(name: string, fallback: string): string {
  if (typeof window === "undefined") return fallback;
  const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return v.length > 0 ? v : fallback;
}

export function CandleChart({ bars, symbol, timeframe, onHoverCandle, theme: _theme }: CandleChartProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rafRef = useRef<number>(0);

  // view window: index of first visible bar + number of visible bars
  const [view, setView] = useState<{ start: number; count: number } | null>(null);
  const [hover, setHover] = useState<{ x: number; y: number } | null>(null);
  const geomRef = useRef<{ left: number; width: number; barW: number } | null>(null);

  const barsRef = useRef(bars);
  barsRef.current = bars;
  const viewRef = useRef(view);
  viewRef.current = view;

  // Reset view when the dataset genuinely changes (new timeframe / lifecycle).
  const resetOn = useMemo(() => `${bars.length}:${bars[0]?.time ?? 0}:${bars[bars.length - 1]?.time ?? 0}`, [bars]);
  useEffect(() => {
    setView((prev) => {
      const n = bars.length;
      if (n === 0) return null;
      if (prev === null) {
        return { start: Math.max(0, n - 120), count: Math.min(120, n) };
      }
      // Keep the window if size is unchanged, otherwise clamp.
      if (prev.start + prev.count <= n) return prev;
      return { start: Math.max(0, n - prev.count), count: Math.min(prev.count, n) };
    });
  }, [resetOn, bars.length]);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    const host = hostRef.current;
    if (canvas === null || host === null) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = host.clientWidth;
    const h = host.clientHeight;
    if (w === 0 || h === 0) return;
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
      canvas.width = Math.floor(w * dpr);
      canvas.height = Math.floor(h * dpr);
    }
    const ctx = canvas.getContext("2d");
    if (ctx === null) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    const plotW = w - PAD_R - PAD_L;
    const plotH = h - PAD_T - PAD_B;
    if (plotW < 40 || plotH < 40) return;

    const textMut = cssVar("--text-muted", "#64748b");
    const gridCol = cssVar("--border-subtle", "rgba(159,176,198,0.14)");
    const upCol = cssVar("--up", "#3fa46b");
    const downCol = cssVar("--down", "#d37166");
    const accent = cssVar("--accent-strong", "#6d9be0");

    const all = barsRef.current;
    const vw = viewRef.current;
    if (all.length === 0 || vw === null) return;
    const start = Math.max(0, Math.min(vw.start, all.length - MIN_VISIBLE));
    const count = Math.max(MIN_VISIBLE, Math.min(vw.count, all.length - start));
    const slice = all.slice(start, start + count);

    let lo = Infinity;
    let hi = -Infinity;
    for (const b of slice) {
      if (b.low < lo) lo = b.low;
      if (b.high > hi) hi = b.high;
    }
    if (!Number.isFinite(lo) || !Number.isFinite(hi)) return;
    const pad = (hi - lo) * 0.06 || Math.abs(hi) * 0.001 || 1;
    lo -= pad;
    hi += pad;
    const yOf = (p: number) => PAD_T + ((hi - p) / (hi - lo)) * plotH;

    const barW = plotW / count;
    const bodyW = Math.max(1, Math.min(18, barW * 0.62));
    geomRef.current = { left: PAD_L, width: plotW, barW };

    // vertical time grid (≈6 guides)
    ctx.strokeStyle = gridCol;
    ctx.fillStyle = textMut;
    ctx.lineWidth = 1;
    ctx.font = "10px " + cssVar("--font-mono", "monospace");
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
    const guides = 6;
    for (let i = 0; i <= guides; i++) {
      const gi = Math.round((i / guides) * (slice.length - 1));
      const x = PAD_L + gi * barW + barW / 2;
      ctx.beginPath();
      ctx.moveTo(x, PAD_T);
      ctx.lineTo(x, PAD_T + plotH);
      ctx.stroke();
      const label = formatUtcShort(slice[gi].time);
      ctx.fillText(label, Math.min(x + 3, w - PAD_R - 54), PAD_T + plotH + 5);
    }

    // horizontal price grid
    ctx.textAlign = "left";
    for (let i = 0; i <= 4; i++) {
      const p = lo + ((hi - lo) * i) / 4;
      const y = yOf(p);
      ctx.strokeStyle = gridCol;
      ctx.beginPath();
      ctx.moveTo(PAD_L, y);
      ctx.lineTo(PAD_L + plotW, y);
      ctx.stroke();
      ctx.fillStyle = textMut;
      ctx.fillText(p.toFixed(2), PAD_L + plotW + 6, y - 5);
    }

    // candles
    const halfWick = Math.max(1, Math.round(bodyW / 2));
    for (let i = 0; i < slice.length; i++) {
      const b = slice[i];
      const cx = PAD_L + i * barW + barW / 2;
      const up = b.close >= b.open;
      const col = up ? upCol : downCol;
      ctx.strokeStyle = col;
      ctx.fillStyle = col;
      // wick
      ctx.beginPath();
      ctx.moveTo(cx, yOf(b.high));
      ctx.lineTo(cx, yOf(b.low));
      ctx.stroke();
      // body
      const yOpen = yOf(b.open);
      const yClose = yOf(b.close);
      const top = Math.min(yOpen, yClose);
      const h = Math.max(1, Math.abs(yClose - yOpen));
      if (bodyW <= 2.4) {
        ctx.fillRect(Math.round(cx - halfWick), top, Math.max(1, halfWick * 2), h);
      } else {
        ctx.fillRect(cx - halfWick, top, bodyW, h);
      }
    }

    // last price line (dashed) from the newest CLOSED bar in the series
    const last = all[all.length - 1];
    if (last) {
      const y = yOf(last.close);
      ctx.save();
      ctx.strokeStyle = accent;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(PAD_L, y);
      ctx.lineTo(PAD_L + plotW, y);
      ctx.stroke();
      ctx.restore();
      ctx.fillStyle = accent;
      ctx.fillRect(PAD_L + plotW + 2, y - 8, PAD_R - 6, 16);
      ctx.fillStyle = "#0b1424";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.font = "600 10px " + cssVar("--font-mono", "monospace");
      ctx.fillText(last.close.toFixed(2), PAD_L + plotW + 2 + (PAD_R - 8) / 2, y);
    }

    // crosshair (mouse hover or touch press)
    if (hover !== null) {
      const hx = Math.max(PAD_L, Math.min(hover.x, PAD_L + plotW));
      const hy = Math.max(PAD_T, Math.min(hover.y, PAD_T + plotH));
      ctx.save();
      ctx.strokeStyle = cssVar("--text-faint", "#45536b");
      ctx.setLineDash([3, 3]);
      ctx.beginPath();
      ctx.moveTo(hx, PAD_T);
      ctx.lineTo(hx, PAD_T + plotH);
      ctx.moveTo(PAD_L, hy);
      ctx.lineTo(PAD_L + plotW, hy);
      ctx.stroke();
      // highlighted price at crosshair
      const price = hi - ((hy - PAD_T) / plotH) * (hi - lo);
      ctx.fillStyle = cssVar("--bg-raised", "#13213a");
      ctx.fillRect(PAD_L + plotW + 2, hy - 8, PAD_R - 6, 16);
      ctx.fillStyle = cssVar("--text-primary", "#e8edf4");
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(price.toFixed(2), PAD_L + plotW + 2 + (PAD_R - 8) / 2, hy);
      ctx.restore();
    }
  }, [hover]);

  // Redraw on any state change.
  useEffect(() => {
    rafRef.current = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(rafRef.current);
  }, [draw, bars, view]);

  // Resize observer.
  useEffect(() => {
    const host = hostRef.current;
    if (host === null) return;
    const ro = new ResizeObserver(() => {
      rafRef.current = requestAnimationFrame(draw);
    });
    ro.observe(host);
    return () => ro.disconnect();
  }, [draw]);

  // Restore theme-triggered repaint.
  useEffect(() => {
    rafRef.current = requestAnimationFrame(draw);
  });

  /* ---------- interactions ---------- */

  const pointerToPlot = (e: { clientX: number; clientY: number }) => {
    const host = hostRef.current;
    if (host === null) return null;
    const r = host.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  };

  const candleAtX = (x: number): Candle | null => {
    const g = geomRef.current;
    const vw = viewRef.current;
    if (g === null || vw === null) return null;
    const idx = Math.floor((x - g.left) / g.barW);
    const all = barsRef.current;
    const i = vw.start + idx;
    if (i < 0 || i >= all.length) return null;
    return all[i] ?? null;
  };

  const onPointerMove = (e: React.PointerEvent) => {
    const pt = pointerToPlot(e);
    if (pt === null) return;
    setHover(pt);
    onHoverCandle?.(candleAtX(pt.x));
  };

  const onPointerLeave = () => {
    setHover(null);
    onHoverCandle?.(null);
  };

  // Wheel zoom
  useEffect(() => {
    const host = hostRef.current;
    if (host === null) return;
    const onWheel = (e: WheelEvent) => {
      const vw = viewRef.current;
      if (vw === null) return;
      e.preventDefault();
      const all = barsRef.current;
      const factor = e.deltaY > 0 ? 1.18 : 0.85;
      let count = Math.round(vw.count * factor);
      count = Math.max(MIN_VISIBLE, Math.min(all.length, count));
      let start = vw.start;
      // zoom towards the right edge (latest bars stay visible)
      const end = vw.start + vw.count;
      const newEnd = Math.min(all.length, end);
      start = Math.max(0, newEnd - count);
      setView({ start, count });
    };
    host.addEventListener("wheel", onWheel, { passive: false });
    return () => host.removeEventListener("wheel", onWheel);
  }, []);

  // Drag to pan (pointer), pinch to zoom (two pointers).
  const dragRef = useRef<{ x: number; start: number; count: number } | null>(null);
  const pinchRef = useRef<{ dist: number; count: number } | null>(null);
  const activePointers = useRef(new Map<number, { x: number; y: number }>());

  const onPointerDown = (e: React.PointerEvent) => {
    const host = hostRef.current;
    if (host === null) return;
    (e.target as Element).setPointerCapture?.(e.pointerId);
    activePointers.current.set(e.pointerId, { x: e.clientX, y: e.clientY });
    const vw = viewRef.current;
    if (vw === null) return;
    if (activePointers.current.size === 1) {
      dragRef.current = { x: e.clientX, start: vw.start, count: vw.count };
    } else if (activePointers.current.size === 2) {
      const [a, b] = [...activePointers.current.values()];
      dragRef.current = null;
      pinchRef.current = { dist: Math.abs(a.x - b.x), count: vw.count };
    }
  };

  const onPointerUp = (e: React.PointerEvent) => {
    activePointers.current.delete(e.pointerId);
    if (activePointers.current.size < 2) pinchRef.current = null;
    if (activePointers.current.size === 0) dragRef.current = null;
  };

  const onPointerDragMove = (e: React.PointerEvent) => {
    if (activePointers.current.has(e.pointerId)) {
      activePointers.current.set(e.pointerId, { x: e.clientX, y: e.clientY });
    }
    // pinch zoom
    if (pinchRef.current !== null && activePointers.current.size === 2) {
      const [a, b] = [...activePointers.current.values()];
      const dist = Math.abs(a.x - b.x);
      const all = barsRef.current;
      const cv = viewRef.current;
      if (cv === null) return;
      const scale = pinchRef.current.count * (pinchRef.current.dist / Math.max(1, dist));
      const count = Math.max(MIN_VISIBLE, Math.min(all.length, Math.round(scale)));
      const end = cv.start + cv.count;
      setView({ start: Math.max(0, Math.min(end - count, all.length - count)), count });
      return;
    }
    // pan
    const drag = dragRef.current;
    const vw = viewRef.current;
    if (drag === null || vw === null) return;
    const g = geomRef.current;
    if (g === null) return;
    const all = barsRef.current;
    const barW = g.width / vw.count;
    const dxBars = (e.clientX - drag.x) / barW;
    let start = Math.round(drag.start + dxBars);
    start = Math.max(0, Math.min(start, all.length - vw.count));
    setView({ start, count: vw.count });
  };

  return (
    <div
      className="chart-host"
      ref={hostRef}
      onPointerMove={(e) => {
        onPointerDragMove(e);
        onPointerMove(e);
      }}
      onPointerDown={onPointerDown}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onPointerLeave={() => {
        onPointerUp({ pointerId: -1 } as unknown as React.PointerEvent);
        onPointerLeave();
      }}
      role="img"
      aria-label={`Candlestick chart ${symbol ?? ""} ${timeframe ?? ""} — closed bars only`}
    >
      <canvas ref={canvasRef} />
    </div>
  );
}

/** OHLC readout for the selected/hovered candle. */
export function CandleReadout({ candle }: { candle: Candle | null }) {
  if (candle === null) {
    return (
      <span className="muted" style={{ fontSize: "var(--fs-xs)" }}>
        Hover or touch a candle to read its OHLC. Closed bars only.
      </span>
    );
  }
  const up = candle.close >= candle.open;
  const col = up ? "var(--up)" : "var(--down)";
  const chip = (label: string, v: number) => (
    <span>
      {label} <strong style={{ color: col }} className="num">{v.toFixed(2)}</strong>
    </span>
  );
  return (
    <div className="row wrap" style={{ gap: 14, fontSize: "var(--fs-xs)", color: "var(--text-secondary)" }}>
      <span className="num muted">{formatUtcShort(candle.time)} UTC</span>
      {chip("O", candle.open)}
      {chip("H", candle.high)}
      {chip("L", candle.low)}
      {chip("C", candle.close)}
      {candle.tick_volume != null ? (
        <span className="muted num">vol {candle.tick_volume}</span>
      ) : null}
      {candle.time ? null : <span>{UNAVAILABLE}</span>}
    </div>
  );
}
