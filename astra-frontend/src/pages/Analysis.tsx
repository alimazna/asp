import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { Unavailable } from "components/States";
import { ShadowBanner } from "components/Banners";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import * as api from "api/endpoints";
import type { AnalysisRecord } from "api/types";
import {
  UNAVAILABLE,
  directionTone,
  fmtNumber,
  formatDirection,
  formatScore,
  formatUtcTime,
  probabilityOrNullText,
  scoreFieldLabel,
} from "api/honors";

function AnalysisBody({ a }: { a: AnalysisRecord }) {
  const score = formatScore(a.signal);
  const dirTone = directionTone(a.signal.direction);
  return (
    <>
      {a.meta.degraded ? (
        <div className="conn-banner conn-banner--warn" role="alert" style={{ marginBottom: 12 }}>
          <span className="conn-banner__title">DEGRADED DATA</span>
          <span className="conn-banner__msg">
            The backend marks this analysis record as degraded. Confident styling is suppressed and
            unavailable fields are rendered as such.
          </span>
        </div>
      ) : null}
      <div className="grid-half">
        <section>
          <div className="card-title">SIGNAL</div>
          <dl className="kv">
            <dt>Direction</dt>
            <dd className="num" style={{ fontWeight: 700, color: `var(--${dirTone === "up" ? "up" : dirTone === "down" ? "down" : "text-muted"})` }}>
              {formatDirection(a.signal.direction)}
            </dd>
            <dt>Horizon</dt>
            <dd>{a.signal.horizon ?? UNAVAILABLE}</dd>
            <dt>{scoreFieldLabel(a.signal)}</dt>
            <dd className="num" style={{ fontSize: "var(--fs-md)" }}>{score.label}</dd>
            <dt>Model version</dt>
            <dd>{a.signal.model_version ?? UNAVAILABLE}</dd>
            <dt>Features</dt>
            <dd>{a.signal.features_contributing.length > 0 ? a.signal.features_contributing.map(String).join(", ") : "[]"}</dd>
          </dl>
          <div className="disclaimer" style={{ marginTop: 10 }}>
            {score.detail}
          </div>
        </section>

        <section>
          <div className="card-title">CONTEXT</div>
          <dl className="kv">
            <dt>Regime</dt>
            <dd>{a.context.regime}</dd>
            <dt>H4 bias</dt>
            <dd>{a.context.h4_bias}</dd>
            <dt>M15 trigger</dt>
            <dd>{a.context.m15_trigger}</dd>
            <dt>MTF agreement</dt>
            <dd className="num">{a.context.mtf_agreement ?? UNAVAILABLE}</dd>
            <dt>Volatility</dt>
            <dd>{a.context.volatility_state}</dd>
          </dl>
        </section>
      </div>

      <div className="grid-half" style={{ marginTop: 16 }}>
        <section>
          <div className="card-title">PROBABILITY (RULE C)</div>
          <dl className="kv">
            <dt>Published probability</dt>
            <dd className="num" style={{ color: a.signal.probability_calibrated ? "var(--up)" : undefined }}>
              {probabilityOrNullText(a.signal)}
            </dd>
            <dt>Calibrated</dt>
            <dd>{a.signal.probability_calibrated ? "yes" : "no"}</dd>
            <dt>score_is_probability</dt>
            <dd>{String(a.meta.score_is_probability)}</dd>
            <dt>Confidence lo</dt>
            <dd className="num">{a.signal.confidence_lo ?? UNAVAILABLE}</dd>
            <dt>Confidence hi</dt>
            <dd className="num">{a.signal.confidence_hi ?? UNAVAILABLE}</dd>
          </dl>
        </section>

        <section>
          <div className="card-title">LEVELS (SUGGESTIONS ONLY)</div>
          <dl className="kv">
            <dt>Entry</dt>
            <dd className="num">{a.levels.entry ?? UNAVAILABLE}</dd>
            <dt>Stop loss</dt>
            <dd className="num">{a.levels.stop_loss ?? UNAVAILABLE}</dd>
            <dt>Take profit</dt>
            <dd className="num">{a.levels.take_profit ?? UNAVAILABLE}</dd>
            <dt>Reward/risk</dt>
            <dd className="num">{fmtNumber(a.levels.reward_risk)}</dd>
            <dt>Suggested risk %</dt>
            <dd className="num">{a.levels.suggested_risk_pct ?? UNAVAILABLE}</dd>
            <dt>SL method</dt>
            <dd>{a.levels.sl_method ?? UNAVAILABLE}</dd>
            <dt>TP method</dt>
            <dd>{a.levels.tp_method ?? UNAVAILABLE}</dd>
          </dl>
          <div className="muted" style={{ fontSize: "var(--fs-2xs)", marginTop: 8 }}>
            Levels come from the live risk proposal and are never orders. Null levels are not drawn
            anywhere in this terminal.
          </div>
        </section>
      </div>

      <div className="grid-half" style={{ marginTop: 16 }}>
        <section>
          <div className="card-title">META &amp; COVERAGE</div>
          <dl className="kv">
            <dt>Symbol</dt>
            <dd>{a.symbol}</dd>
            <dt>Timestamp</dt>
            <dd className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(a.timestamp)}</dd>
            <dt>Coverage tier</dt>
            <dd>
              <Chip tone={a.meta.coverage_tier === "high" ? "up" : a.meta.coverage_tier === "unknown" ? "neutral" : "warn"}>
                {a.meta.coverage_tier.toUpperCase()}
              </Chip>
            </dd>
            <dt>Data freshness (sec)</dt>
            <dd className="num">{a.meta.data_freshness_sec ?? UNAVAILABLE}</dd>
            <dt>Degraded</dt>
            <dd>{String(a.meta.degraded)}</dd>
          </dl>
          <div className="disclaimer" style={{ marginTop: 10 }}>
            {a.meta.disclaimer}
          </div>
        </section>
      </div>
    </>
  );
}

export default function AnalysisPage() {
  const { pollScale } = useApp();
  const analysis = useApi({
    fetcher: (s) => api.getAnalysisLatest({ signal: s }),
    intervalMs: POLL.analysis * pollScale,
  });
  const context = useApi({
    fetcher: (s) => api.getContextLatest({ signal: s }),
    intervalMs: POLL.analysis * pollScale,
  });
  const risk = useApi({
    fetcher: (s) => api.getRiskLatest({ signal: s }),
    intervalMs: POLL.health * pollScale,
  });

  return (
    <>
      <h2 className="page-heading">Latest Analysis &amp; Signal</h2>
      <p className="page-sub">
        Raw decision record from /analysis/latest + /context/latest + /risk/latest. Only fields the
        backend actually returned are shown; nulls stay visibly unavailable.
      </p>
      <ShadowBanner />

      <DataCard
        title="ANALYSIS RECORD"
        result={analysis}
        emptyTitle="NO ANALYSIS AVAILABLE"
        emptyLabel="The backend has not produced an analysis record yet — e.g. before the first closed M15 bar has been observed."
        render={(a) => <AnalysisBody a={a} />}
      />

      <DataCard
        title="LIVE MARKET CONTEXT"
        result={context}
        render={(c) => (
          <dl className="kv">
            <dt>Symbol</dt>
            <dd>{c.symbol}</dd>
            <dt>Timestamp</dt>
            <dd className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(c.timestamp)}</dd>
            <dt>Regime</dt>
            <dd>{c.context.regime}</dd>
            <dt>H4 bias</dt>
            <dd>{c.context.h4_bias}</dd>
            <dt>M15 trigger</dt>
            <dd>{c.context.m15_trigger}</dd>
            <dt>Volatility state</dt>
            <dd>{c.context.volatility_state}</dd>
            <dt>MTF agreement</dt>
            <dd className="num">{c.context.mtf_agreement ?? UNAVAILABLE}</dd>
          </dl>
        )}
      />

      <DataCard
        title="RISK PROPOSAL (IF ANY)"
        result={risk}
        render={(r) =>
          r.proposal_available && r.proposal !== null ? (
            <dl className="kv">
              <dt>Decision</dt>
              <dd>{r.proposal.decision ?? UNAVAILABLE}</dd>
              <dt>Direction</dt>
              <dd>{r.proposal.direction ?? UNAVAILABLE}</dd>
              <dt>Entry</dt>
              <dd className="num">{r.proposal.entry_price ?? UNAVAILABLE}</dd>
              <dt>Stop</dt>
              <dd className="num">{r.proposal.stop_price ?? UNAVAILABLE}</dd>
              <dt>Target</dt>
              <dd className="num">{r.proposal.target_price ?? UNAVAILABLE}</dd>
              <dt>Risk fraction</dt>
              <dd className="num">{r.proposal.risk_fraction ?? UNAVAILABLE}</dd>
              <dt>Risk amount</dt>
              <dd className="num">{r.proposal.risk_amount ?? UNAVAILABLE}</dd>
              <dt>Size (lots)</dt>
              <dd className="num">{r.proposal.position_size_lots ?? UNAVAILABLE}</dd>
              <dt>R:R</dt>
              <dd className="num">{fmtNumber(r.proposal.reward_risk_ratio)}</dd>
              <dt>Valid / reason</dt>
              <dd>{`${String(r.proposal.valid)} · ${r.proposal.reason ?? UNAVAILABLE}`}</dd>
            </dl>
          ) : (
            <Unavailable label={r.proposal_reason || "No risk proposal exists for the current decision."} />
          )
        }
      />
      <div style={{ height: 56 }} />
    </>
  );
}
