import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react";
import { AURA_DEFAULT_LOCAL, DEMO_MODE, isConfigured } from "api/client";
import { useApi, POLL } from "api/useApi";
import { getHealth, getSystemState } from "api/endpoints";
import type { Health, SystemState } from "api/types";
import { serviceStateTone, type Tone } from "api/honors";

export type Theme = "dark" | "light";
export type RefreshInterval = "normal" | "slow" | "paused";

interface AppContextValue {
  theme: Theme;
  setTheme: (t: Theme) => void;
  refresh: RefreshInterval;
  setRefresh: (r: RefreshInterval) => void;
  demoMode: boolean;
  /** Base URL actually used (null = not configured). */
  apiConfigured: boolean;
  /** Live connection state from /health polling. */
  connection: {
    state: "CONNECTED" | "OFFLINE" | "NOT_CONFIGURED" | "CHECKING";
    tone: Tone;
    detail: string | null;
  };
  systemState: SystemState | null;
  health: Health | null;
  /** interval multiplier used by screens */
  pollScale: number;
}

const AppCtx = createContext<AppContextValue | null>(null);

const THEME_KEY = "astra.theme";
const REFRESH_KEY = "astra.refresh";

export function AppProvider({ children }: { children: ReactNode }) {
  const [theme, setThemeState] = useState<Theme>(() => {
    try {
      const saved = localStorage.getItem(THEME_KEY);
      if (saved === "light" || saved === "dark") return saved;
    } catch {
      /* storage unavailable */
    }
    return "dark";
  });
  const [refresh, setRefreshState] = useState<RefreshInterval>(() => {
    try {
      const saved = localStorage.getItem(REFRESH_KEY);
      if (saved === "slow" || saved === "paused" || saved === "normal") return saved;
    } catch {
      /* storage unavailable */
    }
    return "normal";
  });

  useEffect(() => {
    try {
      document.documentElement.setAttribute("data-theme", theme);
    } catch {
      /* noop */
    }
    try {
      localStorage.setItem(THEME_KEY, theme);
    } catch {
      /* noop */
    }
  }, [theme]);

  const setTheme = useCallback((t: Theme) => setThemeState(t), []);
  const setRefresh = useCallback((r: RefreshInterval) => {
    setRefreshState(r);
    try {
      localStorage.setItem(REFRESH_KEY, r);
    } catch {
      /* noop */
    }
  }, []);

  const configured = isConfigured();
  const demoMode = DEMO_MODE;

  const sys = useApi<SystemState>({
    fetcher: (signal) => getSystemState({ signal }),
    intervalMs: configured && refresh !== "paused" ? POLL.health * (refresh === "slow" ? 3 : 1) : 0,
    enabled: configured,
  });
  const health = useApi<Health>({
    fetcher: (signal) => getHealth({ signal }),
    intervalMs: configured && refresh !== "paused" ? POLL.health * (refresh === "slow" ? 3 : 1) : 0,
    enabled: configured,
  });

  const connection: AppContextValue["connection"] = useMemo(() => {
    if (!configured) {
      return {
        state: "NOT_CONFIGURED",
        tone: "critical",
        detail: "API base URL not configured",
      };
    }
    if (sys.error !== null || health.error !== null) {
      // Never present an unreachable backend as connected.
      return {
        state: "OFFLINE",
        tone: "down",
        detail: sys.errorDetail ?? health.errorDetail,
      };
    }
    if (sys.data !== null || health.data !== null) {
      const h = health.data;
      const agg = (h?.aggregate ?? "").toUpperCase();
      const tone: Tone = serviceStateTone(h ? agg : "STARTING");
      return {
        state: "CONNECTED",
        tone: tone === "neutral" ? "accent" : tone,
        detail: agg || null,
      };
    }
    return { state: "CHECKING", tone: "neutral", detail: null };
  }, [configured, sys.error, sys.errorDetail, health.error, health.errorDetail, sys.data, health.data]);

  const pollScale = refresh === "slow" ? 3 : 1;

  const value: AppContextValue = {
    theme,
    setTheme,
    refresh,
    setRefresh,
    demoMode,
    apiConfigured: configured,
    connection,
    systemState: sys.data,
    health: health.data,
    pollScale,
  };

  return <AppCtx.Provider value={value}>{children}</AppCtx.Provider>;
}

export function useApp(): AppContextValue {
  const v = useContext(AppCtx);
  if (v === null) throw new Error("useApp outside provider");
  return v;
}

export { AURA_DEFAULT_LOCAL as DEFAULT_LOCAL_API };
