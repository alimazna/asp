import { useState } from "react";
import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { ErrorState, StateBlock } from "components/States";
import { POLL, useApi } from "api/useApi";
import { getGovernanceStatus, postCommand } from "api/endpoints";
import { UNAVAILABLE, formatUtcTime } from "api/honors";

/**
 * Governance page. The only write surface the backend exposes is
 * POST /command restricted to `notify` and `request_approval` with a
 * non-empty `actor`. No live-trading switch exists anywhere in this UI and
 * `live_trading_authorised` is always displayed as returned (false).
 */
export default function GovernancePage() {
  const gov = useApi({
    fetcher: (s) => getGovernanceStatus({ signal: s }),
    intervalMs: POLL.slow,
  });

  const [actor, setActor] = useState("");
  const [payload, setPayload] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [feedback, setFeedback] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (command: "notify" | "request_approval") => {
    if (actor.trim() === "") {
      setFeedback({ ok: false, text: "Actor attribution is required (400 actor_required)." });
      return;
    }
    setSubmitting(true);
    setFeedback(null);
    try {
      const res = await postCommand({ command, actor: actor.trim(), payload: payload.trim() });
      setFeedback({
        ok: true,
        text: `Accepted${res.request_id ? ` — request ${res.request_id}${res.status ? ` (${res.status})` : ""}` : res.message_id ? ` — message ${res.message_id}` : ""}.`,
      });
      setPayload("");
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFeedback({ ok: false, text: `Rejected by backend: ${msg}` });
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <>
      <h2 className="page-heading">Governance &amp; Approvals</h2>
      <p className="page-sub">
        Approval queue and request history from /governance/status. Live trading is never
        authorised in this system.
      </p>

      <div className="conn-banner conn-banner--warn" role="note" style={{ marginBottom: 4 }}>
        <span className="conn-banner__title">LIVE TRADING</span>
        <span className="conn-banner__msg">
          {gov.data
            ? `live_trading_authorised = ${String(gov.data.live_trading_authorised)} — shadow only, by construction. Requests that attempt execution are rejected with 403 command_not_permitted.`
            : "reading /governance/status…"}
        </span>
      </div>

      <DataCard
        title={`PENDING APPROVALS (${gov.data?.pending_count ?? 0})`}
        result={gov}
        render={(g) =>
          g.pending.length === 0 ? (
            <StateBlock icon="◌" title="NOTHING PENDING" message="No approval requests are queued." />
          ) : (
            <div className="table-wrap">
              <table className="table">
                <thead>
                  <tr>
                    <th>Request</th>
                    <th>Kind</th>
                    <th>Subject</th>
                    <th>Requested</th>
                  </tr>
                </thead>
                <tbody>
                  {g.pending.map((p) => (
                    <tr key={p.request_id}>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{p.request_id}</td>
                      <td>{p.kind}</td>
                      <td>{p.subject_id}</td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(p.requested_at)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )
        }
      />

      <DataCard
        title={`REQUEST HISTORY (${gov.data?.history?.length ?? 0})`}
        result={gov}
        emptyLabel="No governance requests have been made."
        render={(g) =>
          g.history.length === 0 ? (
            <StateBlock icon="◌" title="NO HISTORY" />
          ) : (
            <div className="table-wrap">
              <table className="table">
                <thead>
                  <tr>
                    <th>Request</th>
                    <th>Kind</th>
                    <th>Status</th>
                    <th>By</th>
                    <th>Requested</th>
                    <th>Decided</th>
                  </tr>
                </thead>
                <tbody>
                  {g.history.map((h) => (
                    <tr key={h.request_id}>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{h.request_id}</td>
                      <td>{h.kind}</td>
                      <td>
                        <Chip tone={h.status === "APPROVED" ? "up" : h.status === "DENIED" ? "down" : "neutral"}>
                          {h.status}
                        </Chip>
                      </td>
                      <td>{h.requested_by ?? UNAVAILABLE}</td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(h.requested_at)}</td>
                      <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(h.decided_at)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )
        }
      />

      <section className="card">
        <div className="card-title">OPERATOR COMMANDS (ALLOW-LIST)</div>
        <p className="muted" style={{ fontSize: "var(--fs-xs)", marginTop: 0 }}>
          The backend accepts exactly two commands — <code>notify</code> and{" "}
          <code>request_approval</code>. Every command requires an <code>actor</code> for
          attribution. Any execution-like command is rejected with 403; there is no path in this UI
          that could place a live order.
        </p>
        <div className="field">
          <label htmlFor="gov-actor">Actor (required)</label>
          <input
            id="gov-actor"
            className="value-input"
            value={actor}
            onChange={(e) => setActor(e.target.value)}
            placeholder="operator name / id"
          />
        </div>
        <div className="field">
          <label htmlFor="gov-payload">Payload (free text)</label>
          <input
            id="gov-payload"
            className="value-input"
            value={payload}
            onChange={(e) => setPayload(e.target.value)}
            placeholder="message or approval subject"
          />
        </div>
        <div className="row wrap" style={{ gap: 10 }}>
          <button className="btn" type="button" disabled={submitting} onClick={() => submit("notify")}>
            Send notify
          </button>
          <button
            className="btn"
            type="button"
            disabled={submitting}
            onClick={() => submit("request_approval")}
          >
            Request approval
          </button>
        </div>
        {feedback ? (
          feedback.ok ? (
            <div className="conn-banner conn-banner--warn" style={{ marginTop: 12 }}>
              <span className="conn-banner__title">ACCEPTED (202)</span>
              <span className="conn-banner__msg">{feedback.text}</span>
            </div>
          ) : (
            <div style={{ marginTop: 12 }}>
              <ErrorState title="COMMAND REJECTED" detail={feedback.text} />
            </div>
          )
        ) : null}
      </section>
      <div style={{ height: 56 }} />
    </>
  );
}
