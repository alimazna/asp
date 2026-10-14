import { NavLink, useLocation } from "react-router-dom";
import { useApp } from "lib/appContext";
import { DEFAULT_LOCAL_API } from "lib/appContext";
import { getBaseUrl } from "api/client";

export interface NavEntry {
  to: string;
  label: string;
  icon: string;
  group: string;
  mobile?: boolean;
}

export const NAV: NavEntry[] = [
  { to: "/", label: "Dashboard", icon: "◱", group: "MARKET", mobile: true },
  { to: "/chart", label: "Chart", icon: "◫", group: "MARKET", mobile: true },
  { to: "/timeframes", label: "Timeframes", icon: "⊞", group: "MARKET", mobile: true },
  { to: "/analysis", label: "Analysis", icon: "◎", group: "DECISIONS", mobile: true },
  { to: "/history", label: "History", icon: "☰", group: "DECISIONS" },
  { to: "/shadow", label: "Shadow Trades", icon: "◈", group: "DECISIONS" },
  { to: "/risk", label: "Risk & System", icon: "⛨", group: "OPS", mobile: true },
  { to: "/bridge", label: "MT5 Bridge", icon: "⇄", group: "OPS" },
  { to: "/research", label: "Research", icon: "⚗", group: "OPS" },
  { to: "/governance", label: "Governance", icon: "§", group: "OPS" },
  { to: "/audit", label: "Audit Log", icon: "❑", group: "OPS" },
  { to: "/settings", label: "Settings", icon: "⚙", group: "SYSTEM", mobile: true },
];

export function NavLinks({ onNavigate }: { onNavigate?: () => void }) {
  const groups = NAV.reduce<Record<string, NavEntry[]>>((acc, e) => {
    (acc[e.group] ??= []).push(e);
    return acc;
  }, {});
  return (
    <nav aria-label="Primary">
      {Object.entries(groups).map(([group, entries]) => (
        <div className="nav-group" key={group}>
          <div className="nav-group__label">{group}</div>
          {entries.map((e) => (
            <NavLink
              key={e.to}
              to={e.to}
              end={e.to === "/"}
              className={({ isActive }) => "nav-item" + (isActive ? " active" : "")}
              onClick={onNavigate}
            >
              <span className="ico" aria-hidden>
                {e.icon}
              </span>
              {e.label}
            </NavLink>
          ))}
        </div>
      ))}
    </nav>
  );
}

export function Sidebar() {
  const { apiConfigured, demoMode } = useApp();
  return (
    <aside className="sidebar">
      <NavLinks />
      <div className="sidebar__footer">
        <div className="row" style={{ gap: 6 }}>
          <span
            className="dot dot--warn"
            aria-hidden
          />
          <span>SHADOW ONLY — simulation, never live</span>
        </div>
        <div style={{ marginTop: 6 }}>
          {apiConfigured ? (
            <>
              API: {getBaseUrl() ?? DEFAULT_LOCAL_API}
              {demoMode ? " · DEMO DATA" : ""}
            </>
          ) : (
            "API: NOT CONFIGURED (VITE_API_BASE_URL)"
          )}
        </div>
        <div style={{ marginTop: 6 }}>ASTRA web v1.0 — API v1 schema 1.0</div>
      </div>
    </aside>
  );
}

export function MobileDrawer({ open, onClose }: { open: boolean; onClose: () => void }) {
  const { demoMode, apiConfigured } = useApp();
  if (!open) return null;
  return (
    <>
      <button className="scrim" aria-label="Close menu" onClick={onClose} type="button" />
      <div className="drawer" role="dialog" aria-label="Navigation menu">
        <div className="row spread" style={{ padding: "0 8px 12px" }}>
          <span style={{ fontWeight: 800, letterSpacing: "0.14em" }}>ASTRA</span>
          <button className="icon-btn" onClick={onClose} aria-label="Close menu" type="button">
            ✕
          </button>
        </div>
        <NavLinks onNavigate={onClose} />
        <div className="sidebar__footer">
          {apiConfigured ? "API configured" : "API: NOT CONFIGURED"} {demoMode ? "· DEMO DATA" : ""}
        </div>
      </div>
    </>
  );
}

export function MobileTabBar() {
  const mobile = NAV.filter((n) => n.mobile);
  return (
    <nav className="tabbar" aria-label="Primary mobile">
      {mobile.map((e) => (
        <NavLink
          key={e.to}
          to={e.to}
          end={e.to === "/"}
          className={({ isActive }) => (isActive ? "active" : "")}
        >
          <span className="ico" aria-hidden>
            {e.icon}
          </span>
          {e.label.split(" ")[0]}
        </NavLink>
      ))}
    </nav>
  );
}

export function useCurrentPageTitle(): string {
  const location = useLocation();
  const entry = NAV.find((n) => n.to === location.pathname);
  return entry?.label ?? "Dashboard";
}
