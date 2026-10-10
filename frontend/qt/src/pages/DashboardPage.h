#pragma once
#include "api/ApiTypes.h"
#include <QWidget>
#include <QColor>
#include <QPalette>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QFrame>
#include <QTableWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QVector>

class QScrollArea;

namespace astra {

class ApiClient;
class CandleChart;

// ──────────────────────────────────────────────────────────────────────────────
// Dashboard — reference layout (4 rows + bottom action bar)
//   Row 1: SYSTEM HEALTH | DATA STREAMS | SIGNALS | RISK | EXECUTION
//   Row 2: CHART (60%, live /api/v1/candles)
//          + SIGNALS table (40%, /analysis/history?limit=20)
//   Row 3: TIMEFRAME MATRIX (60%, /api/v1/timeframes) + RISK PANEL (40%)
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
    void updateFromTimeframes(const QVector<TimeframeData>& items);
    void setOnline(bool online);

protected:
    void changeEvent(QEvent* e) override;

private:
    void setupLayout();
    void requestChartCandles(const QString& tf);
    QFrame* makeCard(QWidget* parent, const QString& title);
    QLabel* makeCardValue(QWidget* parent);
    QLabel* makeCardSub(QWidget* parent);
    void setCardValue(QLabel* label, const QString& text, const QString& color);

    // Re-applies every inline stylesheet from the current application palette.
    // Called on StyleChange/PaletteChange so widgets follow a theme switch that
    // happened after construction.
    void restyle();

    // Theme-aware colors, driven by the application palette (set by
    // ThemeManager per theme). Replaces the old hardcoded dark hex values.
    QColor primaryText() const;
    QColor secondaryText() const;
    QColor mutedText() const;
    QColor surfaceColor() const;
    QColor borderColor() const;

    // Inline-styled widgets carry an "astraStyle" property naming the style
    // kind, so restyle() can regenerate their stylesheets after a theme change.
    void applyTabStyle(QPushButton* tab);
    void applyActionStyle(QPushButton* b);

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

    // Row 2 — live chart + signals table
    CandleChart* mChart = nullptr;
    QVector<QPushButton*> mChartTfButtons;
    QString mChartTf = "M15";
    QTableWidget* mSignalsTable = nullptr;
    QStackedWidget* mSignalsStack = nullptr;
    QLabel* mSignalsEmpty = nullptr;

    // Row 3 — timeframe matrix + risk panel
    QTableWidget* mMatrix = nullptr;
    QVector<QString> mMatrixRowTfs;  // canonical row order, keeps "—" placeholders

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
