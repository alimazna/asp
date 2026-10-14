import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { POLL, useApi } from "api/useApi";
import { getAuditRecent } from "api/endpoints";
import { UNAVAILABLE, formatUtcTime } from "api/honors";

export default function AuditPage() {
  const audit = useApi({
    fetcher: (s) => getAuditRecent({ signal: s }),
    intervalMs: POLL.slow,
  });

  return (
    <>
      <h2 className="page-heading">Audit Log</h2>
      <p className="page-sub">
        Append-only, hash-chained audit stream and active incidents from /audit/recent (newest
        first, bounded to 200 by the backend).
      </p>

      <DataCard
        title={`AUDIT RECORDS (${audit.data?.count ?? 0} in stream: ${audit.data?.audit_stream_size ?? 0})`}
        result={audit}
        render={(a) =>
          (a.audit_records ?? []).length === 0 ? (
            <EmptyState label="No audit records yet." />
          ) : (
            <div className="table-wrap">
              <table className="table">
                <thead>
                  <tr>
                    <th className="num">Seq</th>
                    <th>Event</th>
                    <th>Action</th>
                    <th>Outcome</th>
                    <th>Service state</th>
                    <th>Actor</th>
                    <th>Subject</th>
                    <th>At (UTC)</th>
                    <th>Details</th>
                    <th>Record hash</th>
                  </tr>
                </thead>
                <tbody>
                  {a.audit_records.map((r) => (
                    <tr key={r.event_id}>
                      <td className="num">{r.sequence ?? UNAVAILABLE}</td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{r.event_id}</td>
                      <td>{r.action}</td>
                      <td>
                        <Chip tone={r.outcome === "OK" ? "up" : r.outcome === "ERROR" ? "down" : "neutral"}>
                          {r.outcome}
                        </Chip>
                      </td>
                      <td>{r.service_state}</td>
                      <td>{r.actor}</td>
                      <td style={{ maxWidth: 150, overflow: "hidden", textOverflow: "ellipsis" }} title={r.subject}>
                        {r.subject}
                      </td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(r.occurred_at)}</td>
                      <td className="muted" style={{ maxWidth: 240, overflow: "hidden", textOverflow: "ellipsis" }} title={r.details}>
                        {r.details}
                      </td>
                      <td className="num muted" style={{ fontSize: "var(--fs-2xs)", maxWidth: 110, overflow: "hidden", textOverflow: "ellipsis" }} title={r.record_hash}>
                        {r.record_hash ? `${r.record_hash.slice(0, 8)}…` : UNAVAILABLE}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )
        }
      />
      <div style={{ height: 56 }} />
    </>
  );
}
