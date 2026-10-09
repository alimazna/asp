#include "ChartPage.h"
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QApplication>
#include <QPalette>
#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QFont>

namespace astra {

// Auto-refresh cadence while the Chart page is the visible page. The timer is
// stopped in hideEvent so a background page never polls.
static constexpr int kRefreshIntervalMs = 10000;
// The chart is capped at 500 bars for the target Intel HD 3000.
static constexpr int kCandleLimit = 500;

// ── Spinner — QPainter-only, no GL, no QMovie. Starts only while loading. ──
class SpinnerWidget : public QWidget {
public:
    explicit SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(36, 36);
        mTimer.setInterval(80);
        connect(&mTimer, &QTimer::timeout, this, [this]() {
            mAngle = (mAngle + 30) % 360;
            update();
        });
    }
    void start() { if (!mTimer.isActive()) mTimer.start(); }
    void stop() { mTimer.stop(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor accent = palette().color(QPalette::Highlight);
        QPen pen(accent, 3);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        // 270-degree arc rotating under the accent color.
        p.drawArc(rect().adjusted(4, 4, -4, -4), -mAngle * 16, 270 * 16);
    }

private:
    QTimer mTimer;
    int mAngle = 0;
};

ChartPage::ChartPage(QWidget* parent)
    : QWidget(parent)
    , mCurrentTf("M15")
{
    mRefreshTimer.setInterval(kRefreshIntervalMs);
    connect(&mRefreshTimer, &QTimer::timeout, this, &ChartPage::requestCandles);
    setupLayout();
    restyle();
}

void ChartPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void ChartPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Entering the page: fetch immediately and start the refresh cadence.
    requestCandles();
    mRefreshTimer.start();
}

void ChartPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Leaving the page: stop polling to save CPU.
    mRefreshTimer.stop();
}

void ChartPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionOverlay();
}

void ChartPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString tertiary = pal.color(QPalette::PlaceholderText).name();

    if (mChartTitle) {
        mChartTitle->setStyleSheet(
            QString("QLabel { color: %1; font-size: 16px; font-weight: 600; }")
                .arg(textPrimary));
    }
    if (mSymbolLabel) {
        mSymbolLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 14px; font-weight: 500; "
                    "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                .arg(textSecondary));
    }
    if (mFreshnessLabel) {
        mFreshnessLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 12px; "
                    "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                .arg(tertiary));
    }
    if (mOverlayTitle) {
        mOverlayTitle->setStyleSheet(
            QString("QLabel { color: %1; font-size: 15px; font-weight: 600; }")
                .arg(textPrimary));
    }
    if (mOverlayBody) {
        mOverlayBody->setStyleSheet(
            QString("QLabel { color: %1; font-size: 13px; }").arg(textSecondary));
    }
}

void ChartPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::candlesReceived,
                this, &ChartPage::onCandlesReceived);
        connect(mApiClient, &ApiClient::error,
                this, &ChartPage::onApiError);
    }
}

void ChartPage::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(0);

    // ── Top row ──
    mTopRow = new QFrame(this);
    mTopRow->setFixedHeight(60);
    QHBoxLayout* topLayout = new QHBoxLayout(mTopRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    mChartTitle = new QLabel(mTopRow);
    mChartTitle->setText("CHART");
    topLayout->addWidget(mChartTitle);

    mSymbolLabel = new QLabel(mTopRow);
    mSymbolLabel->setText("\u2014");
    topLayout->addWidget(mSymbolLabel, 0, Qt::AlignVCenter);

    mFreshnessLabel = new QLabel(mTopRow);
    mFreshnessLabel->setText("");
    topLayout->addWidget(mFreshnessLabel, 0, Qt::AlignVCenter);

    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    topLayout->addSpacerItem(spacer);

    // "Go to live" — shown only when the viewport is panned back from now.
    mGoLiveBtn = new QPushButton("Go to live", mTopRow);
    mGoLiveBtn->setVisible(false);
    connect(mGoLiveBtn, &QPushButton::clicked, this, [this]() {
        mChart->goToLive();
    });
    topLayout->addWidget(mGoLiveBtn);

    mTfSwitcher = new TimeframeSwitcher(mTopRow);
    connect(mTfSwitcher, &TimeframeSwitcher::timeframeChanged,
            this, &ChartPage::updateChartForTimeframe);
    topLayout->addWidget(mTfSwitcher);

    mainLayout->addWidget(mTopRow);

    // ── Chart + overlay ──
    mChartContainer = new QWidget(this);
    QVBoxLayout* containerLayout = new QVBoxLayout(mChartContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    mChart = new CandleChart(mChartContainer);
    mChart->setFocusPolicy(Qt::StrongFocus);
    mChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(mChart, &CandleChart::pannedChanged, this, &ChartPage::onPannedChanged);
    containerLayout->addWidget(mChart);

    mainLayout->addWidget(mChartContainer, 1);
    mainLayout->setStretch(1, 1);

    // Overlay lives above the chart, centered; it is a child of the container so
    // it can be positioned over the chart without affecting layout.
    mOverlay = new QFrame(mChartContainer);
    mOverlay->setObjectName("chartOverlay");
    mOverlay->hide();
    mOverlayLayout = new QVBoxLayout(mOverlay);
    mOverlayLayout->setContentsMargins(24, 24, 24, 24);
    mOverlayLayout->setSpacing(10);
    mOverlayLayout->setAlignment(Qt::AlignCenter);

    SpinnerWidget* spinner = new SpinnerWidget(mOverlay);
    spinner->setObjectName("chartSpinner");
    mOverlayLayout->addWidget(spinner, 0, Qt::AlignHCenter);

    mOverlayTitle = new QLabel(mOverlay);
    mOverlayTitle->setAlignment(Qt::AlignCenter);
    mOverlayLayout->addWidget(mOverlayTitle);

    mOverlayBody = new QLabel(mOverlay);
    mOverlayBody->setAlignment(Qt::AlignCenter);
    mOverlayBody->setWordWrap(true);
    mOverlayLayout->addWidget(mOverlayBody);

    mRetryBtn = new QPushButton("Retry", mOverlay);
    mRetryBtn->setVisible(false);
    connect(mRetryBtn, &QPushButton::clicked, this, &ChartPage::requestCandles);
    mOverlayLayout->addWidget(mRetryBtn, 0, Qt::AlignHCenter);

    positionOverlay();
}

void ChartPage::positionOverlay() {
    if (!mOverlay || !mChartContainer) return;
    // Cover the chart area only (not the top row).
    const QPoint topLeft = mChart->mapTo(mChartContainer, QPoint(0, 0));
    mOverlay->setGeometry(QRect(topLeft, mChart->size()));
    mOverlay->raise();
}

void ChartPage::setOverlay(Overlay state, const QString& message) {
    mOverlayState = state;
    // SpinnerWidget has no Q_OBJECT (it never needs signals/slots of its own),
    // so look it up as a plain QWidget and cast.
    auto* spinner = static_cast<SpinnerWidget*>(
        mOverlay->findChild<QWidget*>("chartSpinner"));

    const QColor pal = palette().color(QPalette::Window);
    QPalette op = mOverlay->palette();
    op.setColor(QPalette::Window, pal);
    mOverlay->setAutoFillBackground(true);
    mOverlay->setPalette(op);

    switch (state) {
        case Overlay::None:
            if (spinner) spinner->stop();
            mOverlay->hide();
            return;
        case Overlay::Loading:
            if (spinner) spinner->start();
            mOverlay->setStyleSheet(QString("QFrame#chartOverlay { "
                "background-color: rgba(0,0,0,0); }"));
            mOverlayTitle->setText("Loading candles\u2026");
            mOverlayBody->setText("");
            mRetryBtn->setVisible(false);
            break;
        case Overlay::Error:
            if (spinner) spinner->stop();
            mOverlayTitle->setText("Candles unavailable");
            mOverlayBody->setText(message.isEmpty()
                ? QStringLiteral("The candle service is unavailable.")
                : message);
            mRetryBtn->setVisible(true);
            break;
        case Overlay::Empty:
            if (spinner) spinner->stop();
            mOverlayTitle->setText("No candle data");
            mOverlayBody->setText("No candle data \u2014 backend bridge unavailable");
            mRetryBtn->setVisible(true);
            break;
    }
    mOverlay->show();
    mOverlay->raise();
    positionOverlay();
}

void ChartPage::requestCandles() {
    if (!mApiClient) {
        setOverlay(Overlay::Empty);
        return;
    }
    // Show the spinner only on the first load for a timeframe, so a periodic
    // refresh does not flash the overlay over an already-rendered chart.
    if (mOverlayState != Overlay::None) {
        setOverlay(Overlay::Loading);
    }
    mAwaitingCandles = true;
    mApiClient->fetchCandles(mCurrentTf, kCandleLimit);
}

void ChartPage::onCandlesReceived(const CandlesResponse& resp) {
    mAwaitingCandles = false;
    if (resp.data.timeframe != mCurrentTf) return;  // stale reply for an old tf

    if (!resp.data.available || resp.data.bars.isEmpty()) {
        // Honest empty state. Never fabricate a bar.
        mChart->setCandles({});
        mChart->setProperty("hasData", false);
        setOverlay(Overlay::Empty);
        return;
    }

    mSymbolLabel->setText(resp.data.symbol.isEmpty() ? "\u2014" : resp.data.symbol);
    const QString fresh = resp.data.freshness.isEmpty() ? "UNKNOWN" : resp.data.freshness;
    mFreshnessLabel->setText(QString("\u25CF %1").arg(fresh));

    QVector<CandleData> chartData;
    chartData.reserve(resp.data.bars.size());
    for (const Candle& c : resp.data.bars) {
        CandleData cd;
        cd.open = c.open;
        cd.high = c.high;
        cd.low = c.low;
        cd.close = c.close;
        cd.volume = static_cast<double>(c.tickVolume);
        cd.timeLabel = QDateTime::fromSecsSinceEpoch(c.time).toString("MM-dd HH:mm");
        chartData.append(cd);
    }
    // Preserve the viewport when the user has panned back; otherwise the chart
    // follows the newest bar.
    mChart->setCandles(chartData);
    mChart->setProperty("hasData", true);

    setOverlay(Overlay::None);
}

void ChartPage::onApiError(const QString& message, const QString& code) {
    // Only react while this page is the visible one and a candle request is in
    // flight; other pages' errors must not paint this overlay.
    if (!isVisible() || !mAwaitingCandles) return;
    mAwaitingCandles = false;
    if (mChart->isPanned() || mChart->property("hasData").toBool()) {
        // Chart already has bars: keep them on a transient refresh failure.
        return;
    }
    QString body = message;
    if (body.isEmpty()) body = QStringLiteral("The candle service is unavailable.");
    if (code == "dependency_unavailable" || code == "network_error") {
        body = QStringLiteral("The bridge is unreachable from the backend.");
    }
    setOverlay(Overlay::Error, body);
}

void ChartPage::onPannedChanged(bool panned) {
    if (mGoLiveBtn) mGoLiveBtn->setVisible(panned);
}

void ChartPage::updateChartForTimeframe(const QString& tf) {
    mCurrentTf = tf;
    mChart->setCandles({});
    mChart->resetZoom();
    setOverlay(Overlay::Loading);
    requestCandles();
}

void ChartPage::onTimeframeKey(int index) {
    if (index >= 0 && index <= 8) {
        const char* tfs[] = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
        mTfSwitcher->setTimeframe(tfs[index]);
        updateChartForTimeframe(tfs[index]);
    }
}

void ChartPage::zoomIn() {
    mChart->zoomIn();
}

void ChartPage::zoomOut() {
    mChart->zoomOut();
}

void ChartPage::resetZoom() {
    mChart->resetZoom();
}

}  // namespace astra
