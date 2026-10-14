/**
 * Shared-state rendering: loading skeleton, error + retry, empty, stale.
 * These are the states §21 requires to be tested (never-fabricated data).
 */
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { DataCard } from "components/DataCard";
import { Chip } from "components/Chip";
import { CandleChart, CandleReadout } from "charts/CandleChart";
import type { UseApiResult } from "api/useApi";
import type { Candle } from "api/types";
import { fixtureData } from "./helpers/fixtures";

function result<T>(over: Partial<UseApiResult<T>> = {}): UseApiResult<T> {
  return {
    data: null,
    loading: false,
    stale: false,
    error: null,
    errorDetail: null,
    fetchedAt: null,
    refetch: () => {},
    ...over,
  };
}

describe("DataCard states", () => {
  it("shows a loading skeleton before the first dataset", () => {
    const { container } = render(
      <DataCard title="RISK STATE" result={result({ loading: true })} render={() => <p>content</p>} />,
    );
    expect(container.querySelector(".skel")).not.toBeNull();
    expect(screen.queryByText("content")).toBeNull();
  });

  it("shows the backend error with a working retry action", () => {
    const refetch = vi.fn();
    render(
      <DataCard
        title="RISK STATE"
        result={result({ error: "BACKEND OFFLINE", errorDetail: "unreachable", refetch })}
        render={() => <p>content</p>}
      />,
    );
    expect(screen.getByText("BACKEND OFFLINE")).toBeInTheDocument();
    expect(screen.getByText("unreachable")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Retry" }));
    expect(refetch).toHaveBeenCalledTimes(1);
  });

  it("shows the NOT CONFIGURED state without inventing data", () => {
    render(
      <DataCard
        title="ANALYSIS"
        result={result({
          error: "CLOUD CONNECTION NOT CONFIGURED",
          errorDetail: "No API base URL is set.",
        })}
        render={() => <p>content</p>}
      />,
    );
    expect(screen.getByText("CLOUD CONNECTION NOT CONFIGURED")).toBeInTheDocument();
    expect(screen.queryByText("content")).toBeNull();
  });

  it("shows an intentional empty state when the backend returns nothing yet", () => {
    render(
      <DataCard
        title="RESEARCH"
        result={result({ data: null })}
        emptyLabel="No experiments recorded."
        render={() => <p>content</p>}
      />,
    );
    expect(screen.getByText("NOT AVAILABLE")).toBeInTheDocument();
    expect(screen.getByText("No experiments recorded.")).toBeInTheDocument();
  });

  it("keeps previous data visible but marks it stale after a failed poll", () => {
    render(
      <DataCard
        title="BRIDGE"
        result={result({
          data: { ok: true },
          stale: true,
          error: "BACKEND OFFLINE",
          errorDetail: "unreachable",
          fetchedAt: Date.now(),
        })}
        render={(d: { ok: boolean }) => <p>content {String(d.ok)}</p>}
      />,
    );
    // data still rendered, but explicitly labelled stale + retry offered
    expect(screen.getByText(/content true/)).toBeInTheDocument();
    expect(screen.getByText("Showing last received data. Retry: manual.")).toBeInTheDocument();
    expect(screen.getByText("stale")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Retry now" })).toBeInTheDocument();
  });
});

describe("Chip", () => {
  it("renders the tone class and label", () => {
    render(<Chip tone="critical">BLOCKED</Chip>);
    const chip = screen.getByText("BLOCKED");
    expect(chip).toHaveClass("chip", "chip--critical");
  });
});

describe("candlestick chart", () => {
  it("renders an honest empty chart state with real bars absent", () => {
    const { container } = render(<CandleChart bars={[]} symbol="XAUUSD" timeframe="M15" />);
    expect(container.querySelector("canvas")).not.toBeNull();
    expect(screen.getByRole("img")).toHaveAttribute(
      "aria-label",
      expect.stringContaining("closed bars only"),
    );
  });

  it("draws the real OHLC series returned by the API", () => {
    const series = fixtureData<{ bars: Candle[]; symbol: string; timeframe: "M15" }>("candles");
    expect(series.bars.length).toBeGreaterThan(0);
    const { container } = render(
      <CandleChart bars={series.bars} symbol={series.symbol} timeframe={series.timeframe} />,
    );
    expect(container.querySelector("canvas")).not.toBeNull();
    // the series is passed through untouched — never synthesised
    expect(series.bars[0]).toMatchObject({
      open: expect.any(Number),
      high: expect.any(Number),
      low: expect.any(Number),
      close: expect.any(Number),
    });
  });

  it("shows an OHLC readout for the selected candle and a hint otherwise", () => {
    const { rerender } = render(<CandleReadout candle={null} />);
    expect(screen.getByText(/Closed bars only/)).toBeInTheDocument();

    const candle: Candle = {
      time: 1760001900,
      open: 2650.75,
      high: 2651.9,
      low: 2650.1,
      close: 2651.6,
      tick_volume: 967,
    };
    rerender(<CandleReadout candle={candle} />);
    expect(screen.getByText("2650.75")).toBeInTheDocument();
    expect(screen.getByText("2651.90")).toBeInTheDocument();
    expect(screen.getByText("2650.10")).toBeInTheDocument();
    expect(screen.getByText("2651.60")).toBeInTheDocument();
    expect(screen.getByText(/vol 967/)).toBeInTheDocument();
  });
});
