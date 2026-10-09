#pragma once
#include "api/ApiTypes.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QTableWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QVector>

class QScrollArea;

namespace astra {

class ApiClient;

// ──────────────────────────────────────────────────────────────────────────────
// Dashboard — reference layout (4 rows + bottom action bar)
//   Row 1: SYSTEM HEALTH | DATA STREAMS | SIGNALS | RISK | EXECUTION
//   Row 2: CHART (60%, /api/v1/candles missing -> "Coming soon")
//          + SIGNALS table (40%, /analysis/history?limit=20)
//   Row 3: TIMEFRAME MATRIX (60%) + RISK PANEL (40%)
//   Row 4: Research | Knowledge | Candidates | Validation |
//          Approval Center | Schedule
//   Bar:   [Refresh] enabled, others disabled until the backend ships.
// Every unknown value renders as "—" or "UNKNOWN" — never fabricated.
// ──────────────────────────────────────────────────────────────────────────────

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void updateFromAnalysis(const AnalysisResponse& resp);
    void updateFromHealth(const HealthResponse& resp);
    void updateFromHistory(const QVector<AnalysisData>& items);
    void setOnline(bool online);

private:
    void setupLayout();
    QFrame* makeCard(QWidget* parent, const QString& title);
    QLabel* makeCardValue(QWidget* parent);
    QLabel* makeCardSub(QWidget* parent);
    void setCardValue(QLabel* label, const QString& text, const QString& color);

    ApiClient* mApiClient = nullptr;

    // Row 1 — status cards
    QLabel* mHealthValue = nullptr;
    QLabel* mHealthSub = nullptr;
    QLabel* mStreamsValue = nullptr;
    QLabel* mStreamsSub = nullptr;
    QLabel* mSignalsValue = nullptr;
    QLabel* mSignalsSub = nullptr;
    QLabel* mRiskValue = nullptr;
    QLabel* mRiskSub = nullptr;
    QLabel* mExecutionValue = nullptr;
    QLabel* mExecutionSub = nullptr;

    // Row 2 — chart placeholder + signals table
    QTableWidget* mSignalsTable = nullptr;
    QStackedWidget* mSignalsStack = nullptr;
    QLabel* mSignalsEmpty = nullptr;

    // Row 3 — timeframe matrix + risk panel
    QTableWidget* mMatrix = nullptr;

    // Row 4 — quick cards
    QVector<QLabel*> mQuickValues;

    // Action bar
    QPushButton* mRefreshBtn = nullptr;

    // Last known state (so offline transitions can be rendered honestly)
    HealthResponse mLastHealth;
    bool mHasHealth = false;
    bool mOnline = false;
    int mHistoryCount = 0;
    bool mHasHistory = false;
};

}  // namespace astra
