#pragma once
#include <QWidget>
#include <QPainter>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <QPalette>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// CandleChart — QPainter-only custom widget for candlestick chart
// Renders on QPainter; no QML, no OpenGL, no QGraphicsView
// Designed to consume /api/v1/candles (NOT available yet — stub)
//
// Layout:
//   Chart canvas fills available space
//   Price axis: right side, 60px wide
//   Time axis: bottom, 32px tall
//   Gap: 8px
//
// Interactions:
//   Mouse wheel: zoom (20-500 candles, default 100)
//   Click+drag: pan
//   Hover: crosshair with OHLC box
//
// Colors (from visual identity):
//   Bull candle: #4CAF7A (hollow body)
//   Bear candle: #D95A5A (filled body)
//   Grid: #162A44 at 30% opacity
//   Last price line: dashed, #4A90D9
//   SL line: dashed, #D95A5A
//   TP line: dashed, #4CAF7A
// ──────────────────────────────────────────────────────────────────────────────

struct CandleData {
    double open;
    double high;
    double low;
    double close;
    QString timeLabel;  // formatted time for axis
    double volume;
};

class CandleChart : public QWidget {
    Q_OBJECT

public:
    explicit CandleChart(QWidget* parent = nullptr);

    void setCandles(const QVector<CandleData>& candles);
    void setAnalysisLevels(double sl, double tp);

    // Interactions
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void goToLive();

    // True while the viewport is scrolled back from the newest bar, which is
    // what drives the "Go to live" affordance.
    [[nodiscard]] bool isPanned() const { return mIsPanned; }

signals:
    void pannedChanged(bool panned);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void drawChart(QPainter& p);
    void drawGrid(QPainter& p);
    void drawCandles(QPainter& p);
    void drawPriceAxis(QPainter& p);
    void drawTimeAxis(QPainter& p);
    void drawLastPriceLine(QPainter& p);
    void drawSLTPLines(QPainter& p);
    void drawCrosshair(QPainter& p);
    void drawOHCLBox(QPainter& p, const QPointF& pos, const CandleData& candle);
    void updateGeometry();

    // State
    QVector<CandleData> mCandles;
    double mSL = 0;
    double mTP = 0;
    bool mShowSLTP = false;

    // Viewport
    int mVisibleStart = 0;       // index into mCandles
    int mVisibleCount = 100;     // default 100 candles
    int mMinVisible = 20;
    int mMaxVisible = 500;

    // Pan state
    bool mIsPanning = false;
    int mPanStartX = 0;
    int mPanStartIndex = 0;

    // Crosshair
    bool mShowCrosshair = false;
    int mCrosshairX = 0;
    int mCrosshairY = 0;

    // Layout (recalculated on resize)
    int mChartLeft = 0;
    int mChartTop = 0;
    int mChartWidth = 0;
    int mChartHeight = 0;
    int mPriceAxisWidth = 60;
    int mTimeAxisHeight = 32;
    int mGap = 8;

    // Price range
    double mPriceMin = 0;
    double mPriceMax = 0;
    double mPriceRange = 0;

    // Candle geometry
    double mCandleWidth = 0;
    double mCandleGap = 0;

    // Last close (for dashed line)
    double mLastClose = 0;

    // OHLC data for hovered candle
    CandleData mHoveredCandle;
    bool mHasHoverData = false;

    // "Go to live" button state
    bool mIsPanned = false;
};

}  // namespace astra
