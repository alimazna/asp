#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonValue>
#include <optional>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// Corrected API contract (from tests/fixtures/api_v1/valid/):
//   - Envelope: { api:"v1", schema:"1.0", data:{...} }
//   - Default mode: UNCALIBRATED
//   - signal.probability = null, signal.score = present (0-1 range)
//   - signal.probability_calibrated = false
//   - meta.score_is_probability = false (ALWAYS in v1)
//   - Most fields null: horizon, confidence_lo/hi, model_version,
//     levels.*, context.mtf_agreement, meta.data_freshness_sec, timestamp
// ──────────────────────────────────────────────────────────────────────────────

struct ApiEnvelope {
    QString api;       // always "v1"
    QString schema;    // always "1.0"
    QJsonObject data;
};

struct Context {
    QString regime;            // "UNKNOWN" | "RANGE" | "TREND" | "VOLATILE" | "QUIET"
    QString h4Bias;            // "NONE" | "UP" | "DOWN" | "NEUTRAL" | "BULLISH" | "BEARISH"
    QString m15Trigger;        // "NONE" | "LONG" | "SHORT" | "UP" | "DOWN"
    std::optional<double> mtfAgreement;  // null in v1 uncalibrated
    QString volatilityState;   // "UNKNOWN" | "NORMAL" | "HIGH" | "LOW"
};

struct Signal {
    QString direction;                    // "UP" | "DOWN" | "FLAT" | "NONE" | "UNKNOWN"
    std::optional<QString> horizon;       // null in v1 uncalibrated
    std::optional<double> probability;    // null when uncalibrated
    bool probabilityCalibrated;           // false in v1 uncalibrated
    std::optional<double> score;          // null when the backend omits it — never fabricated
    std::optional<double> confidenceLo;   // null in v1 uncalibrated
    std::optional<double> confidenceHi;   // null in v1 uncalibrated
    std::optional<QString> modelVersion;  // null in v1 uncalibrated
    QStringList featuresContributing;     // always [] in v1
};

struct Levels {
    std::optional<double> entry;
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;
    std::optional<double> rewardRisk;
    std::optional<double> suggestedRiskPct;
    std::optional<QString> slMethod;
    std::optional<QString> tpMethod;
};

struct Meta {
    QString coverageTier;         // "high" | "medium" | "low" | "unknown"
    std::optional<int> dataFreshnessSec;
    bool degraded;
    bool scoreIsProbability;       // ALWAYS false in v1 (not a display switch)
    QString disclaimer;
};

struct AnalysisData {
    std::optional<QString> timestamp;
    QString symbol;
    Context context;
    Signal signal;
    Levels levels;
    Meta meta;
};

struct AnalysisResponse {
    ApiEnvelope envelope;
    AnalysisData data;
};

struct HealthData {
    QString status;          // "ok" | "degraded" | "offline"
    QString bridge;          // "ok" | "stale" | "offline"
    QString version;         // "v1"
    std::optional<int> uptimeSec;
    std::optional<QString> coverageTier;  // absent when the route does not report it
};

struct HealthResponse {
    ApiEnvelope envelope;
    HealthData data;
};

struct ContextData {
    std::optional<QString> timestamp;
    QString symbol;
    Context context;
};

struct ContextResponse {
    ApiEnvelope envelope;
    ContextData data;
};

// ──────────────────────────────────────────────────────────────────────────────
// Candle series — GET /api/v1/candles?tf={tf}&limit={N}
//   data: { bars:[{time,open,high,low,close,tick_volume,spread,real_volume}],
//           timeframe, symbol, count, closed_only, newest_closed_time, freshness }
// Closed-bar only; the forming bar is never returned.
// ──────────────────────────────────────────────────────────────────────────────

struct Candle {
    qint64 time = 0;        // epoch seconds (bar open)
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    qint64 tickVolume = 0;
    int spread = 0;
    qint64 realVolume = 0;
};

struct CandlesData {
    bool available = false;      // true only when at least one bar arrived
    QString timeframe;
    QString symbol;
    QString freshness;           // FRESH | STALE | UNKNOWN
    QVector<Candle> bars;
};

struct CandlesResponse {
    ApiEnvelope envelope;
    CandlesData data;
};

// ── Governance / system surfaces (additive, read-only) ───────────────────────
// These mirror the real backend/fixture shapes. Every unknown or absent field
// is rendered by the UI as "\u2014" (em dash); nothing is fabricated.

// GET /api/v1/research/status
struct ResearchExperiment {
    QString experimentId;
    QString hypothesisId;
    QString method;
    QString outcome;
    std::optional<double> sampleSize;
    std::optional<double> resultMetric;
};
struct ResearchFailure {
    QString failureId;
    QString category;
    QString summary;
    std::optional<double> occurrences;
    bool resolved = false;
};
struct ResearchData {
    bool available = false;
    QString mode;
    QString note;
    int experimentCount = 0;
    int failureCount = 0;
    QVector<ResearchExperiment> experiments;
    QVector<ResearchFailure> failures;
};

// GET /api/v1/governance/status
struct ApprovalRequest {
    QString requestId;
    QString kind;
    QString subjectId;
    QString status;
    QString requestedBy;
};
struct GovernanceData {
    bool available = false;
    bool liveTradingAuthorised = false;
    int pendingCount = 0;
    QVector<ApprovalRequest> pending;
    QVector<ApprovalRequest> history;
};

// GET /api/v1/audit/recent
struct AuditRecord {
    qint64 sequence = 0;
    QString eventId;
    QString action;
    QString outcome;
    QString serviceState;
    QString actor;
    QString subject;
    QString details;
};
struct IncidentRecord {
    QString incidentId;
    QString severity;
    QString state;
    QString title;
};
struct AuditData {
    bool available = false;
    int count = 0;
    int auditStreamSize = 0;
    QVector<AuditRecord> records;
    QVector<IncidentRecord> incidents;
};

// GET /api/v1/system/state
struct SystemStateData {
    bool available = false;
    QString mode;
    bool shadowOnly = false;
    bool ready = false;
    QString bridgeState;
    QString startupStage;
};

// Error body (flat, NOT enveloped)
struct ApiError {
    QString code;
    QString message;
    bool retryable = false;  // v1 has no retryable field; default false
};

// Timeframe constants
constexpr const char* TIMEFRAMES[] = {
    "M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"
};
constexpr int NUM_TIMEFRAMES = 9;

// Helpers
[[nodiscard]] inline bool isNullOrMissing(const QJsonValue& v) {
    return v.isUndefined() || v.isNull();
}

inline QString scoreToPercent(double score) {
    // score is 0-1 range from API; display as 0-100%
    return QString::number(qRound(score * 100.0));
}

inline QString formatPrice(double price) {
    return QString::number(price, 'f', 2);
}

}  // namespace astra
