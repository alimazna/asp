import { useApp } from "lib/appContext";
import { DEFAULT_LOCAL_API } from "lib/appContext";
import { getBaseUrl } from "api/client";

const REFRESH_OPTIONS: Array<{ id: "normal" | "slow" | "paused"; label: string; hint: string }> = [
  { id: "normal", label: "Normal", hint: "analysis ~5s · health ~10s (desktop-app guidance)" },
  { id: "slow", label: "Slow (×3)", hint: "gentler on low-power devices and metered connections" },
  { id: "paused", label: "Paused", hint: "no background polling; manual retry only" },
];

export default function SettingsPage() {
  const { theme, setTheme, refresh, setRefresh, apiConfigured, demoMode } = useApp();
  const baseUrl = getBaseUrl();

  return (
    <>
      <h2 className="page-heading">Settings</h2>
      <p className="page-sub">
        Presentation and polling preferences only. There are no controls here that change backend
        analysis rules, risk policy, or execution behaviour — none are provided by the API.
      </p>

      <div className="settings-grid">
        <section className="card">
          <div className="card-title">THEME</div>
          <div className="seg" role="group" aria-label="Theme" style={{ alignSelf: "flex-start" }}>
            <button type="button" aria-pressed={theme === "dark"} onClick={() => setTheme("dark")}>
              Dark
            </button>
            <button type="button" aria-pressed={theme === "light"} onClick={() => setTheme("light")}>
              Light
            </button>
          </div>
          <div className="hint muted" style={{ fontSize: "var(--fs-2xs)", marginTop: 8 }}>
            Dark is the default, per the ASTRA visual identity. Preference is stored locally.
          </div>
        </section>

        <section className="card">
          <div className="card-title">BACKGROUND REFRESH</div>
          {REFRESH_OPTIONS.map((o) => (
            <label className="radio-row" key={o.id}>
              <input
                type="radio"
                name="refresh"
                checked={refresh === o.id}
                onChange={() => setRefresh(o.id)}
              />
              <span>
                <strong style={{ color: "var(--text-primary)" }}>{o.label}</strong>
                <span className="muted"> — {o.hint}</span>
              </span>
            </label>
          ))}
        </section>

        <section className="card">
          <div className="card-title">API CONFIGURATION (ENV)</div>
          <div className="field">
            <label>Active base URL</label>
            <div className="value-input" style={{ opacity: 0.9 }}>
              {apiConfigured ? baseUrl : "(not configured)"}
            </div>
            <div className="hint">
              Set with <code>VITE_API_BASE_URL</code> at build/dev time — the browser cannot safely
              hold remote service configuration that differs per deployment. Local default:{" "}
              <code>{DEFAULT_LOCAL_API}</code>
            </div>
          </div>
          <div className="field">
            <label>Demo mode</label>
            <div className="value-input" style={{ opacity: 0.9 }}>
              {demoMode ? "ENABLED (VITE_DEMO_MODE=1)" : "OFF"}
            </div>
            <div className="hint">
              When <code>VITE_DEMO_MODE=1</code> the app consumes built-in demo fixtures that are
              always labelled <strong>DEMO DATA</strong> in the UI. Real API responses are never
              silently mixed with demo data.
            </div>
          </div>
        </section>

        <section className="card">
          <div className="card-title">SAFETY</div>
          <ul className="muted" style={{ fontSize: "var(--fs-sm)", paddingLeft: 18, margin: 0 }}>
            <li>Live trading is never authorised and no switch for it exists.</li>
            <li>The only backend write command is the allow-listed POST /command (notify, request_approval).</li>
            <li>No broker credentials, service keys, or admin tokens are held in this app.</li>
            <li>All market data comes from the AURA API; nothing is cached as current when polls fail.</li>
          </ul>
        </section>
      </div>
      <div style={{ height: 56 }} />
    </>
  );
}
