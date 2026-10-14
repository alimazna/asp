import { useState } from "react";
import { CandleChart, CandleReadout } from "charts/CandleChart";
import { DataCard } from "components/DataCard";
import { Chip } from "components/Chip";
import { EmptyState, ErrorState, StateBlock } from "components/States";
import { ShadowBanner } from "components/Banners";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import * as api from "api/endpoints";
import type { Candle, CandleSeries } from "api/types";
import type { Tone } from "api/honors";
import {
  UNAVAILABLE,
  ageText,
  directionTone,
  fmtNumber,
  fmtPrice,
  formatScore,
  formatUtcTime,
  freshnessTone,
  healthStatusTone,
  qualityTone,
  serviceStateTone,
  scoreFieldLabel,
} from "api/honors";

const CHART_TF = "M15";

export default function DashboardPage() {
  const { systemState, health, connection, pollScale, demoMode } = useApp();
  const analysis = useApi({
    fetcher: (s) => api.getAnalysisLatest({ signal: s }),
    intervalMs: POLL.analysis * pollScale,
  });
  const risk = useApi({
    fetcher: (s) => api.getRiskLatest({ signal: s }),
    intervalMs: POLL.health * pollScale,
  });
  const positions = useApi({
    fetcher: (s) => api.getShadowPositions({ signal: s }),
    intervalMs: POLL.health * pollScale,
  });
  const timeframes = useApi({
    fetcher: (s) => api.getTimeframes({ signal: s }),
    intervalMs: POLL.health * 2 * pollScale,
  });
  const [hoverCandle, setHoverCandle] = useState<Candle | null>(null);

  const chart = useApi<CandleSeries>({
    fetcher: (s) => api.getCandles(CHART_TF, 180, { signal: s }),
    intervalMs: POLL.candles * pollScale,
  });

  const openPositions = (positions.data ?? []).filter((p) => p.state === "OPEN");

  const score = analysis.data ? formatScore(analysis.data.signal) : null;

  // Chart series freshness as reported by the backend (never assumed fresh).
  const chartFreshness = (chart.data?.freshness ?? "UNKNOWN").toUpperCase();
  const chartFreshnessTone: Tone =
    chartFreshness === "FRESH" ? "up" : chartFreshness === "STALE" ? "warn" : "neutral";

  return (
    <>
      <ShadowBanner demoMode={demoMode} />

      {/* KPI row */}
      <div className="grid-kpi">
        <div className="card kpi">
          <div className="kpi__label">
            <span className={`dot dot--${serviceStateTone(systemState?.mode)}`} aria-hidden />
            SYSTEM MODE
          </div>
          <div className="kpi__value">{systemState?.mode ?? UNAVAILABLE}</div>
          <div className="kpi__hint">
            {systemState ? (systemState.ready ? "backend ready" : "backend not ready") : "awaiting /system/state"}
          </div>
        </div>

        <div className="card kpi">
          <div className="kpi__label">
            <span className={`dot dot--${healthStatusTone(health?.aggregate)}`} aria-hidden />
            AGGREGATE HEALTH
          </div>
          <div className="kpi__value">{health?.aggregate ?? UNAVAILABLE}</div>
          <div className="kpi__hint">
            {health
              ? health.decision_grade_data
                ? "decision-grade data"
                : "data not decision-grade"
              : health
                ? "awaiting health"
                : "awaiting /health"}
          </div>
        </div>

        <div className="card kpi">
          <div className="kpi__label">
            <span className={`dot dot--${directionTone(analysis.data?.signal.direction)}`} aria-hidden />
            SIGNAL DIRECTION
          </div>
          <div
            className={`kpi__value ${directionTone(analysis.data?.signal.direction) === "up" ? "kpi__value--up" : directionTone(analysis.data?.signal.direction) === "down" ? "kpi__value--down" : "kpi__value--na"}`}
          >
            {analysis.data ? analysis.data.signal.direction : "—"}
          </div>
          <div className="kpi__hint">from /analysis/latest</div>
        </div>

        <div className="card kpi">
          <div className="kpi__label">
            <span className={`dot dot--${score?.kind === "prob" ? "up" : "neutral"}`} aria-hidden />
            {analysis.data ? scoreFieldLabel(analysis.data.signal) : "SCORE"}
          </div>
          <div className="kpi__value">{score?.label ?? UNAVAILABLE}</div>
          <div className="kpi__hint" title={score?.detail}>
            {analysis.data ? (analysis.data.signal.probability_calibrated ? "calibrated probability" : "raw score — not a probability") : "—"}
          </div>
        </div>

        <div className="card kpi">
          <div className="kpi__label">
            <span className={`dot dot--${health?.degraded_reasons?.length ? "warn" : health ? "up" : "neutral"}`} aria-hidden />
            DEGRADED
          </div>
          <div className="kpi__value">
            {health ? (health.degraded_reasons.length > 0 ? String(health.degraded_reasons.length) : "0") : "—"}
          </div>
          <div className="kpi__hint">
            {health && health.degraded_reasons.length > 0
              ? health.degraded_reasons.slice(0, 2).join(" · ")
              : health
                ? "no degraded reasons reported"
                : "—"}
          </div>
        </div>
      </div>

      {/* Chart + analysis summary */}
      <div className="grid-2">
        <section className="card">
          <div className="row spread wrap">
            <h3 className="card-title" style={{ margin: 0 }}>
              XAUUSD · {CHART_TF} · CLOSED BARS
            </h3>
            {chart.data ? (
              <Chip tone={chartFreshnessTone}>
                {chart.data.freshness ?? "UNKNOWN"}
              </Chip>
            ) : null}
          </div>
          {chart.loading && chart.data === null ? (
            <div className="skel skel--card" style={{ height: 360 }} />
          ) : chart.error !== null && chart.data === null ? (
            <ErrorState title={chart.error} detail={chart.errorDetail} onRetry={chart.refetch} />
          ) : chart.data === null || chart.data.bars.length === 0 ? (
            <StateBlock
              icon="◫"
              title="NO CANDLES YET"
              message={
                connection.state === "CONNECTED"
                  ? "The backend returned no closed bars for this timeframe yet."
                  : "Connect the AURA backend to load candles. No data is invented."
              }
              action={
                <button type="button" className="btn btn--sm" onClick={chart.refetch}>
                  Retry
                </button>
              }
            />
          ) : (
            <>
              <CandleChart
                bars={chart.data.bars}
                symbol={chart.data.symbol}
                timeframe={chart.data.timeframe}
                onHoverCandle={setHoverCandle}
              />
              <div style={{ marginTop: 8 }}>
                <CandleReadout candle={hoverCandle} />
              </div>
            </>
          )}
        </section>

        <DataCard
          title="LATEST ANALYSIS"
          result={analysis}
          emptyTitle="NO ANALYSIS PUBLISHED"
          emptyLabel="The backend has not published a decision record yet."
          render={(a) => (
            <>
              <dl className="kv">
                <dt>Direction</dt>
                <dd className={`num`} style={{ color: `var(--${directionTone(a.signal.direction) === "up" ? "up" : directionTone(a.signal.direction) === "down" ? "down" : "text-muted"})` }}>
                  {a.signal.direction}
                </dd>
                <dt>{scoreFieldLabel(a.signal)}</dt>
                <dd className="num">{formatScore(a.signal).label}</dd>
                <dt>Probability</dt>
                <dd className="num">
                  {a.signal.probability_calibrated && a.signal.probability !== null
                    ? `${(a.signal.probability * 100).toFixed(1)}%`
                    : "N/A — CALIBRATION NOT AUTHORISED"}
                </dd>
                <dt>Regime</dt>
                <dd>{a.context.regime}</dd>
                <dt>H4 bias</dt>
                <dd>{a.context.h4_bias}</dd>
                <dt>M15 trigger</dt>
                <dd>{a.context.m15_trigger}</dd>
                <dt>Volatility</dt>
                <dd>{a.context.volatility_state}</dd>
                <dt>Coverage</dt>
                <dd>{a.meta.coverage_tier.toUpperCase()}</dd>
                <dt>Data fresh</dt>
                <dd className="num">{a.meta.data_freshness_sec ?? UNAVAILABLE}</dd>
                <dt>Timestamp</dt>
                <dd className="num" style={{ fontSize: "var(--fs-2xs)" }}>
                  {formatUtcTime(a.timestamp)}
                </dd>
              </dl>
              <div className="disclaimer" style={{ marginTop: 10 }}>
                {a.meta.disclaimer}
              </div>
            </>
          )}
        />
      </div>

      {/* Timeframe overview + risk/positions */}
      <div className="grid-half">
        <DataCard
          title="NINE-TIMEFRAME OVERVIEW"
          result={timeframes}
          render={(rows) => (
            <div className="table-wrap">
              <table className="table matrix">
                <thead>
                  <tr>
                    <th>TF</th>
                    <th>State</th>
                    <th>Closed</th>
                    <th>Fresh</th>
                  </tr>
                </thead>
                <tbody>
                  {rows.map((r) => (
                    <tr key={r.timeframe} className={r.observed ? undefined : "not-observed"}>
                      <td className={r.timeframe === "H4" ? "num" : undefined}>
                        <span className="row" style={{ gap: 6 }}>
                          {r.timeframe}
                          {r.timeframe === "H4" ? (
                            <span className="chip chip--accent" style={{ padding: "1px 6px", fontSize: 9 }}>STRUCT</span>
                          ) : r.timeframe === "M15" ? (
                            <span className="chip chip--neutral" style={{ padding: "1px 6px", fontSize: 9 }}>OP</span>
                          ) : null}
                        </span>
                      </td>
                      <td>
                        <Chip tone={qualityTone(r)} title={r.quality?.state}>
                          {r.observed ? r.quality?.state ?? "UNKNOWN" : "UNOBSERVED"}
                        </Chip>
                      </td>
                      <td className="num">{r.has_closed_bar ? "yes" : r.observed ? "no" : "—"}</td>
                      <td>
                        <Chip tone={freshnessTone(r.freshness?.state)} pulse={r.freshness?.state === "FRESH"}>
                          {r.freshness?.state ?? "UNKNOWN"}
                        </Chip>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        />

        <div className="stack">
          <DataCard
            title="RISK STATE"
            result={risk}
            render={(r) => (
              <dl className="kv">
                <dt>Available</dt>
                <dd>{r.available ? "yes" : "no"}</dd>
                <dt>Open positions</dt>
                <dd className="num">{r.available ? r.open_positions : UNAVAILABLE}</dd>
                <dt>Aggregate risk</dt>
                <dd className="num">
                  {r.available ? fmtNumber(r.aggregate_open_risk_fraction, 4) : UNAVAILABLE}
                </dd>
                <dt>Guardian bound</dt>
                <dd>{r.risk_bounded_by_guardian ? "yes" : "no"}</dd>
                <dt>Proposal</dt>
                <dd>{r.proposal_available ? (r.proposal?.decision ?? "available") : r.proposal_reason || "not available"}</dd>
              </dl>
            )}
          />
          <DataCard
            title={`OPEN SHADOW POSITIONS (${openPositions.length})`}
            result={positions}
            emptyLabel="No open shadow positions."
            render={() =>
              openPositions.length === 0 ? (
                <EmptyState label="No open shadow positions right now." />
              ) : (
                <div className="table-wrap">
                  <table className="table">
                    <thead>
                      <tr>
                        <th>ID</th>
                        <th>Dir</th>
                        <th className="num">Entry</th>
                        <th>TF</th>
                      </tr>
                    </thead>
                    <tbody>
                      {openPositions.slice(0, 6).map((p) => (
                        <tr key={p.position_id}>
                          <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{p.position_id}</td>
                          <td style={{ color: `var(--${directionTone(p.direction) === "up" ? "up" : directionTone(p.direction) === "down" ? "down" : "text-muted"})` }}>
                            {p.direction}
                          </td>
                          <td className="num">{fmtPrice(p.entry_price)}</td>
                          <td>{p.timeframe ?? UNAVAILABLE}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )
            }
          />
          {systemState?.mode && ["EMERGENCY", "HALTED", "MANUAL", "PAUSED"].includes(systemState.mode) ? (
            <div className="conn-banner conn-banner--warn" role="alert">
              <span className="conn-banner__title">SYSTEM MODE: {systemState.mode}</span>
              <span className="conn-banner__msg">
                The backend is in a restricted operational mode. Capabilities may be limited. SHADOW ONLY remains in force.
              </span>
            </div>
          ) : null}
        </div>
      </div>

      <div className="row spread wrap muted" style={{ fontSize: "var(--fs-2xs)" }}>
        <span>
          Connection: {connection.state}
          {connection.detail ? ` — ${connection.detail}` : ""}
        </span>
        <span>
          {analysis.fetchedAt ? ageText(analysis.fetchedAt) : "no analysis received yet"} · Data via AURA API v1 · XAUUSD only
        </span>
      </div>
      <div style={{ height: 56 }} />
    </>
  );
}
