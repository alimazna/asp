import { type Tone } from "api/honors";

export function Chip({
  tone = "neutral",
  children,
  pulse,
  title,
}: {
  tone?: Tone;
  children: React.ReactNode;
  pulse?: boolean;
  title?: string;
}) {
  const cls =
    tone === "up"
      ? "chip--up"
      : tone === "down"
        ? "chip--down"
        : tone === "warn"
          ? "chip--warn"
          : tone === "accent"
            ? "chip--accent"
            : tone === "critical"
              ? "chip--critical"
              : "chip--neutral";
  return (
    <span className={`chip ${cls}`} title={title}>
      <span className={`dot dot--${tone} ${pulse ? "pulse" : ""}`} aria-hidden />
      {children}
    </span>
  );
}

export function Dot({ tone = "neutral", pulse }: { tone?: Tone; pulse?: boolean }) {
  return <span className={`dot dot--${tone} ${pulse ? "pulse" : ""}`} aria-hidden />;
}
