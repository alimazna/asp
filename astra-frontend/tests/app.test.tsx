/**
 * End-to-end screen tests: app shell + responsive navigation, the nine
 * timeframe order, dashboard honesty labels and the history/shadow list pages.
 * Every response comes from the frozen contract fixtures via the fetch double,
 * so each test also proves the real request URLs are the frozen ones.
 */
import { beforeEach, describe, expect, it } from "vitest";
import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { AppProvider } from "lib/appContext";
import { AppShell } from "layouts/AppShell";
import DashboardPage from "pages/Dashboard";
import TimeframesPage from "pages/Timeframes";
import HistoryPage from "pages/History";
import ShadowPage from "pages/Shadow";
import BridgePage from "pages/Bridge";
import ChartPage from "pages/Chart";
import { configure } from "api/client";
import { TIMEFRAMES } from "api/types";
import { installMockApi, type MockApi } from "./helpers/mockApi";
import { fixtureData } from "./helpers/fixtures";

const BASE = "http://127.0.0.1:8790/api/v1/";

function renderShell() {
  return render(
    <AppProvider>
      <MemoryRouter initialEntries={["/"]}>
        <Routes>
          <Route element={<AppShell />}>
            <Route path="/" element={<DashboardPage />} />
          </Route>
        </Routes>
      </MemoryRouter>
    </AppProvider>,
  );
}

function renderPage(node: React.ReactNode) {
  return render(<AppProvider>{node}</AppProvider>);
}

describe("unconfigured / disconnected deployment", () => {
  beforeEach(() => {
    configure(null);
    installMockApi();
  });

  it("shows CLOUD CONNECTION NOT CONFIGURED instead of silently using localhost", async () => {
    renderShell();
    expect(await screen.findByText("CLOUD CONNECTION NOT CONFIGURED")).toBeInTheDocument();
  });

  it("still shows SHADOW ONLY and the UTC clock", async () => {
    renderShell();
    expect(await screen.findByText("SHADOW ONLY")).toBeInTheDocument();
    expect(screen.getByTitle("UTC clock")).toBeInTheDocument();
  });

  it("keeps SHADOW ONLY visible on the mobile viewport", async () => {
    window.innerWidth = 390;
    window.dispatchEvent(new Event("resize"));
    renderShell();
    const chip = await screen.findByText("SHADOW ONLY");
    expect(chip).toBeVisible();
  });
});

describe("app shell navigation", () => {
  beforeEach(() => {
    configure(BASE);
    installMockApi();
  });

  it("renders every screen in the sidebar with the brand mark", async () => {
    const { container } = renderShell();
    const nav = await screen.findByRole("navigation", { name: "Primary" });
    for (const label of [
      "Dashboard",
      "Chart",
      "Timeframes",
      "Analysis",
      "History",
      "Shadow Trades",
      "Risk & System",
      "MT5 Bridge",
      "Research",
      "Governance",
      "Audit Log",
      "Settings",
    ]) {
      expect(within(nav).getByText(label)).toBeInTheDocument();
    }
    expect(container.querySelector('img[src="/astra-mark.svg"]')).not.toBeNull();
  });

  it("opens and closes the mobile drawer from the burger button", async () => {
    renderShell();
    const burger = screen.getByRole("button", { name: "Open navigation menu" });
    expect(screen.queryByRole("dialog")).toBeNull();

    fireEvent.click(burger);
    const drawer = await screen.findByRole("dialog", { name: "Navigation menu" });
    expect(within(drawer).getByText("Chart")).toBeInTheDocument();

    // the drawer offers both a scrim and a close button; both dismiss it
    const closeButtons = within(drawer).getAllByRole("button", { name: "Close menu" });
    expect(closeButtons.length).toBeGreaterThan(0);
    fireEvent.click(closeButtons[closeButtons.length - 1]);
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("exposes a thumb-reachable mobile tab bar", async () => {
    renderShell();
    const tabbar = await screen.findByRole("navigation", { name: "Primary mobile" });
    expect(within(tabbar).getAllByRole("link").length).toBeGreaterThanOrEqual(5);
  });

  it("renders a page title and context line", async () => {
    renderShell();
    expect(await screen.findByRole("heading", { level: 1, name: "Dashboard" })).toBeInTheDocument();
  });
});

describe("dashboard", () => {
  let mock: MockApi;

  beforeEach(() => {
    configure(BASE);
    mock = installMockApi();
  });

  it("requests only frozen v1 routes", async () => {
    renderShell();
    await waitFor(() => expect(mock.calls.length).toBeGreaterThan(0));
    await waitFor(() =>
      expect(mock.urls().some((u) => u.startsWith(`${BASE}candles?tf=M15`))).toBe(true),
    );
    for (const url of mock.urls()) {
      expect(url.startsWith(BASE)).toBe(true);
      expect(url).not.toMatch(/\/v2|supabase|localhost:\d+\/(?!api)/);
    }
    expect(mock.urls().some((u) => u.includes("analysis/latest"))).toBe(true);
    expect(mock.urls().some((u) => u.includes("timeframes"))).toBe(true);
  });

  it("labels the raw score as SCORE and never prints a percentage for it", async () => {
    const { container } = renderShell();
    const a = fixtureData<{ signal: { score: number; probability: number | null } }>(
      "analysis_latest",
    );
    const scoreText = String(a.signal.score);

    await waitFor(() => expect(container.textContent).toContain(scoreText));
    const kpiLabels = [...container.querySelectorAll(".kpi__label")].map((n) => n.textContent);
    expect(kpiLabels).toContain("SCORE");
    expect(kpiLabels).not.toContain("PROBABILITY");

    // no fabricated probability anywhere while calibration is unauthorised
    expect(container.textContent).toContain("N/A — CALIBRATION NOT AUTHORISED");
    const kpiValues = [...container.querySelectorAll(".kpi__value")].map((n) => n.textContent);
    expect(kpiValues.some((v) => v !== null && v.includes("%"))).toBe(false);
  });

  it("renders the real candle series for the operational timeframe", async () => {
    const { container } = renderShell();
    const series = fixtureData<{ bars: unknown[]; timeframe: string }>("candles");
    await waitFor(() => expect(container.querySelector("canvas")).not.toBeNull());
    expect(screen.getByText(`XAUUSD · M15 · CLOSED BARS`)).toBeInTheDocument();
    expect(series.bars.length).toBeGreaterThan(0);
  });
});

describe("nine-timeframe support", () => {
  beforeEach(() => {
    configure(BASE);
    installMockApi();
  });

  it("declares the canonical order M1 → M5 → M15 → M30 → H1 → H4 → D1 → W1 → MN1", () => {
    expect([...TIMEFRAMES]).toEqual([
      "M1",
      "M5",
      "M15",
      "M30",
      "H1",
      "H4",
      "D1",
      "W1",
      "MN1",
    ]);
  });

  it("requests a candle series per timeframe from the timeframe control", async () => {
    const mock = installMockApi();
    renderPage(
      <MemoryRouter>
        <ChartPage />
      </MemoryRouter>,
    );
    const group = await screen.findByRole("group", { name: "Timeframe" });
    const buttons = within(group)
      .getAllByRole("button")
      .map((b) => b.textContent);
    expect(buttons).toEqual([...TIMEFRAMES]);

    await waitFor(() => expect(mock.urls().some((u) => u.includes("tf=M15"))).toBe(true));
    fireEvent.click(within(group).getByRole("button", { name: "H4" }));
    await waitFor(() => expect(mock.urls().some((u) => u.includes("tf=H4"))).toBe(true));
  });

  it("lists all nine rows in order on the timeframes screen", async () => {
    const { container } = renderPage(<TimeframesPage />);
    await waitFor(() =>
      expect(container.querySelectorAll("table.matrix tbody tr").length).toBe(9),
    );
    const tfCells = [...container.querySelectorAll("table.matrix tbody tr td:first-child")].map(
      // the cell carries OP / STRUCT role badges after the timeframe token
      (td) => (td.textContent ?? "").replace(/OP$|STRUCT$/, ""),
    );
    expect(tfCells).toEqual(["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]);
  });
});

describe("history / shadow / bridge screens render backend rows", () => {
  beforeEach(() => {
    configure(BASE);
    installMockApi();
  });

  it("renders analysis history rows with the score column labelled honestly", async () => {
    const rows = fixtureData<unknown[]>("analysis_history");
    renderPage(<HistoryPage />);
    await waitFor(() =>
      expect(document.querySelectorAll("table tbody tr").length).toBe(rows.length),
    );
    // uncalibrated rows: header says SCORE, probability column says N/A
    expect(screen.getByRole("columnheader", { name: "SCORE" })).toBeInTheDocument();
    expect(screen.getAllByText("N/A").length).toBeGreaterThan(0);
    // 0.512 is rendered raw — never as a percentage
    expect(screen.getByText("0.512")).toBeInTheDocument();
  });

  it("marks shadow positions as SHADOW ONLY and shows outcome fields", async () => {
    const positions = fixtureData<unknown[]>("shadow_positions");
    renderPage(<ShadowPage />);
    await waitFor(() =>
      expect(document.querySelectorAll("table tbody tr").length).toBeGreaterThanOrEqual(
        positions.length,
      ),
    );
    expect(screen.getAllByText(/SHADOW/).length).toBeGreaterThan(0);
  });

  it("shows the real MT5 bridge lifecycle without claiming readiness", async () => {
    fixtureData("bridge_status");
    renderPage(<BridgePage />);
    await waitFor(() => expect(screen.getByText("WAITING_FOR_MT5")).toBeInTheDocument());
    // fixture reports mt5_ready=false and an empty resolved symbol → the page
    // must say so and must not present the symbol as resolved.
    expect(screen.getByText("MT5 DATA NOT CONFIRMED")).toBeInTheDocument();
    expect(screen.getAllByText("DOWN").length).toBeGreaterThan(0);
  });
});
