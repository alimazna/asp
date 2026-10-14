import { useMemo, useState } from "react";
import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { ShadowBanner } from "components/Banners";
import { POLL, useApi } from "api/useApi";
import { getShadowOutcomes, getShadowPositions } from "api/endpoints";
import type { ShadowOutcome, ShadowPosition } from "api/types";
import {
  UNAVAILABLE,
  directionTone,
  fmtNumber,
  fmtPrice,
  formatUtcTime,
} from "api/honors";

import type { Tone } from "api/honors";

function openStateTone(state: string): Tone {
  if (state === "OPEN") return "up";
  if (state.startsWith("CLOSED_TARGET")) return "up";
  if (state.startsWith("CLOSED_STOP")) return "down";
  return "neutral";
}

export default function ShadowPage() {
  const [tab, setTab] = useState<"open" | "closed" | "outcomes">("open");

  const positions = useApi({
    fetcher: (s) => getShadowPositions({ signal: s }),
    intervalMs: POLL.health,
  });
  const outcomes = useApi({
    fetcher: (s) => getShadowOutcomes({ signal: s }),
    intervalMs: POLL.slow,
  });

  const open = useMemo(
    () => (positions.data ?? []).filter((p) => p.state === "OPEN"),
    [positions.data],
  );
  const closed = useMemo(
    () => (positions.data ?? []).filter((p) => p.state !== "OPEN"),
    [positions.data],
  );

  // No aggregate performance metrics are computed from incomplete records.
  const completeOutcomes = (outcomes.data ?? []).filter(
    (o) => o.realized_pnl !== null && o.realized_pnl !== undefined,
  );

  return (
    <>
      <h2 className="page-heading">Shadow Trades &amp; Outcomes</h2>
      <p className="page-sub">
        Simulation-only records from /shadow/positions and /shadow/outcomes. Nothing here is a real
        broker execution; unrealised or incomplete records are shown without derived metrics.
      </p>
      <ShadowBanner />

      <div className="seg" role="group" aria-label="Shadow view" style={{ marginBottom: 12, alignSelf: "flex-start" }}>
        <button type="button" aria-pressed={tab === "open"} onClick={() => setTab("open")}>
          Open ({open.length})
        </button>
        <button type="button" aria-pressed={tab === "closed"} onClick={() => setTab("closed")}>
          Closed ({closed.length})
        </button>
        <button type="button" aria-pressed={tab === "outcomes"} onClick={() => setTab("outcomes")}>
          Outcomes ({outcomes.data?.length ?? 0})
        </button>
      </div>

      {tab === "open" ? (
        <DataCard
          title={`OPEN SHADOW POSITIONS (${open.length})`}
          result={positions}
          render={() =>
            open.length === 0 ? (
              <EmptyState label="No open shadow positions." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Position</th>
                      <th>Dir</th>
                      <th>TF</th>
                      <th className="num">Entry</th>
                      <th className="num">Stop</th>
                      <th className="num">Target</th>
                      <th className="num">Lots</th>
                      <th>Opened</th>
                    </tr>
                  </thead>
                  <tbody>
                    {open.map((p: ShadowPosition) => (
                      <tr key={p.position_id}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{p.position_id}</td>
                        <td style={{ color: `var(--${directionTone(p.direction) === "up" ? "up" : directionTone(p.direction) === "down" ? "down" : "text-muted"})` }}>
                          {p.direction}
                        </td>
                        <td>{p.timeframe ?? UNAVAILABLE}</td>
                        <td className="num">{fmtPrice(p.entry_price)}</td>
                        <td className="num">{fmtPrice(p.stop_price)}</td>
                        <td className="num">{fmtPrice(p.target_price)}</td>
                        <td className="num">{fmtNumber(p.lots, 2)}</td>
                        <td style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(p.opened_at)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )
          }
        />
      ) : null}

      {tab === "closed" ? (
        <DataCard
          title={`CLOSED SHADOW POSITIONS (${closed.length})`}
          result={positions}
          render={() =>
            closed.length === 0 ? (
              <EmptyState label="No closed shadow positions yet." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Position</th>
                      <th>Dir</th>
                      <th>State</th>
                      <th className="num">Entry</th>
                      <th className="num">Exit</th>
                      <th className="num">P&L</th>
                      <th className="num">R</th>
                      <th>Reason</th>
                      <th>Closed</th>
                    </tr>
                  </thead>
                  <tbody>
                    {closed.map((p: ShadowPosition) => (
                      <tr key={p.position_id}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{p.position_id}</td>
                        <td>{p.direction}</td>
                        <td><Chip tone={openStateTone(p.state)}>{p.state.replace("CLOSED_", "")}</Chip></td>
                        <td className="num">{fmtPrice(p.entry_price)}</td>
                        <td className="num">{fmtPrice(p.exit_price)}</td>
                        <td className="num" style={{ color: (p.realized_pnl ?? 0) >= 0 ? "var(--up)" : "var(--down)" }}>
                          {p.realized_pnl ?? UNAVAILABLE}
                        </td>
                        <td className="num">{p.realized_r ?? UNAVAILABLE}</td>
                        <td className="muted">{p.close_reason ?? UNAVAILABLE}</td>
                        <td style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(p.closed_at)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )
          }
        />
      ) : null}

      {tab === "outcomes" ? (
        <DataCard
          title={`SHADOW OUTCOMES (${outcomes.data?.length ?? 0})`}
          result={outcomes}
          render={(all: ShadowOutcome[]) =>
            all.length === 0 ? (
              <EmptyState label="No recorded shadow outcomes yet." />
            ) : (
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Outcome</th>
                      <th>Class</th>
                      <th>Dir</th>
                      <th>TF</th>
                      <th className="num">Entry</th>
                      <th className="num">Exit</th>
                      <th className="num">P&L</th>
                      <th className="num">R</th>
                      <th className="num">Bars held</th>
                      <th>Recorded</th>
                    </tr>
                  </thead>
                  <tbody>
                    {all
                      .slice()
                      .reverse()
                      .slice(0, 100)
                      .map((o) => (
                        <tr key={o.outcome_id}>
                          <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{o.outcome_id}</td>
                          <td>
                            <Chip tone={o.outcome_class === "WIN" ? "up" : o.outcome_class === "LOSS" ? "down" : "neutral"}>
                              {o.outcome_class ?? "UNKNOWN"}
                            </Chip>
                          </td>
                          <td>{o.direction ?? UNAVAILABLE}</td>
                          <td>{o.timeframe ?? UNAVAILABLE}</td>
                          <td className="num">{fmtPrice(o.entry_price)}</td>
                          <td className="num">{fmtPrice(o.exit_price)}</td>
                          <td className="num" style={{ color: (o.realized_pnl ?? 0) >= 0 ? "var(--up)" : "var(--down)" }}>
                            {o.realized_pnl ?? UNAVAILABLE}
                          </td>
                          <td className="num">{o.realized_r ?? UNAVAILABLE}</td>
                          <td className="num">{o.bars_held ?? UNAVAILABLE}</td>
                          <td style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(o.recorded_at)}</td>
                        </tr>
                      ))}
                  </tbody>
                </table>
              </div>
            )
          }
        />
      ) : null}

      <div className="muted" style={{ fontSize: "var(--fs-2xs)", marginTop: 8 }}>
        {completeOutcomes.length} of {outcomes.data?.length ?? 0} outcomes carry complete realized
        P&L. No aggregate win-rate or expectancy is computed here — historical shadow results never
        imply future profitability.
      </div>
      <div style={{ height: 56 }} />
    </>
  );
}
