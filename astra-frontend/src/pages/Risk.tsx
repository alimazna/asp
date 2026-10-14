import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import { getAuditRecent, getHealthV1 } from "api/endpoints";
import {
  UNAVAILABLE,
  formatUtcTime,
  healthStatusTone,
  serviceStateTone,
} from "api/honors";

export default function RiskPage() {
  const { systemState, health, pollScale } = useApp();

  const healthV1 = useApi({
    fetcher: (s) => getHealthV1({ signal: s }),
    intervalMs: POLL.health * pollScale,
    enabled: systemState !== null, // /health/v1 is a newer surface; treat as optional
  });
  const audit = useApi({
    fetcher: (s) => getAuditRecent({ signal: s }),
    intervalMs: POLL.slow * pollScale,
  });

  const degraded = health?.degraded_reasons ?? [];

  return (
    <>
      <h2 className="page-heading">Risk &amp; System</h2>
      <p className="page-sub">
        Backend-owned risk posture, service health and incident state. The frontend never computes
        risk values — everything here is read directly from the API.
      </p>

      <div className="grid-half">
        <section className="card">
          <div className="card-title">SYSTEM STATE (/system/state)</div>
          {systemState === null ? (
            <EmptyState label="System state is not available yet." />
          ) : (
            <dl className="kv">
              <dt>Mode</dt>
              <dd>
                <Chip tone={serviceStateTone(systemState.mode)}>{systemState.mode}</Chip>
              </dd>
              <dt>Shadow only</dt>
              <dd>
                <Chip tone={systemState.shadow_only ? "accent" : "critical"}>
                  {String(systemState.shadow_only)}
                </Chip>
              </dd>
              <dt>Ready</dt>
              <dd>{String(systemState.ready)}</dd>
              <dt>Bridge state</dt>
              <dd>
                <Chip tone={serviceStateTone(systemState.bridge_state)}>{systemState.bridge_state}</Chip>
              </dd>
              <dt>Startup stage</dt>
              <dd>{systemState.startup_stage}</dd>
            </dl>
          )}
        </section>

        <section className="card">
          <div className="card-title">AGGREGATE HEALTH (/health)</div>
          {health === null ? (
            <EmptyState label="Aggregate health is not available yet." />
          ) : (
            <dl className="kv">
              <dt>Aggregate</dt>
              <dd>
                <Chip tone={healthStatusTone(health.aggregate)}>{health.aggregate}</Chip>
              </dd>
              <dt>Decision-grade data</dt>
              <dd>
                {health.decision_grade_data ? (
                  <Chip tone="up">yes</Chip>
                ) : (
                  <Chip tone="warn">no</Chip>
                )}
              </dd>
              <dt>Observed at</dt>
              <dd className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(health.observed_at)}</dd>
              <dt>unknown_is_not_safe</dt>
              <dd>{String(health.unknown_is_not_safe)}</dd>
            </dl>
          )}
          {degraded.length > 0 ? (
            <div className="conn-banner conn-banner--warn" style={{ marginTop: 10 }} role="alert">
              <span className="conn-banner__title">DEGRADED REASONS ({degraded.length})</span>
              <span className="conn-banner__msg">{degraded.join(" · ")}</span>
            </div>
          ) : null}
        </section>
      </div>

      <div className="grid-half">
        {healthV1.data !== null || !healthV1.error ? (
          <DataCard
            title="LIVENESS (/health/v1)"
            result={healthV1}
            render={(h) => (
              <dl className="kv">
                <dt>Status</dt>
                <dd><Chip tone={healthStatusTone(h.status)}>{h.status}</Chip></dd>
                <dt>Bridge</dt>
                <dd><Chip tone={healthStatusTone(h.bridge)}>{h.bridge}</Chip></dd>
                <dt>API version</dt>
                <dd>{h.version}</dd>
                <dt>Uptime</dt>
                <dd className="num">{h.uptime_sec !== null && h.uptime_sec !== undefined ? `${h.uptime_sec}s` : UNAVAILABLE}</dd>
              </dl>
            )}
          />
        ) : null}

        <DataCard
          title={`ACTIVE INCIDENTS (${audit.data?.active_incidents?.length ?? 0})`}
          result={audit}
          render={(a) =>
            (a.active_incidents ?? []).length === 0 ? (
              <EmptyState label="No active incidents reported." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Incident</th>
                      <th>Severity</th>
                      <th>State</th>
                      <th>Title</th>
                    </tr>
                  </thead>
                  <tbody>
                    {a.active_incidents.map((inc: { incident_id: string; severity: string; state: string; title: string }) => (
                      <tr key={inc.incident_id}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{inc.incident_id}</td>
                        <td>
                          <Chip tone={inc.severity === "critical" || inc.severity === "high" ? "critical" : "warn"}>
                            {inc.severity}
                          </Chip>
                        </td>
                        <td>{inc.state}</td>
                        <td style={{ whiteSpace: "normal" }}>{inc.title}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )
          }
        />
      </div>

      <DataCard
        title={`AUDIT STREAM (last ${Math.min(25, audit.data?.audit_records?.length ?? 0)} of ${audit.data?.audit_stream_size ?? 0})`}
        result={audit}
        render={(a) =>
          (a.audit_records ?? []).length === 0 ? (
            <EmptyState label="The audit stream is empty." />
          ) : (
            <div className="table-wrap">
              <table className="table">
                <thead>
                  <tr>
                    <th className="num">#</th>
                    <th>Action</th>
                    <th>Outcome</th>
                    <th>State</th>
                    <th>Actor</th>
                    <th>Subject</th>
                    <th>At (UTC)</th>
                    <th>Details</th>
                  </tr>
                </thead>
                <tbody>
                  {a.audit_records.slice(0, 25).map((r) => (
                    <tr key={r.event_id}>
                      <td className="num">{r.sequence ?? UNAVAILABLE}</td>
                      <td>{r.action}</td>
                      <td>
                        <Chip tone={r.outcome === "OK" ? "up" : r.outcome === "ERROR" ? "down" : "neutral"}>
                          {r.outcome}
                        </Chip>
                      </td>
                      <td>{r.service_state}</td>
                      <td>{r.actor}</td>
                      <td style={{ maxWidth: 160, overflow: "hidden", textOverflow: "ellipsis" }}>{r.subject}</td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(r.occurred_at)}</td>
                      <td className="muted" style={{ maxWidth: 260, overflow: "hidden", textOverflow: "ellipsis" }} title={r.details}>
                        {r.details}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )
        }
      />
      <div className="muted" style={{ fontSize: "var(--fs-2xs)", marginTop: 8 }}>
        Stream is append-only and hash-chained; every record carries previous_hash →
        record_hash. Full integrity verification remains a backend capability — it is
        not recomputed in the browser.
      </div>
      <div style={{ height: 56 }} />
    </>
  );
}
