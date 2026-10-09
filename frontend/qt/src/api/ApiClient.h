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
    void fetchContextLatest();
    void fetchAnalysisHistory(int limit = 50);
    void fetchCandles(const QString& tf, int limit = 500);
    void fetchResearchStatus();
    void fetchGovernanceStatus();
    void fetchAuditRecent();
    void fetchSystemState();

    // Cancel pending requests
    void cancelAll();

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

    // Connection state
    [[nodiscard]] bool isOnline() const { return mIsOnline; }
    [[nodiscard]] bool isDegraded() const { return mIsDegraded; }

signals:
    void analysisReceived(const AnalysisResponse& response);
    void historyReceived(const QVector<AnalysisData>& items);
    void healthReceived(const HealthResponse& response);
    void contextReceived(const ContextResponse& response);
    void candlesReceived(const CandlesResponse& response);
    void researchReceived(const ResearchData& data);
    void governanceReceived(const GovernanceData& data);
    void auditReceived(const AuditData& data);
    void systemStateReceived(const SystemStateData& data);
    void error(const QString& message, const QString& code);
    void offline();
    void online();

private:
    void parseEnvelope(const QByteArray& raw, ApiEnvelope& envelope);
    AnalysisData parseAnalysisData(const QJsonObject& obj);
    ContextData parseContextData(const QJsonObject& obj);
    HealthData parseHealthData(const QJsonObject& obj);
    CandlesData parseCandlesData(const QJsonObject& obj);
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
    ResearchData mCurrentResearch;
    GovernanceData mCurrentGovernance;
    AuditData mCurrentAudit;
    SystemStateData mCurrentSystemState;

    bool mIsOnline = false;
    bool mIsDegraded = false;
    int mRetryCount = 0;
    QTimer mRetryTimer;
};

}  // namespace astra
