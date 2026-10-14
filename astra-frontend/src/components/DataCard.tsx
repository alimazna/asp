import { Chip } from "./Chip";
import { ErrorState, SkelCard, StateBlock } from "./States";
import { ageText } from "api/honors";
import type { UseApiResult } from "api/useApi";

/**
 * Card that renders one of the four shared states based on a useApi result:
 * loading skeleton → error banner+retry → empty → content.
 * The freshness line is always truthful (never faked to look current).
 */
export function DataCard<T>({
  title,
  result,
  render,
  emptyLabel,
  emptyTitle,
  minHeight,
  showFreshness = true,
}: {
  title: string;
  result: UseApiResult<T>;
  render: (data: T) => React.ReactNode;
  emptyLabel?: string;
  emptyTitle?: string;
  minHeight?: number;
  showFreshness?: boolean;
}) {
  const {
    data,
    loading,
    error,
    errorDetail,
    stale,
    fetchedAt,
    refetch,
  } = result;
  return (
    <section className="card" style={minHeight ? { minHeight } : undefined} aria-busy={loading}>
      <div className="row spread" style={{ marginBottom: 0 }}>
        <h3 className="card-title" style={{ margin: 0 }}>
          {title}
        </h3>
        {showFreshness && fetchedAt !== null && !stale && !loading ? (
          <span className="chip chip--neutral" style={{ gap: 4 }}>
            <span className="dot dot--up" aria-hidden />
            {ageText(fetchedAt)}
          </span>
        ) : null}
        {showFreshness && stale ? (
          <span className="chip chip--warn" style={{ gap: 4 }}>
            <span className="dot dot--warn" aria-hidden />
            stale
          </span>
        ) : null}
      </div>
      {loading && data === null ? (
        <SkelCard />
      ) : error !== null && data === null ? (
        <ErrorState title={error} detail={errorDetail} onRetry={refetch} />
      ) : data === null ? (
        <StateBlock icon="◌" title={emptyTitle ?? "NOT AVAILABLE"} message={emptyLabel} />
      ) : (
        <>
          {stale && error !== null ? (
            <div
              className="conn-banner conn-banner--warn"
              style={{ padding: "6px 10px", marginBottom: 10, fontSize: "var(--fs-xs)" }}
            >
              <span className="conn-banner__title">{error}</span>
              <span className="conn-banner__msg" style={{ fontSize: "var(--fs-2xs)" }}>
                Showing last received data. Retry: manual.
              </span>
              <button className="btn btn--sm" onClick={refetch} type="button">
                Retry now
              </button>
            </div>
          ) : null}
          {render(data)}
        </>
      )}
    </section>
  );
}

export function KVRow({
  label,
  value,
  mono = true,
}: {
  label: string;
  value: React.ReactNode;
  mono?: boolean;
}) {
  return (
    <>
      <dt>{label}</dt>
      <dd className={mono ? "num" : undefined}>{value}</dd>
    </>
  );
}

/** Chip convenience re-export to keep screens terse. */
export { Chip };
