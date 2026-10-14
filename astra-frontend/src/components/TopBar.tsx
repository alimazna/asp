import { useEffect, useState } from "react";
import { Chip } from "./Chip";
import { useApp } from "lib/appContext";

function useUtcClock() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(t);
  }, []);
  return now;
}

export function TopBar({ onMenu }: { onMenu: () => void }) {
  const { connection, systemState, demoMode, theme, setTheme } = useApp();
  const now = useUtcClock();
  const p = (n: number) => String(n).padStart(2, "0");
  const hh = p(now.getUTCHours());
  const mm = p(now.getUTCMinutes());
  const ss = p(now.getUTCSeconds());

  const connLabel =
    connection.state === "CONNECTED"
      ? "API ONLINE"
      : connection.state === "OFFLINE"
        ? "OFFLINE"
        : connection.state === "NOT_CONFIGURED"
          ? "NOT CONFIGURED"
          : "CONNECTING";

  const connTitle =
    connection.state === "OFFLINE"
      ? (connection.detail ?? "backend unreachable")
      : connection.detail ?? undefined;

  const mode = systemState?.mode ?? null;
  // SHADOW ONLY is always displayed (mission §7). While the backend state is
  // unknown we still assert the mode this terminal is built for; if the backend
  // ever reports shadow_only === false we surface that as a critical warning
  // rather than quietly dropping the guarantee.
  const liveModeReported = systemState !== null && systemState.shadow_only === false;

  return (
    <header className="topbar">
      <button
        className="icon-btn burger"
        onClick={onMenu}
        aria-label="Open navigation menu"
        type="button"
      >
        ☰
      </button>
      <a className="topbar__brand" href="/" aria-label="ASTRA home">
        <img src="/astra-mark.svg" alt="" />
        <span>ASTRA</span>
      </a>
      <div className="topbar__title" style={{ display: "none" }}>
        <span className="t" />
        <span className="s" />
      </div>
      <div className="row" style={{ flex: 1, minWidth: 0, justifyContent: "flex-end" }}>
        <Chip
          tone={liveModeReported ? "critical" : demoMode ? "warn" : "accent"}
          title={
            liveModeReported
              ? "Backend reports shadow_only=false. This terminal performs no order routing, but the flag is not what this UI expects — verify the backend configuration."
              : demoMode
                ? "DEMO DATA mode — fixtures only, no live backend. All activity is shadow simulation; there is no live trading."
                : "All displayed activity is shadow simulation. There is no live trading."
          }
        >
          SHADOW ONLY
        </Chip>
        {liveModeReported ? <Chip tone="critical">LIVE FLAG SET</Chip> : null}
        <Chip
          tone={connection.tone}
          pulse={connection.state === "CONNECTED"}
          title={connTitle}
        >
          {connLabel}
        </Chip>
        <span className="topbar__clock" title="UTC clock">
          <span aria-hidden>◷</span>
          {hh}:{mm}:{ss}
          <span className="z">Z</span>
        </span>
        <button
          className="icon-btn"
          type="button"
          aria-label={`Switch to ${theme === "dark" ? "light" : "dark"} theme`}
          onClick={() => setTheme(theme === "dark" ? "light" : "dark")}
        >
          {theme === "dark" ? "☀" : "☾"}
        </button>
      </div>
      {mode && mode !== "SHADOW" ? (
        <span className="topbar__clock" title="system mode">
          {mode}
        </span>
      ) : null}
    </header>
  );
}
