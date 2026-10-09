#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QTimer>
#include <QEvent>
#include "widgets/CandleChart.h"
#include "widgets/TimeframeSwitcher.h"
#include "api/ApiClient.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// ChartPage — live candlestick screen.
// TOP ROW: "CHART" title + symbol/freshness + timeframe switcher + Go-to-live
// MAIN:    CandleChart, fed by GET /api/v1/candles (the C++ loopback API — the
//          frontend never talks to MT5 or the bridge directly).
// OVERLAY: loading spinner / error+retry / empty state.
//
// Auto-refresh runs ONLY while the page is visible (QStackedWidget hides the
// other pages, so showEvent/hideEvent are the natural gate) to save CPU on the
// target Intel HD 3000. Capped at 500 candles; QPainter only.
// ──────────────────────────────────────────────────────────────────────────────

class ChartPage : public QWidget {
    Q_OBJECT

public:
    explicit ChartPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void onTimeframeKey(int index);  // 0-8 for M1..MN1
    void zoomIn();
    void zoomOut();
    void resetZoom();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Overlay { None, Loading, Error, Empty };

    void setupLayout();
    void restyle();
    void updateChartForTimeframe(const QString& tf);
    void requestCandles();
    void onCandlesReceived(const CandlesResponse& resp);
    void onApiError(const QString& message, const QString& code);
    void onPannedChanged(bool panned);
    void setOverlay(Overlay state, const QString& message = QString());
    void positionOverlay();

    // Widgets
    QFrame* mTopRow = nullptr;
    QLabel* mChartTitle = nullptr;
    QLabel* mSymbolLabel = nullptr;
    QLabel* mFreshnessLabel = nullptr;
    TimeframeSwitcher* mTfSwitcher = nullptr;
    QPushButton* mGoLiveBtn = nullptr;

    QWidget* mChartContainer = nullptr;
    CandleChart* mChart = nullptr;
    QFrame* mOverlay = nullptr;
    QVBoxLayout* mOverlayLayout = nullptr;
    QLabel* mOverlayTitle = nullptr;
    QLabel* mOverlayBody = nullptr;
    QPushButton* mRetryBtn = nullptr;

    // API / state
    ApiClient* mApiClient = nullptr;
    QString mCurrentTf;
    bool mAwaitingCandles = false;
    QTimer mRefreshTimer;
    Overlay mOverlayState = Overlay::None;
};

}  // namespace astra
