#pragma once
#include "ApiTypes.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <QVector>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// API client — talks to http://127.0.0.1:8790/api/v1/
// Reads ONLY. Never contacts the bridge (127.0.0.1:8791).
// ──────────────────────────────────────────────────────────────────────────────

class ApiClient : public QObject {
    Q_OBJECT

public:
    explicit ApiClient(QObject* parent = nullptr);
    ~ApiClient() override = default;

    void setBaseUrl(const QString& baseUrl);
    QString baseUrl() const;

    // Polling
    void fetchAnalysisLatest();
    void fetchHealth();
    // Bridge lifecycle (GET /bridge/status -> process_state). The frontend
    // never contacts 127.0.0.1:8791; the backend proxies the bridge state.
    void fetchBridgeStatus();
    void fetchContextLatest();
    void fetchAnalysisHistory(int limit = 50);
    void fetchCandles(const QString& tf, int limit = 500);
    void fetchTimeframes();
    void fetchResearchStatus();
    void fetchGovernanceStatus();
    void fetchAuditRecent();
    void fetchSystemState();

    // Cancel pending requests
    void cancelAll();
    // Abort an in-flight /health probe, if any, and clear its in-flight flag.
    // Used by the poll timer so a probe whose 5s transfer timeout has not yet
    // fired does not overlap the next 5s tick.
    void cancelHealthProbe();

    // Test-only: number of consecutive /health failures observed (0 when live).
    [[nodiscard]] int consecutiveHealthFailures() const { return mConsecutiveHealthFailures; }

    // Latest governance responses (available after the matching signal).
    [[nodiscard]] ResearchData currentResearch() const { return mCurrentResearch; }
    [[nodiscard]] GovernanceData currentGovernance() const { return mCurrentGovernance; }
    [[nodiscard]] AuditData currentAudit() const { return mCurrentAudit; }
    [[nodiscard]] SystemStateData currentSystemState() const { return mCurrentSystemState; }

    // Latest results (available after finished signal)
    [[nodiscard]] AnalysisResponse currentAnalysis() const { return mCurrentAnalysis; }
    [[nodiscard]] QVector<AnalysisData> currentHistory() const { return mCurrentHistory; }
    [[nodiscard]] HealthResponse currentHealth() const { return mCurrentHealth; }
    [[nodiscard]] ContextResponse currentContext() const { return mCurrentContext; }
    [[nodiscard]] CandlesResponse currentCandles() const { return mCurrentCandles; }
    [[nodiscard]] QVector<TimeframeData> currentTimeframes() const { return mCurrentTimeframes; }

    // Connection state
    [[nodiscard]] bool isOnline() const { return mIsOnline; }
    [[nodiscard]] bool isDegraded() const { return mIsDegraded; }

signals:
    void analysisReceived(const AnalysisResponse& response);
    void historyReceived(const QVector<AnalysisData>& items);
    void healthReceived(const HealthResponse& response);
    void contextReceived(const ContextResponse& response);
    void candlesReceived(const CandlesResponse& response);
    void timeframesReceived(const QVector<TimeframeData>& timeframes);
    void researchReceived(const ResearchData& data);
    void governanceReceived(const GovernanceData& data);
    void auditReceived(const AuditData& data);
    void systemStateReceived(const SystemStateData& data);
    void error(const QString& message, const QString& code);
    // Candle-surface failures are emitted here, NOT on error(): the chart must
    // be able to react to its own request failing without other pages (which
    // also listen on error()) repainting their overlays.
    void candlesFailed(const QString& message, const QString& code);
    // Emitted on EVERY failed /health probe, before the two-strike latching
    // rule decides whether to emit offline(). MainWindow uses it to drive the
    // auto-start bake-in window, which must advance on real probe failures
    // rather than on the (deliberately debounced) offline() transition.
    void healthProbeFailed();
    void offline();
    void online();

private:
    // Secondary liveness surfaces, chained off fetchHealth(): the version/uptime
    // rows come from /health/v1 and the bridge process state from /bridge/status.
    // Their failures never flip the client offline — only /health (the aggregate)
    // does that.
    void fetchHealthV1();

    void parseEnvelope(const QByteArray& raw, ApiEnvelope& envelope);
    AnalysisData parseAnalysisData(const QJsonObject& obj);
    ContextData parseContextData(const QJsonObject& obj);
    HealthData parseHealthData(const QJsonObject& obj);
    void parseBridgeStatus(const QJsonObject& obj, HealthData& health);
    CandlesData parseCandlesData(const QJsonObject& obj);
    TimeframeData parseTimeframeData(const QJsonObject& obj);
    ResearchData parseResearchData(const QJsonObject& obj);
    GovernanceData parseGovernanceData(const QJsonObject& obj);
    AuditData parseAuditData(const QJsonObject& obj);
    SystemStateData parseSystemStateData(const QJsonObject& obj);
    void handleReply(QNetworkReply* reply, std::function<void(const QByteArray&)> onSuccess,
                     std::function<void(const QString&, const QString&)> onError);
    void scheduleRetry();

    QNetworkAccessManager mNetworkManager;
    QString mBaseUrl;
    QList<QNetworkReply*> mPendingReplies;

    AnalysisResponse mCurrentAnalysis;
    QVector<AnalysisData> mCurrentHistory;
    HealthResponse mCurrentHealth;
    ContextResponse mCurrentContext;
    CandlesResponse mCurrentCandles;
    QVector<TimeframeData> mCurrentTimeframes;
    ResearchData mCurrentResearch;
    GovernanceData mCurrentGovernance;
    AuditData mCurrentAudit;
    SystemStateData mCurrentSystemState;

    // Liveness is owned solely by /api/v1/health. Two consecutive failures are
    // required before offline() is emitted, so a single slow/timed-out probe
    // does not flip the whole UI OFFLINE (the chart lost its render window to
    // exactly that 10s flip-flop). Reset to zero on any success.
    int mConsecutiveHealthFailures = 0;
    // True while a /health probe is in flight. Prevents overlapping probes and
    // lets cancelHealthProbe() abort only that reply.
    bool mHealthInFlight = false;
    QNetworkReply* mHealthReply = nullptr;

    bool mIsOnline = false;
    bool mIsDegraded = false;
    int mRetryCount = 0;
    QTimer mRetryTimer;
};

}  // namespace astra
