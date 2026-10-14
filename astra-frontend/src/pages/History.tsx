import { useMemo, useState } from "react";
import { Chip } from "components/Chip";
import { DataCard } from "components/DataCard";
import { EmptyState } from "components/States";
import { POLL, useApi } from "api/useApi";
import { getAnalysisHistory } from "api/endpoints";
import type { AnalysisRecord } from "api/types";
import { UNAVAILABLE, directionTone, formatUtcTime, scoreFieldLabel } from "api/honors";

const PAGE = 25;

export default function HistoryPage() {
  const [limit] = useState<number>(200);
  const [directionFilter, setDirectionFilter] = useState<string>("ALL");
  const [symbolFilter, setSymbolFilter] = useState<string>("ALL");
  const [page, setPage] = useState(0);

  const history = useApi({
    fetcher: (s) => getAnalysisHistory(limit, { signal: s }),
    intervalMs: POLL.slow, // history is slower-moving
    keepPreviousOnError: true,
  });

  const rows = history.data ?? [];

  const filtered = useMemo(() => {
    const base = rows;
    const byDirection = base.filter((r) => {
      if (directionFilter === "ALL") return true;
      const dir = (r.signal?.direction ?? "").toUpperCase();
      if (dir === directionFilter) return true;
      return dir === "LONG" && directionFilter === "UP";
    });
    const bySymbol = byDirection.filter(
      (r) => symbolFilter === "ALL" || r.symbol === symbolFilter,
    );
    return bySymbol;
  }, [rows, directionFilter, symbolFilter]);

  const pageCount = Math.max(1, Math.ceil(filtered.length / PAGE));
  const paged = filtered.slice(page * PAGE, (page + 1) * PAGE);
  const symbols = useMemo(
    () => Array.from(new Set(rows.map((r) => r.symbol))).filter(Boolean),
    [rows],
  );

  return (
    <>
      <h2 className="page-heading">Analysis History</h2>
      <p className="page-sub">
        Newest-first decisions from /analysis/history (limit {limit}). Per-entry context and levels
        are not persisted by the backend and display as {UNAVAILABLE}.
      </p>

      <div className="filters" style={{ marginBottom: 12 }}>
        <label className="muted" htmlFor="f-dir">Direction</label>
        <select id="f-dir" value={directionFilter} onChange={(e) => { setDirectionFilter(e.target.value); setPage(0); }}>
          {["ALL", "UP", "DOWN", "NONE"].map((d) => (
            <option key={d} value={d}>{d}</option>
          ))}
        </select>
        <label className="muted" htmlFor="f-sym">Symbol</label>
        <select id="f-sym" value={symbolFilter} onChange={(e) => { setSymbolFilter(e.target.value); setPage(0); }}>
          <option value="ALL">ALL</option>
          {symbols.map((s) => (
            <option key={s} value={s}>{s}</option>
          ))}
        </select>
        <span className="muted" style={{ fontSize: "var(--fs-2xs)" }}>
          {filtered.length} record{filtered.length === 1 ? "" : "s"} (backend returned {rows.length})
        </span>
      </div>

      <DataCard
        title="RECORDS"
        result={history}
        render={() =>
          filtered.length === 0 ? (
            <EmptyState
              label={
                rows.length === 0
                  ? "The backend has not persisted any analysis records yet."
                  : "No records match the selected filters."
              }
            />
          ) : (
            <>
              {/* Desktop table */}
              <div className="table-wrap">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Timestamp</th>
                      <th>Symbol</th>
                      <th>Direction</th>
                      <th className="num">
                        {scoreFieldLabel(
                          rows[0]?.signal ?? { probability_calibrated: false, probability: null },
                        )}
                      </th>
                      <th>Probability</th>
                      <th>Regime</th>
                      <th>Coverage</th>
                    </tr>
                  </thead>
                  <tbody>
                    {paged.map((r: AnalysisRecord, i: number) => (
                      <tr key={`${r.timestamp ?? "none"}-${i}`}>
                        <td className="num" style={{ fontSize: "var(--fs-2xs)" }}>{formatUtcTime(r.timestamp)}</td>
                        <td>{r.symbol}</td>
                        <td style={{ color: `var(--${directionTone(r.signal.direction) === "up" ? "up" : directionTone(r.signal.direction) === "down" ? "down" : "text-muted"})` }}>
                          {r.signal.direction}
                        </td>
                        <td className="num">{r.signal.score ?? UNAVAILABLE}</td>
                        <td>
                          {r.signal.probability_calibrated && r.signal.probability !== null ? (
                            `${(r.signal.probability * 100).toFixed(1)}%`
                          ) : (
                            <span className="muted" title="RULE C: calibration not authorised">
                              N/A
                            </span>
                          )}
                        </td>
                        <td>{r.context?.regime ?? UNAVAILABLE}</td>
                        <td>
                          <Chip tone={r.meta?.coverage_tier === "high" ? "up" : r.meta?.coverage_tier === "unknown" ? "neutral" : "warn"}>
                            {(r.meta?.coverage_tier ?? "unknown").toUpperCase()}
                          </Chip>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <div className="row spread" style={{ marginTop: 10 }}>
                <button
                  className="btn btn--sm"
                  type="button"
                  disabled={page === 0}
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                >
                  ‹ Newer
                </button>
                <span className="muted num" style={{ fontSize: "var(--fs-2xs)" }}>
                  page {page + 1} / {pageCount}
                </span>
                <button
                  className="btn btn--sm"
                  type="button"
                  disabled={page >= pageCount - 1}
                  onClick={() => setPage((p) => Math.min(pageCount - 1, p + 1))}
                >
                  Older ›
                </button>
              </div>
            </>
          )
        }
      />
      <div style={{ height: 56 }} />
    </>
  );
}
