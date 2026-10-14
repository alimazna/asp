import React from "react";
import ReactDOM from "react-dom/client";
import { HashRouter, Route, Routes } from "react-router-dom";
import { AppProvider } from "lib/appContext";
import { AppShell } from "layouts/AppShell";
import DashboardPage from "pages/Dashboard";
import ChartPage from "pages/Chart";
import TimeframesPage from "pages/Timeframes";
import AnalysisPage from "pages/Analysis";
import HistoryPage from "pages/History";
import ShadowPage from "pages/Shadow";
import RiskPage from "pages/Risk";
import BridgePage from "pages/Bridge";
import ResearchPage from "pages/Research";
import GovernancePage from "pages/Governance";
import AuditPage from "pages/Audit";
import SettingsPage from "pages/Settings";
import "styles/tokens.css";
import "styles/global.css";
import "styles/shell.css";
import "styles/pages.css";

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <AppProvider>
      <HashRouter>
        <Routes>
          <Route element={<AppShell />}>
            <Route path="/" element={<DashboardPage />} />
            <Route path="/chart" element={<ChartPage />} />
            <Route path="/timeframes" element={<TimeframesPage />} />
            <Route path="/analysis" element={<AnalysisPage />} />
            <Route path="/history" element={<HistoryPage />} />
            <Route path="/shadow" element={<ShadowPage />} />
            <Route path="/risk" element={<RiskPage />} />
            <Route path="/bridge" element={<BridgePage />} />
            <Route path="/research" element={<ResearchPage />} />
            <Route path="/governance" element={<GovernancePage />} />
            <Route path="/audit" element={<AuditPage />} />
            <Route path="/settings" element={<SettingsPage />} />
            <Route
              path="*"
              element={
                <div className="card">
                  <div className="state-block">
                    <div className="state-block__title">PAGE NOT FOUND</div>
                    <div className="state-block__msg">
                      This route does not exist in the ASTRA web terminal.
                    </div>
                  </div>
                </div>
              }
            />
          </Route>
        </Routes>
      </HashRouter>
    </AppProvider>
  </React.StrictMode>,
);
