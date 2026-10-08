#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include "api/ApiClient.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// HealthPage — system status + 9 timeframe grid
// Top card: SYSTEM STATUS (rows: Backend, Bridge, Freshness, API version, Uptime, Coverage)
// Second card: TIMEFRAMES (3x3 grid of M1..MN1 with status dots)
// ──────────────────────────────────────────────────────────────────────────────

class HealthPage : public QWidget {
    Q_OBJECT

public:
    explicit HealthPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void updateFromHealth(const HealthResponse& resp);

private:
    void setupLayout();
    void updateSystemStatus(const HealthResponse& resp);

    QFrame* mSystemCard = nullptr;
    QFrame* mTimeframesCard = nullptr;
    QGridLayout* mTimeframeGrid = nullptr;

    // System status labels (pair: icon+dot + value)
    QVector<QLabel*> mStatusLabels;
    QVector<QLabel*> mStatusDots;

    ApiClient* mApiClient = nullptr;
};

}  // namespace astra
