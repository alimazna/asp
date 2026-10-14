import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import { getTimeframeSnapshot, getTimeframes } from "api/endpoints";
import { TIMEFRAMES, type Timeframe, type TimeframeRow } from "api/types";
import {
  UNAVAILABLE,
  formatUtcTime,
  freshnessTone,
  qualityTone,
} from "api/honors";

function MatrixRows({ rows }: { rows: TimeframeRow[] }) {
  // Enforcement of canonical order regardless of backend ordering.
  const byTf = new Map(rows.map((r) => [r.timeframe, r]));
  const ordered = TIMEFRAMES.map((tf) => byTf.get(tf)).filter(
    (r): r is TimeframeRow => r !== undefined,
  );
  return (
    <div className="table-wrap">
      <table className="table matrix">
        <thead>
          <tr>
            <th>Timeframe</th>
            <th>Observed</th>
            <th>Quality</th>
            <th>Decision-grade</th>
            <th>Closed bar</th>
            <th>Freshness</th>
            <th>Last update</th>
            <th>Last closed bar</th>
            <th>Seq</th>
            <th>Capability impact</th>
          </tr>
        </thead>
        <tbody>
          {ordered.map((r) => (
            <tr key={r.timeframe} className={r.observed ? undefined : "not-observed"}>
              <td className="num">
                <span className="row" style={{ gap: 6 }}>
                  {r.timeframe}
                  {r.timeframe === "H4" ? (
                    <span className="chip chip--accent" style={{ padding: "1px 6px", fontSize: 9 }}>STRUCT</span>
                  ) : r.timeframe === "M15" ? (
                    <span className="chip chip--neutral" style={{ padding: "1px 6px", fontSize: 9 }}>OP</span>
                  ) : null}
                </span>
              </td>
              <td>{r.observed ? "yes" : <span className="chip chip--neutral">no</span>}</td>
              <td>
                <Chip tone={qualityTone(r)}>{r.observed ? r.quality?.state ?? "UNKNOWN" : "UNOBSERVED"}</Chip>
              </td>
              <td>
                {r.decision_grade ? (
                  <Chip tone="up">yes</Chip>
                ) : (
                  <span className="muted">—</span>
                )}
              </td>
              <td className="num">{r.has_closed_bar ? "yes" : r.observed ? "no" : UNAVAILABLE}</td>
              <td>
                <Chip tone={freshnessTone(r.freshness?.state)} pulse={r.freshness?.state === "FRESH"}>
                  {r.freshness?.state ?? "UNKNOWN"}
                </Chip>
              </td>
              <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>
                {formatUtcTime(r.last_successful_update)}
              </td>
              <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>
                {formatUtcTime(r.last_closed_bar_open)}
              </td>
              <td className="num">{r.sequence ?? UNAVAILABLE}</td>
              <td style={{ whiteSpace: "normal", fontSize: "var(--fs-2xs)", color: "var(--text-muted)", maxWidth: 320 }}>
                {(r.capability_impact ?? [])
                  .map((ci) => `${ci.capability}: ${ci.impact} (${ci.reason})`)
                  .join(" · ") || UNAVAILABLE}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

/** Compact per-TF cards for narrow screens. */
function MatrixCards({ rows }: { rows: TimeframeRow[] }) {
  const byTf = new Map(rows.map((r) => [r.timeframe, r]));
  const ordered = TIMEFRAMES.map((tf) => byTf.get(tf)).filter((r): r is TimeframeRow => r !== undefined);
  return (
    <div className="grid" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(150px, 1fr))" }}>
      {ordered.map((r) => (
        <div key={r.timeframe} className="card" style={{ padding: 12 }}>
          <div className="row spread">
            <span className="num" style={{ fontWeight: 700 }}>{r.timeframe}</span>
            <Chip tone={qualityTone(r)}>{r.observed ? r.quality?.state ?? "UNKNOWN" : "UNOBS"}</Chip>
          </div>
          <dl className="kv" style={{ marginTop: 8, fontSize: "var(--fs-2xs)" }}>
            <dt>Grade</dt>
            <dd>{r.decision_grade ? "yes" : "no"}</dd>
            <dt>Closed</dt>
            <dd>{r.has_closed_bar ? "yes" : r.observed ? "no" : UNAVAILABLE}</dd>
            <dt>Fresh</dt>
            <dd>{r.freshness?.state ?? "UNKNOWN"}</dd>
            <dt>Updated</dt>
            <dd>{formatUtcTime(r.last_successful_update)}</dd>
          </dl>
          {(r.capability_impact?.length ?? 0) > 0 ? (
            <div style={{ marginTop: 6, fontSize: "var(--fs-2xs)", color: "var(--warn)" }}>
              {r.capability_impact!.map((ci) => `${ci.capability}: ${ci.impact}`).join(" · ")}
            </div>
          ) : null}
        </div>
      ))}
    </div>
  );
}

export default function TimeframesPage() {
  const { pollScale } = useApp();
  const tfs = useApi({
    fetcher: (s) => getTimeframes({ signal: s }),
    intervalMs: POLL.health * 2 * pollScale,
  });

  const selectedTf: Timeframe = "M15";
  const snapshot = useApi({
    fetcher: (s) => getTimeframeSnapshot(selectedTf, { signal: s }),
    intervalMs: POLL.health * 2 * pollScale,
  });

  return (
    <>
      <h2 className="page-heading">Multi-Timeframe Status</h2>
      <p className="page-sub">
        All nine canonical timeframes in fixed order M1 → M5 → M15 → M30 → H1 → H4 → D1 → W1 →
        MN1. H4 is the structural-authority timeframe (STRUCT) and M15 the primary operational
        timeframe (OP). Unobserved timeframes are always listed — never hidden or invented.
      </p>

      <section className="card">
        <div className="card-title">MATRIX (DESKTOP)</div>
        <DataCard
          title="CAPABILITY MATRIX — NINE TIMEFRAMES"
          showFreshness={false}
          result={tfs}
          render={(rows: TimeframeRow[]) =>
            rows.length === 0 ? (
              <EmptyState label="The backend returned no timeframe capability rows." />
            ) : (
              <>
                <div className="matrix-desktop" style={{ display: "block" }}>
                  <MatrixRows rows={rows} />
                </div>
                <div className="matrix-cards" style={{ display: "none" }}>
                  <MatrixCards rows={rows} />
                </div>
              </>
            )
          }
        />
        <style>{`
          .matrix-cards { display: none !important; }
          @media (max-width: 899px) {
            .matrix-desktop { display: none !important; }
            .matrix-cards { display: block !important; }
          }
        `}</style>
      </section>

      <DataCard
        title={`SNAPSHOT — ${selectedTf} (PRIMARY OPERATIONAL TF)`}
        result={snapshot}
        render={(s) => (
          <dl className="kv">
            <dt>Observed</dt>
            <dd>{s.observed ? "yes" : "no"}</dd>
            <dt>Quality</dt>
            <dd>{s.quality?.state ?? "UNKNOWN"}</dd>
            <dt>Decision-grade</dt>
            <dd>{s.quality?.decision_grade ? "yes" : "no"}</dd>
            <dt>Closed bar</dt>
            <dd>{s.has_closed_bar ? "yes" : "no"}</dd>
            <dt>Open (OHLC)</dt>
            <dd className="num">{s.open ?? UNAVAILABLE}</dd>
            <dt>High</dt>
            <dd className="num">{s.high ?? UNAVAILABLE}</dd>
            <dt>Low</dt>
            <dd className="num">{s.low ?? UNAVAILABLE}</dd>
            <dt>Close</dt>
            <dd className="num">{s.close ?? UNAVAILABLE}</dd>
            <dt>Freshness</dt>
            <dd>{s.freshness ? `${s.freshness.state} (age ${s.freshness.age_millis ?? "?"}ms)` : UNAVAILABLE}</dd>
            <dt>Last update</dt>
            <dd>{formatUtcTime(s.last_successful_update)}</dd>
            <dt>Capability impact</dt>
            <dd style={{ whiteSpace: "normal" }}>
              {(s.capability_impact ?? []).map((ci) => `${ci.capability}: ${ci.impact}`).join(" · ") || UNAVAILABLE}
            </dd>
          </dl>
        )}
      />
      <div style={{ height: 56 }} />
    </>
  );
}
