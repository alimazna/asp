import { useEffect, useState } from "react";
import { CandleChart, CandleReadout } from "charts/CandleChart";
import { Chip } from "components/Chip";
import { ErrorState, StateBlock } from "components/States";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import { getCandles } from "api/endpoints";
import { TIMEFRAMES, type Candle, type CandleSeries, type Timeframe } from "api/types";
import { formatUtcTime } from "api/honors";

const LIMITS = [120, 250, 500, 1000] as const;

export default function ChartPage() {
  const { pollScale, connection } = useApp();
  const [tf, setTf] = useState<Timeframe>("M15");
  const [limit, setLimit] = useState<number>(500);
  const [hoverCandle, setHoverCandle] = useState<Candle | null>(null);

  const chart = useApi<CandleSeries>({
    fetcher: (s) => getCandles(tf, limit, { signal: s }),
    intervalMs: POLL.candles * pollScale,
    keepPreviousOnError: false,
    // Refetch when the selected timeframe or series length changes. This must be
    // explicit: the hook reads the fetcher through a ref, so a new inline arrow
    // no longer triggers a fetch on its own.
    deps: [tf, limit],
  });

  useEffect(() => {
    document.title = `ASTRA — Chart ${tf}`;
  }, [tf]);

  const series = chart.data;
  const fresh =
    (series?.freshness ?? "").toUpperCase() === "FRESH"
      ? "up"
      : (series?.freshness ?? "").toUpperCase() === "STALE"
        ? "warn"
        : "neutral";

  return (
    <>
      <div className="row spread wrap" style={{ gap: 10 }}>
        <div className="seg" role="group" aria-label="Timeframe">
          {TIMEFRAMES.map((t) => (
            <button
              key={t}
              type="button"
              className={t === "H4" ? "seg-structural" : undefined}
              aria-pressed={tf === t}
              onClick={() => setTf(t)}
              title={t === "H4" ? "H4 — structural authority timeframe" : t === "M15" ? "M15 — primary operational timeframe" : undefined}
            >
              {t}
            </button>
          ))}
        </div>
        <div className="row" style={{ gap: 8 }}>
          <div className="seg" role="group" aria-label="Candle count">
            {LIMITS.map((n) => (
              <button key={n} type="button" aria-pressed={limit === n} onClick={() => setLimit(n)}>
                {n}
              </button>
            ))}
          </div>
          {series ? (
            <Chip tone={fresh} title="Backend freshness of the closed-bar series">
              {series.freshness?.toUpperCase() ?? "UNKNOWN"}
            </Chip>
          ) : null}
        </div>
      </div>
      <div className="muted" style={{ fontSize: "var(--fs-2xs)" }}>
        Closed bars only — the forming bar is never displayed. Scroll to zoom, drag to pan, tap for OHLC.
        H4 (★) is the structural-authority timeframe; M15 is the primary operational timeframe.
      </div>

      <section className="card">
        <div className="row spread">
          <h3 className="card-title" style={{ margin: 0 }}>
            XAUUSD · {tf}
          </h3>
          {series ? (
            <span className="muted num" style={{ fontSize: "var(--fs-2xs)" }}>
              {series.count} {series.closed_only === false ? "bars" : "closed bars"} · newest closed bar{" "}
              {formatUtcTime(series.newest_closed_time)}
            </span>
          ) : null}
        </div>
        {chart.loading && series === null ? (
          <div className="skel skel--card" style={{ height: 420 }} />
        ) : chart.error !== null && series === null ? (
          <ErrorState
            title={chart.error}
            detail={chart.errorDetail}
            onRetry={chart.refetch}
          />
        ) : series === null || series.bars.length === 0 ? (
          <StateBlock
            icon="◫"
            title="MT5 DATA NOT AVAILABLE"
            message={
              connection.state === "CONNECTED"
                ? "The backend accepted the request but returned no closed bars for this timeframe. The MT5 terminal may not be running or the symbol is unavailable at the broker — the data will appear once MT5 supplies closed bars. Nothing is simulated here."
                : "Not connected to the AURA backend API. Candles come only from the real backend (MT5 via the internal bridge); no series is fabricated offline."
            }
            action={
              <button className="btn btn--sm" type="button" onClick={chart.refetch}>
                Retry
              </button>
            }
          />
        ) : chart.stale ? (
          <>
            <div className="conn-banner conn-banner--warn" role="alert" style={{ marginBottom: 10 }}>
              <span className="conn-banner__title">DATA STALE</span>
              <span className="conn-banner__msg">Last refresh failed — the candles shown are the last received series, not a current one.</span>
              <button className="btn btn--sm" type="button" onClick={chart.refetch}>
                Retry now
              </button>
            </div>
            <CandleChart bars={series.bars} symbol={series.symbol} timeframe={series.timeframe} onHoverCandle={setHoverCandle} />
          </>
        ) : (
          <CandleChart bars={series.bars} symbol={series.symbol} timeframe={series.timeframe} onHoverCandle={setHoverCandle} />
        )}
      </section>

      <section className="card">
        <div className="card-title">SELECTED CANDLE</div>
        <CandleReadout candle={hoverCandle} />
      </section>
      <div style={{ height: 56 }} />
    </>
  );
}
