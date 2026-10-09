#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QEvent>
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

protected:
    void changeEvent(QEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void updateSystemStatus(const HealthResponse& resp);

    QFrame* mSystemCard = nullptr;
    QFrame* mTimeframesCard = nullptr;
    QGridLayout* mTimeframeGrid = nullptr;

    QLabel* mSysTitle = nullptr;
    QLabel* mTfTitle = nullptr;
    QVector<QLabel*> mRowCaptions;
    QVector<QFrame*> mDividers;
    QVector<QFrame*> mTfItemFrames;
    QVector<QLabel*> mTfItemLabels;

    // System status labels (pair: icon+dot + value)
    QVector<QLabel*> mStatusLabels;
    QVector<QLabel*> mStatusDots;
    QVector<QLabel*> mTfStatusLabels;

    HealthResponse mLastResp;
    bool mHasResp = false;

    ApiClient* mApiClient = nullptr;
};

}  // namespace astra
