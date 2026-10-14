/**
 * ASTRA API v1 types — mirroring the frozen contract.
 * Source of truth: docs/architecture/API_V1_SCHEMA.json (T17/T30),
 * docs/frontend/FRONTEND_HANDOFF_GUIDE.md and
 * docs/frontend/ASTRA_FRONTEND_HANDOFF.md.
 *
 * Fields the backend does not know are `null` / `"UNKNOWN"`. The UI must
 * render those as unavailable — never as healthy, zero, or safe.
 */

/** Canonical timeframe order: M1 → M5 → M15 → M30 → H1 → H4 → D1 → W1 → MN1 */
export const TIMEFRAMES = [
  "M1",
  "M5",
  "M15",
  "M30",
  "H1",
  "H4",
  "D1",
  "W1",
  "MN1",
] as const;

export type Timeframe = (typeof TIMEFRAMES)[number];

/** Service states (src/foundation/ServiceState.h). */
export type ServiceState =
  | "STARTING"
  | "ONLINE"
  | "DEGRADED"
  | "OFFLINE"
  | "RECOVERING"
  | "PAUSED"
  | "BLOCKED"
  | "ERROR";

/** System modes (src/foundation/SystemMode.h). */
export type SystemMode =
  | "STARTING"
  | "RECOVERY"
  | "NORMAL"
  | "DEGRADED"
  | "SHADOW"
  | "PAUSED"
  | "MANUAL"
  | "EMERGENCY"
  | "HALTED";

export type DataQualityState =
  | "VALID"
  | "DEGRADED"
  | "INVALID"
  | "UNKNOWN"
  | "STALE"
  | "MISSING"
  | "OUT_OF_ORDER"
  | "DUPLICATE"
  | "INCOMPLETE";

export type FreshnessState = "FRESH" | "STALE" | "UNKNOWN";

export type Direction = "UP" | "DOWN" | "FLAT" | "NONE" | "UNKNOWN";

export type CoverageTier = "high" | "medium" | "low" | "unknown";

/** Success envelope: {"api":"v1","schema":"1.0","data":{...}} */
export interface Envelope<T> {
  api: string;
  schema: string;
  data: T;
}

/** Flat error object (not enveloped). */
export interface ApiErrorBody {
  error: true | "true";
  code: string;
  message: string;
}

/* ---------------------------------------------------------------- */
/* system/state                                                     */
/* ---------------------------------------------------------------- */
export interface SystemState {
  mode: SystemMode;
  shadow_only: boolean;
  ready: boolean;
  bridge_state: string;
  startup_stage: string;
  api?: string;
  schema?: string;
}

/* ---------------------------------------------------------------- */
/* health                                                           */
/* ---------------------------------------------------------------- */
export interface Health {
  aggregate: string;
  decision_grade_data: boolean;
  degraded_reasons: string[];
  observed_at: number | null;
  unknown_is_not_safe: boolean;
}

/** /health/v1 — frontend liveness surface. */
export interface HealthV1 {
  status: "ok" | "degraded" | "offline";
  bridge: "ok" | "degraded" | "offline";
  version: string;
  uptime_sec: number | null;
}

/* ---------------------------------------------------------------- */
/* timeframes                                                       */
/* ---------------------------------------------------------------- */
export interface CapabilityImpact {
  capability: string;
  impact: string;
  reason: string;
}

export interface Freshness {
  state: FreshnessState;
  is_fresh: boolean;
  last_update: number | null;
  age_millis: number | null;
  max_age_millis: number | null;
}

export interface TimeframeRow {
  timeframe: Timeframe;
  observed: boolean;
  has_closed_bar: boolean;
  quality: { state: string; decision_grade: boolean };
  decision_grade: boolean;
  freshness: Freshness | null;
  last_successful_update: number | null;
  last_closed_bar_open?: number | null;
  sequence?: number | null;
  capability_impact?: CapabilityImpact[];
}

export interface TimeframeSnapshot extends TimeframeRow {
  open?: number | null;
  high?: number | null;
  low?: number | null;
  close?: number | null;
  open_time?: number | null;
}

/* ---------------------------------------------------------------- */
/* candles                                                          */
/* ---------------------------------------------------------------- */
export interface Candle {
  time: number;
  open: number;
  high: number;
  low: number;
  close: number;
  tick_volume?: number | null;
  spread?: number | null;
  real_volume?: number | null;
}

export interface CandleSeries {
  bars: Candle[];
  timeframe: Timeframe;
  symbol: string;
  count: number;
  closed_only?: boolean;
  newest_closed_time?: number | null;
  freshness?: string;
}

/* ---------------------------------------------------------------- */
/* analysis (latest + history share one shape)                      */
/* ---------------------------------------------------------------- */
export interface AnalysisContext {
  regime: string;
  h4_bias: string;
  m15_trigger: string;
  mtf_agreement: number | null;
  volatility_state: string;
}

export interface AnalysisSignal {
  direction: Direction;
  horizon: string | null;
  probability: number | null;
  probability_calibrated: boolean;
  /** Raw, opaque score. NOT a probability; its scale is unresolved. */
  score: number | null;
  confidence_lo: number | null;
  confidence_hi: number | null;
  model_version: string | null;
  features_contributing: Array<Record<string, unknown>>;
}

export interface AnalysisLevels {
  entry: number | null;
  stop_loss: number | null;
  take_profit: number | null;
  reward_risk: number | null;
  suggested_risk_pct: number | null;
  sl_method: string | null;
  tp_method: string | null;
}

export interface AnalysisMeta {
  coverage_tier: CoverageTier;
  data_freshness_sec: number | null;
  degraded: boolean;
  /** Literal flag: is the surfaced number a calibrated probability? Always false in v1. */
  score_is_probability: boolean;
  disclaimer: string;
}

export interface AnalysisRecord {
  timestamp: string | null;
  symbol: string;
  context: AnalysisContext;
  signal: AnalysisSignal;
  levels: AnalysisLevels;
  meta: AnalysisMeta;
}

/* ---------------------------------------------------------------- */
/* context                                                          */
/* ---------------------------------------------------------------- */
export interface ContextLatest {
  timestamp: string | null;
  symbol: string;
  context: AnalysisContext;
}

/* ---------------------------------------------------------------- */
/* signals/latest (legacy surface, used where analysis is missing)  */
/* ---------------------------------------------------------------- */
export interface LatestSignal {
  available: boolean;
  reason?: string;
  decision_id?: string | null;
  trigger_timeframe?: string | null;
  closed_bar_id?: string | null;
  signal?: "LONG" | "SHORT" | "NONE" | null;
  score?: number | null;
  score_is_probability?: boolean;
  confidence?: number | null;
  probability?: number | null;
  probability_calibrated?: boolean;
  reference_price?: number | null;
  system_mode?: string | null;
  has_outcome?: boolean | null;
  symbol?: string | null;
  trigger_time?: string | number | null;
  data_state?: string | null;
  structure_state?: string | null;
  regime_state?: string | null;
  eligibility_state?: string | null;
  strategy_version?: string | null;
  configuration_version?: string | null;
}

/* ---------------------------------------------------------------- */
/* risk                                                             */
/* ---------------------------------------------------------------- */
export interface RiskProposal {
  decision_id?: string | null;
  direction?: string | null;
  entry_price?: number | null;
  stop_price?: number | null;
  target_price?: number | null;
  risk_fraction?: number | null;
  risk_amount?: number | null;
  position_size_lots?: number | null;
  reward_risk_ratio?: number | null;
  decision?: string | null;
  reason?: string | null;
  valid?: boolean;
}

export interface RiskLatest {
  available: boolean;
  open_positions: number;
  aggregate_open_risk_fraction: number;
  risk_bounded_by_guardian: boolean;
  proposal_available: boolean;
  proposal: RiskProposal | null;
  proposal_reason: string;
}

/* ---------------------------------------------------------------- */
/* shadow positions / outcomes                                      */
/* ---------------------------------------------------------------- */
export type ShadowPositionState =
  | "OPEN"
  | "CLOSED_TARGET"
  | "CLOSED_STOP"
  | "CLOSED_MANUAL"
  | "CLOSED_EXPIRED";

export interface ShadowPosition {
  position_id: string;
  decision_id: string;
  direction: string;
  state: string;
  entry_price?: number | null;
  stop_price?: number | null;
  target_price?: number | null;
  lots?: number | null;
  risk_fraction?: number | null;
  timeframe?: string | null;
  opened_at?: number | null;
  opened_bar_open?: number | null;
  exit_price?: number | null;
  realized_pnl?: number | null;
  realized_r?: number | null;
  close_reason?: string | null;
  closed_at?: number | null;
  closed_bar_open?: number | null;
  shadow_only: boolean;
}

export type OutcomeClass = "WIN" | "LOSS" | "BREAKEVEN" | "UNKNOWN";

export interface ShadowOutcome {
  outcome_id: string;
  position_id: string;
  decision_id?: string;
  direction?: string;
  timeframe?: string;
  exit_state?: string;
  entry_price?: number | null;
  exit_price?: number | null;
  lots?: number | null;
  realized_pnl?: number | null;
  realized_r?: number | null;
  risk_fraction?: number | null;
  bars_held?: number | null;
  outcome_class?: string;
  recorded_at?: number | null;
  note?: string;
  shadow_only: boolean;
}

/* ---------------------------------------------------------------- */
/* research                                                         */
/* ---------------------------------------------------------------- */
export interface ResearchExperiment {
  experiment_id: string;
  hypothesis_id: string;
  method: string;
  outcome: string;
  sample_size?: number | null;
  result_metric?: number | null;
  started_at?: number | null;
}

export interface ResearchFailure {
  failure_id: string;
  category: string;
  summary: string;
  occurrences?: number | null;
  resolved: boolean;
  last_seen?: number | null;
}

export interface ResearchStatus {
  available: boolean;
  mode: "SHADOW";
  note: string;
  experiment_count: number;
  experiments: ResearchExperiment[];
  failure_count: number;
  failures: ResearchFailure[];
}

/* ---------------------------------------------------------------- */
/* governance                                                       */
/* ---------------------------------------------------------------- */
export interface GovernancePending {
  request_id: string;
  kind: string;
  subject_id: string;
  requested_at?: number | null;
}

export interface GovernanceHistoryEntry {
  request_id: string;
  kind: string;
  status: string;
  requested_by?: string | null;
  requested_at?: number | null;
  decided_at?: number | null;
}

export interface GovernanceStatus {
  live_trading_authorised: false;
  pending_count: number;
  pending: GovernancePending[];
  history: GovernanceHistoryEntry[];
}

/* ---------------------------------------------------------------- */
/* audit                                                            */
/* ---------------------------------------------------------------- */
export interface AuditRecord {
  sequence: number | null;
  event_id: string;
  action: string;
  outcome: string;
  service_state: string;
  occurred_at: number | null;
  actor: string;
  subject: string;
  details: string;
  previous_hash: string;
  record_hash: string;
}

export interface AuditIncident {
  incident_id: string;
  severity: string;
  state: string;
  title: string;
}

export interface AuditRecent {
  count: number;
  audit_stream_size: number;
  audit_records: AuditRecord[];
  active_incidents: AuditIncident[];
}

/* ---------------------------------------------------------------- */
/* command                                                          */
/* ---------------------------------------------------------------- */
export interface CommandRequest {
  command: "notify" | "request_approval";
  actor: string;
  payload: string;
}

export interface CommandAccepted {
  accepted: boolean;
  message_id?: string;
  request_id?: string;
  status?: string;
}

export interface BridgeStatus {
  bridge_process_state: string;
  startup_stage: string;
  handshake_ok: boolean;
  mt5_ready: boolean;
  transport: string;
  host: string;
  loopback_only: boolean;
  managed_by_application: boolean;
  requires_manual_cmd: boolean;
  resolved_symbol: string;
  package_available: boolean;
  initialized: boolean;
  mt5_ready_live: boolean;
  broker: string | null;
  server: string | null;
  bridge_symbol: string | null;
  process_state: string;
  last_error: string;
  last_successful_request: number | null;
  observed: boolean;
}

/** Rate-limit/time information for a rendered datum. */
export interface Meta<T> {
  data: T | null;
  /** Epoch ms of the last successful fetch, or null if never succeeded. */
  fetchedAt: number | null;
}
