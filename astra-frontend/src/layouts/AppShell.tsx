import { useState } from "react";
import { Outlet } from "react-router-dom";
import { TopBar } from "components/TopBar";
import { MobileDrawer, MobileTabBar, Sidebar, useCurrentPageTitle } from "components/Sidebar";
import { ConnectionBanner } from "components/Banners";
import { useApp } from "lib/appContext";

export function AppShell() {
  const [menuOpen, setMenuOpen] = useState(false);
  const { connection, demoMode } = useApp();
  const showBanner =
    connection.state === "NOT_CONFIGURED" || connection.state === "OFFLINE";
  const pageTitle = useCurrentPageTitle();

  return (
    <div className="shell">
      <TopBar onMenu={() => setMenuOpen(true)} />
      <div className="shell__body">
        <Sidebar />
        <MobileDrawer open={menuOpen} onClose={() => setMenuOpen(false)} />
        <main className="main">
          {showBanner ? <ConnectionBanner /> : null}
          {demoMode ? (
            <div className="conn-banner conn-banner--warn" role="note">
              <span className="conn-banner__title">DEMO DATA</span>
              <span className="conn-banner__msg">
                This build is running in demo mode. Every value is a labelled fixture served by
                the local mock API — never a production market reading.
              </span>
            </div>
          ) : null}
          <div className="page-head">
            <h1 className="page-title">{pageTitle}</h1>
            <span className="page-context">
              XAUUSD · {demoMode ? "DEMO DATA" : "SHADOW ONLY"} · UTC
            </span>
          </div>
          <Outlet />
        </main>
      </div>
      <MobileTabBar />
    </div>
  );
}
