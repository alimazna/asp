// Offscreen widget tests for CandleChart.
//
// CandleChart renders on QPainter; these tests construct it offscreen and grab
// it with a range of series lengths. The short-series cases are the regression
// teeth for the time-axis integer divide-by-zero: with fewer than 10 visible
// bars numLabels collapses to 1, and the label x-span divisor used to be
// (numLabels - 1) == 0, which trapped (SIGFPE) during paint.

#include "widgets/CandleChart.h"

#include <QApplication>
#include <QPixmap>
#include <QVector>

#include <cstdio>

using namespace astra;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    if (!ok) {
        std::fprintf(stderr, "  CHECK failed: %s\n", name);
        ++g_failures;
    } else {
        std::printf("[PASS] %s\n", name);
    }
}

QVector<CandleData> makeBars(int count) {
    QVector<CandleData> bars;
    bars.reserve(count);
    for (int i = 0; i < count; ++i) {
        CandleData c;
        c.open = 2000.0 + i;
        c.high = 2002.0 + i;
        c.low = 1999.0 + i;
        c.close = 2001.0 + i;
        c.volume = 10.0 + i;
        c.timeLabel = QString("t%1").arg(i);
        bars.append(c);
    }
    return bars;
}

// Rendering a given series length must not crash and must produce a non-null
// pixmap of the requested size.
void rendersCategory(int count, const char* label) {
    CandleChart chart;
    chart.resize(600, 400);
    chart.setCandles(makeBars(count));
    chart.show();

    const QPixmap px = chart.grab();
    check(!px.isNull(), label);
    check(px.width() == 600 && px.height() == 400, "grab has the resized geometry");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Short series: numLabels == 1 (the divide-by-zero regression).
    rendersCategory(0, "empty series paints");
    rendersCategory(1, "single bar paints");
    rendersCategory(3, "three bars paint (numLabels == 1)");
    rendersCategory(9, "nine bars paint (numLabels == 1)");

    // Normal series, multiple labels.
    rendersCategory(10, "ten bars paint");
    rendersCategory(100, "default 100 bars paint");
    rendersCategory(500, "500 bars paint");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
