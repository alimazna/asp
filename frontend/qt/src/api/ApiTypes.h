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
    double score;                         // ALWAYS present (0-1 range, but treat as 0-100 for display)
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
