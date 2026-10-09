#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QEvent>
#include "widgets/CandleChart.h"
#include "widgets/TimeframeSwitcher.h"
#include "api/ApiClient.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// ChartPage — main charting screen
// TOP ROW: "CHART" title + timeframe switcher
// MAIN: CandleChart widget
// Since /api/v1/candles does NOT exist, this is a stub.
// When the endpoint is added, connect the API client to feed candles.
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

private:
    void setupLayout();
    void restyle();
    void updateChartForTimeframe(const QString& tf);

    // Widgets
    QFrame* mTopRow = nullptr;
    QLabel* mChartTitle = nullptr;
    TimeframeSwitcher* mTfSwitcher = nullptr;
    CandleChart* mChart = nullptr;

    // API
    ApiClient* mApiClient = nullptr;
    QString mCurrentTf;
};

}  // namespace astra
