#pragma once
#include "ApiTypes.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>

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

    // Cancel pending requests
    void cancelAll();

    // Latest results (available after finished signal)
    [[nodiscard]] AnalysisResponse currentAnalysis() const { return mCurrentAnalysis; }
    [[nodiscard]] HealthResponse currentHealth() const { return mCurrentHealth; }
    [[nodiscard]] ContextResponse currentContext() const { return mCurrentContext; }

    // Connection state
    [[nodiscard]] bool isOnline() const { return mIsOnline; }
    [[nodiscard]] bool isDegraded() const { return mIsDegraded; }

signals:
    void analysisReceived(const AnalysisResponse& response);
    void healthReceived(const HealthResponse& response);
    void contextReceived(const ContextResponse& response);
    void error(const QString& message, const QString& code);
    void offline();
    void online();

private:
    void parseEnvelope(const QByteArray& raw, ApiEnvelope& envelope);
    AnalysisData parseAnalysisData(const QJsonObject& obj);
    ContextData parseContextData(const QJsonObject& obj);
    HealthData parseHealthData(const QJsonObject& obj);
    void handleReply(QNetworkReply* reply, std::function<void(const QByteArray&)> onSuccess,
                     std::function<void(const QString&, const QString&)> onError);
    void scheduleRetry();

    QNetworkAccessManager mNetworkManager;
    QString mBaseUrl;
    QList<QNetworkReply*> mPendingReplies;

    AnalysisResponse mCurrentAnalysis;
    HealthResponse mCurrentHealth;
    ContextResponse mCurrentContext;

    bool mIsOnline = false;
    bool mIsDegraded = false;
    int mRetryCount = 0;
    QTimer mRetryTimer;
};

}  // namespace astra
