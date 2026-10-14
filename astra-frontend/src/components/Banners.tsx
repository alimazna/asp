import { useApp } from "lib/appContext";
import { DEMO_MODE } from "api/client";

export function ConnectionBanner() {
  const { connection, apiConfigured } = useApp();
  const configuredMsg =
    "No API base URL is set (VITE_API_BASE_URL). Point this build at the AURA backend API, e.g. http://127.0.0.1:8790/api/v1/ — see .env.example. Until then no market data can be displayed and none is invented.";
  const offlineMsg =
    "The AURA backend API is unreachable. The backend (and MT5 bridge) must be running — locally that means the ASTRA desktop host or scripts/mock_api.py. Retrying in the background; the data below is absent, never fabricated.";
  if (connection.state === "NOT_CONFIGURED") {
    return (
      <div className="conn-banner conn-banner--err" role="alert">
        <span className="conn-banner__title">CLOUD CONNECTION NOT CONFIGURED</span>
        <span className="conn-banner__msg">{configuredMsg}</span>
      </div>
    );
  }
  if (connection.state === "OFFLINE") {
    return (
      <div className="conn-banner conn-banner--err" role="alert">
        <span className="conn-banner__title">BACKEND OFFLINE</span>
        <span className="conn-banner__msg">{connection.detail ?? offlineMsg}</span>
      </div>
    );
  }
  if (!apiConfigured) {
    return (
      <div className="conn-banner conn-banner--warn" role="alert">
        <span className="conn-banner__title">CLOUD CONNECTION NOT CONFIGURED</span>
        <span className="conn-banner__msg">{configuredMsg}</span>
      </div>
    );
  }
  return null;
}

export function ShadowBanner({ demoMode }: { demoMode?: boolean }) {
  const isDemo = demoMode ?? DEMO_MODE;
  return (
    <div className="disclaimer" role="note">
      <strong style={{ letterSpacing: "0.06em" }}>
        {isDemo ? "DEMO DATA — " : ""}SHADOW ONLY.
      </strong>{" "}
      Every displayed position, level and statistic is a shadow simulation produced by the
      AURA backend in shadow mode. Nothing is executed at a broker and nothing here is live
      trading. Decision support only — not financial advice.
    </div>
  );
}
