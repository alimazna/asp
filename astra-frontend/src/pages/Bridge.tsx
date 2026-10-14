import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { useApp } from "lib/appContext";
import { POLL, useApi } from "api/useApi";
import { getBridgeStatus } from "api/endpoints";
import { UNAVAILABLE, formatUtcTime, serviceStateTone } from "api/honors";

export default function BridgePage() {
  const { pollScale } = useApp();
  const bridge = useApi({
    fetcher: (s) => getBridgeStatus({ signal: s }),
    intervalMs: POLL.health * pollScale,
  });

  return (
    <>
      <h2 className="page-heading">MT5 Bridge &amp; Market Data</h2>
      <p className="page-sub">
        The Python/MT5 bridge is an internal backend component on 127.0.0.1:8791 — the browser
        never contacts it directly. Everything on this page is read from /bridge/status.
      </p>

      <DataCard
        title="BRIDGE LIFECYCLE"
        result={bridge}
        render={(b) => (
          <>
            <div className="grid-half">
              <dl className="kv">
                <dt>Process state</dt>
                <dd><Chip tone={serviceStateTone(b.bridge_process_state)}>{b.bridge_process_state}</Chip></dd>
                <dt>process_state</dt>
                <dd>{b.process_state}</dd>
                <dt>Startup stage</dt>
                <dd>{b.startup_stage}</dd>
                <dt>Handshake</dt>
                <dd>{b.handshake_ok ? <Chip tone="up">OK</Chip> : <Chip tone="warn">not established</Chip>}</dd>
                <dt>MT5 ready</dt>
                <dd>{b.mt5_ready ? <Chip tone="up">YES</Chip> : <Chip tone="warn">NO</Chip>}</dd>
                <dt>MT5 ready (live)</dt>
                <dd>{String(b.mt5_ready_live)}</dd>
                <dt>Initialized</dt>
                <dd>{String(b.initialized)}</dd>
                <dt>Package available</dt>
                <dd>{String(b.package_available)}</dd>
              </dl>
              <dl className="kv">
                <dt>Resolved symbol</dt>
                <dd className="num">{b.resolved_symbol || UNAVAILABLE}</dd>
                <dt>Bridge symbol</dt>
                <dd>{b.bridge_symbol ?? UNAVAILABLE}</dd>
                <dt>Broker</dt>
                <dd>{b.broker ?? UNAVAILABLE}</dd>
                <dt>Server</dt>
                <dd>{b.server ?? UNAVAILABLE}</dd>
                <dt>Transport</dt>
                <dd>{b.transport}</dd>
                <dt>Host</dt>
                <dd className="num">{b.host}</dd>
                <dt>Loopback only</dt>
                <dd>{String(b.loopback_only)}</dd>
                <dt>Managed by app</dt>
                <dd>{String(b.managed_by_application)}</dd>
                <dt>Requires manual cmd</dt>
                <dd>{String(b.requires_manual_cmd)}</dd>
              </dl>
            </div>
            <div className="grid-half" style={{ marginTop: 14 }}>
              <dl className="kv">
                <dt>Last successful request</dt>
                <dd className="num">{formatUtcTime(b.last_successful_request)}</dd>
                <dt>Observed</dt>
                <dd>{String(b.observed)}</dd>
              </dl>
              <div>
                <div className="card-title">LAST ERROR</div>
                <div
                  className="num"
                  style={{
                    fontSize: "var(--fs-xs)",
                    color: b.last_error && b.last_error !== "none" ? "var(--down)" : "var(--text-muted)",
                    overflowWrap: "anywhere",
                    maxHeight: 90,
                    overflowY: "auto",
                    background: "var(--bg-surface-2)",
                    borderRadius: "var(--radius-sm)",
                    padding: 10,
                    border: "1px solid var(--border-subtle)",
                  }}
                >
                  {b.last_error || "(none)"}
                </div>
              </div>
            </div>
            {b.mt5_ready === false ? (
              <div className="conn-banner conn-banner--warn" style={{ marginTop: 14 }} role="alert">
                <span className="conn-banner__title">MT5 DATA NOT CONFIRMED</span>
                <span className="conn-banner__msg">
                  The backend cannot confirm MT5 terminal data availability. The terminal must be
                  running, logged in, and the broker symbol must resolve. Until then candle and
                  snapshot requests will return "unavailable" — this terminal will not fabricate a
                  substitute series.
                </span>
              </div>
            ) : null}
          </>
        )}
      />
      <div style={{ height: 56 }} />
    </>
  );
}
