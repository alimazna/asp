/**
 * Fixture loader for the frozen v1 contract fixtures copied from the AURA
 * repository (`scripts/contract/fixtures_api_v1`, T22). Tests assert against
 * the real payloads the backend produces — never hand-written "expected data".
 *
 * Uses Vite's JSON globbing so the browser/node type surface stays limited to
 * `vite/client` (no node builtins, no extra @types dependency).
 */

export type FixtureKind = "valid" | "invalid" | "semantic" | "errors";

const MODULES = import.meta.glob<unknown>(
  "../../scripts/contract/fixtures_api_v1/**/*.json",
  { eager: true, import: "default" },
);

export const FIXTURE_ROOT_PREFIX = "../../scripts/contract/fixtures_api_v1/";

export function fixtureNames(kind: FixtureKind): string[] {
  const prefix = `${FIXTURE_ROOT_PREFIX}${kind}/`;
  return Object.keys(MODULES)
    .filter((k) => k.startsWith(prefix))
    .map((k) => k.slice(prefix.length).replace(/\.json$/, ""))
    .sort();
}

export function fixtureBody(kind: FixtureKind, name: string): unknown {
  const key = `${FIXTURE_ROOT_PREFIX}${kind}/${name}.json`;
  const body = MODULES[key];
  if (body === undefined) {
    throw new Error(`unknown contract fixture: ${kind}/${name}`);
  }
  return body;
}

/** Decoded `data` of a valid fixture. */
export function fixtureData<T = unknown>(name: string): T {
  const body = fixtureBody("valid", name) as { data: T };
  return body.data;
}

export type ValidFixtureName =
  | "analysis_history"
  | "analysis_latest"
  | "analysis_latest_calibrated"
  | "audit_recent"
  | "bridge_status"
  | "candles"
  | "context_latest"
  | "governance_status"
  | "health"
  | "health_v1"
  | "research_status"
  | "risk_latest"
  | "shadow_outcomes"
  | "shadow_positions"
  | "system_state"
  | "timeframe_snapshot"
  | "timeframes";

/** URL fragment → valid fixture name, mirroring the frozen route families. */
export const ROUTE_FIXTURES: Array<{ match: RegExp; name: ValidFixtureName }> = [
  { match: /\/health\/v1(\?|$)/, name: "health_v1" },
  { match: /\/health(\?|$)/, name: "health" },
  { match: /\/system\/state(\?|$)/, name: "system_state" },
  { match: /\/timeframes\/[A-Za-z0-9]+\/snapshot(\?|$)/, name: "timeframe_snapshot" },
  { match: /\/timeframes(\?|$)/, name: "timeframes" },
  { match: /\/candles(\?|$)/, name: "candles" },
  { match: /\/analysis\/latest(\?|$)/, name: "analysis_latest" },
  { match: /\/analysis\/history(\?|$)/, name: "analysis_history" },
  { match: /\/context\/latest(\?|$)/, name: "context_latest" },
  { match: /\/risk\/latest(\?|$)/, name: "risk_latest" },
  { match: /\/shadow\/positions(\?|$)/, name: "shadow_positions" },
  { match: /\/shadow\/outcomes(\?|$)/, name: "shadow_outcomes" },
  { match: /\/research\/status(\?|$)/, name: "research_status" },
  { match: /\/governance\/status(\?|$)/, name: "governance_status" },
  { match: /\/bridge\/status(\?|$)/, name: "bridge_status" },
  { match: /\/audit\/recent(\?|$)/, name: "audit_recent" },
];
