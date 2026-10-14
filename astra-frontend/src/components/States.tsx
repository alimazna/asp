import "styles/pages.css";

export function SkelCard({ height }: { height?: number }) {
  return <div className="skel skel--card" style={height ? { height } : undefined} />;
}

export function SkelRows({ rows = 5 }: { rows?: number }) {
  return (
    <div className="stack-sm" style={{ padding: "8px 0" }}>
      {Array.from({ length: rows }).map((_, i) => (
        <div key={i} className="skel" style={{ height: 16, width: `${100 - i * 9}%` }} />
      ))}
    </div>
  );
}

export function StateBlock({
  icon,
  title,
  message,
  action,
}: {
  icon?: string;
  title: string;
  message?: string;
  action?: React.ReactNode;
}) {
  return (
    <div className="state-block">
      {icon ? <div className="state-block__icon" aria-hidden>{icon}</div> : null}
      <div className="state-block__title">{title}</div>
      {message ? <div className="state-block__msg">{message}</div> : null}
      {action ? <div style={{ marginTop: 6 }}>{action}</div> : null}
    </div>
  );
}

export function EmptyState({ label = "No data available yet" }: { label?: string }) {
  return <StateBlock icon="◌" title="NOT AVAILABLE" message={label} />;
}

export function Unavailable({ label }: { label?: string }) {
  return (
    <StateBlock
      icon="∅"
      title="NOT AVAILABLE"
      message={label ?? "The backend does not currently provide this information."}
    />
  );
}

export function ErrorState({
  title,
  detail,
  onRetry,
}: {
  title: string;
  detail?: string | null;
  onRetry?: () => void;
}) {
  return (
    <StateBlock
      icon="⚠"
      title={title}
      message={detail ?? undefined}
      action={
        onRetry ? (
          <button className="btn btn--sm" onClick={onRetry} type="button">
            Retry
          </button>
        ) : undefined
      }
    />
  );
}
