/**
 * Command validation (§16): the only write surface is POST /command with the
 * allow-list `notify` | `request_approval`, always attributed by `actor`.
 * No live-trading control exists and no execution command is offered.
 */
import { beforeEach, describe, expect, it } from "vitest";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import GovernancePage from "pages/Governance";
import { AppProvider } from "lib/appContext";
import { configure } from "api/client";
import { installMockApi, type MockApi } from "./helpers/mockApi";

const BASE = "http://127.0.0.1:8790/api/v1/";

function renderGovernance() {
  return render(
    <AppProvider>
      <GovernancePage />
    </AppProvider>,
  );
}

describe("governance command surface", () => {
  let mock: MockApi;

  beforeEach(() => {
    configure(BASE);
    mock = installMockApi();
  });

  it("reports live_trading_authorised=false from the backend", async () => {
    renderGovernance();
    await waitFor(() =>
      expect(screen.getByText(/live_trading_authorised = false/)).toBeInTheDocument(),
    );
  });

  it("refuses to send a command without actor attribution", async () => {
    renderGovernance();
    const button = await screen.findByRole("button", { name: "Send notify" });
    fireEvent.click(button);

    expect(
      await screen.findByText("Actor attribution is required (400 actor_required)."),
    ).toBeInTheDocument();
    // nothing was sent to the backend
    expect(mock.calls.filter((c) => c.method === "POST")).toHaveLength(0);
  });

  it("posts an allow-listed command with the actor and shows the accepted id", async () => {
    renderGovernance();

    fireEvent.change(await screen.findByLabelText("Actor (required)"), {
      target: { value: "operator-7" },
    });
    fireEvent.change(screen.getByLabelText("Payload (free text)"), {
      target: { value: "raise risk cap" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Request approval" }));

    await waitFor(() => expect(screen.getByText("ACCEPTED (202)")).toBeInTheDocument());
    expect(screen.getByText(/approval-1/)).toBeInTheDocument();

    const post = mock.calls.find((c) => c.method === "POST");
    expect(post).toBeDefined();
    expect(post?.url).toBe(`${BASE}command`);
    expect(JSON.parse(post?.body ?? "{}")).toEqual({
      command: "request_approval",
      actor: "operator-7",
      payload: "raise risk cap",
    });
  });

  it("surfaces a backend rejection instead of a fake success", async () => {
    installMockApi((url) =>
      url.endsWith("/command")
        ? {
            status: 403,
            ok: false,
            text: async () =>
              JSON.stringify({
                error: true,
                code: "command_not_permitted",
                message: "live execution is prohibited",
              }),
          }
        : undefined,
    );

    renderGovernance();
    fireEvent.change(await screen.findByLabelText("Actor (required)"), {
      target: { value: "operator-7" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Send notify" }));

    await waitFor(() => expect(screen.getByText("COMMAND REJECTED")).toBeInTheDocument());
  });

  it("offers no live-trading or execution control", async () => {
    const { container } = renderGovernance();
    const buttons = [...container.querySelectorAll("button")].map((b) =>
      (b.textContent ?? "").toLowerCase(),
    );
    expect(buttons.some((t) => /live|execute|order|trade/i.test(t))).toBe(false);
    expect(container.textContent).toContain("no path in this UI");
  });
});
