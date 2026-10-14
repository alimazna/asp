import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { POLL, useApi } from "api/useApi";
import { getResearchStatus } from "api/endpoints";
import type { Tone } from "api/honors";
import { UNAVAILABLE, formatUtcTime } from "api/honors";

function outcomeTone(outcome: string): Tone {
  switch (outcome.toUpperCase()) {
    case "CONFIRMED":
    case "SUPPORTED":
      return "up";
    case "REJECTED":
    case "FAILED":
      return "down";
    case "INCONCLUSIVE":
    case "PENDING":
      return "warn";
    default:
      return "neutral";
  }
}

export default function ResearchPage() {
  const research = useApi({
    fetcher: (s) => getResearchStatus({ signal: s }),
    intervalMs: POLL.slow,
  });

  return (
    <>
      <h2 className="page-heading">Research</h2>
      <p className="page-sub">
        Real experiment ledger and failure memory from /research/status. Research never grants
        execution authority. No win-rate improvement or calibrated probability is claimed here —
        only what the backend reports.
      </p>

      <DataCard
        title="RESEARCH STATUS"
        result={research}
        render={(r) => (
          <>
            <dl className="kv">
              <dt>Available</dt>
              <dd>{String(r.available)}</dd>
              <dt>Mode</dt>
              <dd><Chip tone="accent">{r.mode}</Chip></dd>
            </dl>
            <div className="disclaimer" style={{ marginTop: 10 }}>{r.note}</div>

            <div className="card-title" style={{ marginTop: 16 }}>
              EXPERIMENTS ({r.experiment_count})
            </div>
            {r.experiment_count === 0 ? (
              <EmptyState label="No experiments recorded yet. The empty ledger is the honest state — nothing is estimated here." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Experiment</th>
                      <th>Hypothesis</th>
                      <th>Method</th>
                      <th>Outcome</th>
                      <th className="num">Sample</th>
                      <th className="num">Metric</th>
                      <th>Started</th>
                    </tr>
                  </thead>
                  <tbody>
                    {r.experiments.map((e) => (
                      <tr key={e.experiment_id}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{e.experiment_id}</td>
                        <td>{e.hypothesis_id}</td>
                        <td>{e.method}</td>
                        <td><Chip tone={outcomeTone(e.outcome)}>{e.outcome}</Chip></td>
                        <td className="num">{e.sample_size ?? UNAVAILABLE}</td>
                        <td className="num">{e.result_metric ?? UNAVAILABLE}</td>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(e.started_at)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}

            <div className="card-title" style={{ marginTop: 16 }}>
              FAILURE MEMORY ({r.failure_count})
            </div>
            {r.failure_count === 0 ? (
              <EmptyState label="No failures recorded." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Failure</th>
                      <th>Category</th>
                      <th>Summary</th>
                      <th className="num">Occurrences</th>
                      <th>Resolved</th>
                      <th>Last seen</th>
                    </tr>
                  </thead>
                  <tbody>
                    {r.failures.map((f) => (
                      <tr key={f.failure_id}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{f.failure_id}</td>
                        <td>{f.category}</td>
                        <td style={{ whiteSpace: "normal" }}>{f.summary}</td>
                        <td className="num">{f.occurrences ?? UNAVAILABLE}</td>
                        <td>{f.resolved ? <Chip tone="up">yes</Chip> : <Chip tone="warn">open</Chip>}</td>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(f.last_seen)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </>
        )}
      />
      <div style={{ height: 56 }} />
    </>
  );
}
