#include "DashboardPage.h"
#include "api/ApiClient.h"
#include "widgets/CandleChart.h"
#include <QApplication>
#include <QScrollArea>
#include <QHeaderView>
#include <QPainter>
#include <QPaintEvent>
#include <QEvent>
#include <QDateTime>
#include <QHash>
#include <QButtonGroup>
#include <QFrame>
#include <QSpacerItem>
#include <QFont>
#include <QTimer>
#include <QPushButton>

namespace astra {

// ── palette (IMAGE 1: ASTRA deep navy + silver) ──────────────────────────────
// Surface/text colors come from the active QSS + application palette (see
// ThemeManager). Only semantic status accents stay hardcoded; they are shared
// by both themes and carry meaning (green=ok, red=offline, amber=degraded).
static const char* kAccent   = "#4A90D9";
static const char* kGreen    = "#4CAF7A";
static const char* kRed      = "#D95A5A";
static const char* kAmber    = "#D9A14A";
static const char* kEm       = "\u2014";  // em dash for unknown values

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void DashboardPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::StyleChange || e->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(e);
}

void DashboardPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        // The dashboard chart card reads the same /api/v1/candles series the
        // Chart page uses; it must not answer with its own placeholder.
        connect(mApiClient, &ApiClient::candlesReceived, this,
                [this](const CandlesResponse& resp) {
                    if (!mChart || resp.data.timeframe != mChartTf) return;
                    QVector<CandleData> chartData;
                    chartData.reserve(resp.data.bars.size());
                    for (const Candle& c : resp.data.bars) {
                        CandleData cd;
                        cd.open = c.open;
                        cd.high = c.high;
                        cd.low = c.low;
                        cd.close = c.close;
                        cd.volume = static_cast<double>(c.tickVolume);
                        cd.timeLabel = QDateTime::fromSecsSinceEpoch(c.time)
                                           .toString("MM-dd HH:mm");
                        chartData.append(cd);
                    }
                    mChart->setCandles(chartData);
                });
    }
    if (mApiClient && mApiClient->isOnline()) {
        setOnline(true);
        updateFromHealth(mApiClient->currentHealth());
    }
    // Fetch the current timeframe immediately on attach (mock/API already up).
    requestChartCandles(mChartTf);
}

void DashboardPage::requestChartCandles(const QString& tf) {
    mChartTf = tf;
    if (mChart) mChart->setCandles({});
    if (mApiClient) mApiClient->fetchCandles(tf, 500);
}

// ── small builders ───────────────────────────────────────────────────────────

QFrame* DashboardPage::makeCard(QWidget* parent, const QString& title) {
    QFrame* card = new QFrame(parent);
    card->setProperty("astraCard", true);
    card->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* lay = new QVBoxLayout(card);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(4);

    QLabel* caption = new QLabel(card);
    caption->setText(title);
    caption->setProperty("astraStyle", "cardCaption");
    lay->addWidget(caption);
    return card;
}

QLabel* DashboardPage::makeCardValue(QWidget* parent) {
    QLabel* value = new QLabel(parent);
    value->setText(kEm);
    value->setProperty("astraStyle", "cardValue");
    value->setProperty("astraValuePlaceholder", true);
    value->setWordWrap(true);
    return value;
}

QLabel* DashboardPage::makeCardSub(QWidget* parent) {
    QLabel* sub = new QLabel(parent);
    sub->setText(kEm);
    sub->setProperty("astraStyle", "cardSub");
    sub->setWordWrap(true);
    return sub;
}

void DashboardPage::setCardValue(QLabel* label, const QString& text, const QString& color) {
    label->setText(text);
    // An empty color means "placeholder" (em dash / unknown) and follows the
    // muted palette role; semantic status colors (green/amber/red) are passed
    // explicitly and stay fixed across themes.
    label->setProperty("astraValuePlaceholder", color.isEmpty());
    label->setProperty("astraValueColor", color);
    restyle();
}

// ── palette accessors ────────────────────────────────────────────────────────
// These read the application palette, which ThemeManager updates on every theme
// switch, so widgets follow the QSS palette instead of a hardcoded dark theme.

QColor DashboardPage::primaryText() const {
    return palette().color(QPalette::Text);
}

QColor DashboardPage::secondaryText() const {
    return palette().color(QPalette::WindowText);
}

QColor DashboardPage::mutedText() const {
    return palette().color(QPalette::PlaceholderText);
}

QColor DashboardPage::surfaceColor() const {
    return palette().color(QPalette::Base);
}

QColor DashboardPage::borderColor() const {
    return palette().color(QPalette::Mid);
}

// ── theme-reactive inline styles ─────────────────────────────────────────────
// Widgets built with setStyleSheet() are not refreshed by a later app-wide QSS
// load, so on StyleChange/PaletteChange we regenerate them from the palette.

void DashboardPage::applyTabStyle(QPushButton* tab) {
    const QColor border = borderColor();
    const QColor active = secondaryText();
    const QColor text = primaryText();
    const QString base =
        QString("QPushButton { font-size: 11px; padding: 2px 9px; border-radius: 6px; "
                "border: 1px solid %1; color: %2; background: transparent; min-width: 0; }")
            .arg(border.name(), active.name());
    const QString activeStyle =
        QString("QPushButton { font-size: 11px; padding: 2px 9px; border-radius: 6px; "
                "border: 1px solid %1; color: %2; background: %1; min-width: 0; }")
            .arg(border.name(), text.name());
    tab->setStyleSheet(tab->isChecked() ? activeStyle : base);
}

void DashboardPage::applyActionStyle(QPushButton* b) {
    const QColor border = borderColor();
    const QColor text = secondaryText();
    const QColor textPrimary = primaryText();
    const QColor textMuted = mutedText();
    b->setStyleSheet(
        QString("QPushButton { font-size: 12px; font-weight: 500; padding: 6px 14px; "
                "border-radius: 8px; border: 1px solid %1; color: %2; "
                "background: transparent; min-width: 0; }"
                "QPushButton:hover:!disabled { background: %1; color: %3; }"
                "QPushButton:disabled { color: %4; border-color: %1; }")
            .arg(border.name(), text.name(), textPrimary.name(), textMuted.name()));
}

void DashboardPage::restyle() {
    const QColor surface = surfaceColor();
    const QColor border = borderColor();
    const QColor text = primaryText();
    const QColor textMuted = mutedText();

    // Walk every styled child and regenerate by its "astraStyle" kind.
    const QList<QWidget*> widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        const QVariant kind = w->property("astraStyle");

        if (auto* label = qobject_cast<QLabel*>(w)) {
            if (kind == "cardCaption") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 12px; font-weight: 500; "
                            "letter-spacing: 0.05em; }").arg(textMuted.name()));
            } else if (kind == "cardSub") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 11px; }").arg(textMuted.name()));
            } else if (kind == "cardValue") {
                const bool placeholder = w->property("astraValuePlaceholder").toBool();
                const QString col = w->property("astraValueColor").toString();
                const QString color = placeholder || col.isEmpty()
                    ? palette().color(QPalette::PlaceholderText).name() : col;
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 20px; font-weight: 600; "
                            "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                        .arg(color));
            } else if (kind == "primaryTitle") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 15px; font-weight: 600; }")
                        .arg(text.name()));
            } else if (kind == "mutedBody") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 13px; }").arg(textMuted.name()));
            } else if (kind == "emptyHint") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 12px; }").arg(textMuted.name()));
            } else if (kind == "quickValue") {
                label->setStyleSheet(
                    QString("QLabel { color: %1; font-size: 18px; font-weight: 600; "
                            "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                        .arg(textMuted.name()));
            }
        } else if (auto* button = qobject_cast<QPushButton*>(w)) {
            if (kind == "tfTab") {
                applyTabStyle(button);
            } else if (kind == "actionButton") {
                applyActionStyle(button);
            }
        } else if (auto* table = qobject_cast<QTableWidget*>(w)) {
            if (kind == "signalsTable") {
                table->setStyleSheet(
                    QString("QTableWidget { background: %1; border: 1px solid %2; "
                            "border-radius: 8px; font-size: 12px; color: %3; }"
                            "QTableWidget::item { border-bottom: 1px solid %2; }"
                            "QHeaderView::section { background: %2; color: %3; border: none; "
                            "padding: 6px 8px; font-size: 10px; font-weight: 500; "
                            "letter-spacing: 0.05em; }")
                        .arg(surface.name(), border.name(), textMuted.name()));
            } else if (kind == "matrixTable") {
                table->setStyleSheet(
                    QString("QTableWidget { background: %1; border: 1px solid %2; "
                            "border-radius: 8px; font-size: 12px; }"
                            "QTableWidget::item { border-bottom: 1px solid %2; }"
                            "QHeaderView::section { background: %2; color: %3; border: none; "
                            "padding: 6px 4px; font-size: 10px; font-weight: 500; "
                            "letter-spacing: 0.05em; }")
                        .arg(surface.name(), border.name(), textMuted.name()));
            }
        }
    }

    // Foreground colors of table items are set per-cell; refresh them too.
    const QColor textSecondary = secondaryText();
    if (mSignalsTable) {
        for (int r = 0; r < mSignalsTable->rowCount(); ++r) {
            if (auto* it = mSignalsTable->item(r, 0)) it->setForeground(textSecondary);
            if (auto* it = mSignalsTable->item(r, 2)) it->setForeground(text);
            if (auto* it = mSignalsTable->item(r, 3)) it->setForeground(textMuted);
            if (auto* it = mSignalsTable->item(r, 1)) {
                // Direction cell: keep semantic green/red, muted otherwise.
                const QString t = it->text();
                if (t.contains(QStringLiteral("\u25B2"))) it->setForeground(QColor(kGreen));
                else if (t.contains(QStringLiteral("\u25BC"))) it->setForeground(QColor(kRed));
                else it->setForeground(textMuted);
            }
        }
    }
    if (mMatrix) {
        for (int r = 0; r < mMatrix->rowCount(); ++r) {
            if (auto* it = mMatrix->item(r, 0)) it->setForeground(text);
            for (int c = 1; c < mMatrix->columnCount(); ++c) {
                if (auto* it = mMatrix->item(r, c)) it->setForeground(textMuted);
            }
        }
    }
}

// ── layout ───────────────────────────────────────────────────────────────────

void DashboardPage::setupLayout() {
    // Scroll container so nothing clips at the 1366x768 minimum window.
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    outer->addWidget(scroll);

    QWidget* page = new QWidget(scroll);
    page->setObjectName("dashboardPage");
    // Scope to this page only: a bare "QWidget { ... }" here would cascade to
    // every child and override the theme QSS (that is what kept the light
    // theme dark). The QSS drives the per-theme surface color instead.
    page->setStyleSheet("#dashboardPage { background: transparent; }");
    scroll->setWidget(page);

    QVBoxLayout* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(16);

    // ─────────────── ROW 1 — status cards ───────────────
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(16);

    QFrame* healthCard = makeCard(page, "SYSTEM HEALTH");
    mHealthValue = makeCardValue(healthCard);
    mHealthSub = makeCardSub(healthCard);
    healthCard->layout()->addWidget(mHealthValue);
    healthCard->layout()->addWidget(mHealthSub);
    row1->addWidget(healthCard, 1);

    QFrame* streamsCard = makeCard(page, "DATA STREAMS");
    mStreamsValue = makeCardValue(streamsCard);
    mStreamsSub = makeCardSub(streamsCard);
    streamsCard->layout()->addWidget(mStreamsValue);
    streamsCard->layout()->addWidget(mStreamsSub);
    row1->addWidget(streamsCard, 1);

    QFrame* signalsCard = makeCard(page, "SIGNALS");
    mSignalsValue = makeCardValue(signalsCard);
    mSignalsSub = makeCardSub(signalsCard);
    signalsCard->layout()->addWidget(mSignalsValue);
    signalsCard->layout()->addWidget(mSignalsSub);
    row1->addWidget(signalsCard, 1);

    QFrame* riskCard = makeCard(page, "RISK");
    mRiskValue = makeCardValue(riskCard);
    mRiskSub = makeCardSub(riskCard);
    setCardValue(mRiskValue, "UNKNOWN", kAmber);
    mRiskSub->setText("risk module pending");
    riskCard->layout()->addWidget(mRiskValue);
    riskCard->layout()->addWidget(mRiskSub);
    row1->addWidget(riskCard, 1);

    QFrame* execCard = makeCard(page, "EXECUTION");
    mExecutionValue = makeCardValue(execCard);
    mExecutionSub = makeCardSub(execCard);
    setCardValue(mExecutionValue, "SHADOW ONLY", kAccent);
    mExecutionSub->setText("mode");
    execCard->layout()->addWidget(mExecutionValue);
    execCard->layout()->addWidget(mExecutionSub);
    row1->addWidget(execCard, 1);

    mainLayout->addLayout(row1);

    // ─────────────── ROW 2 — chart (60%) + signals (40%) ───────────────
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(16);
    row2->setStretch(0, 6);
    row2->setStretch(1, 4);

    // Chart card
    QFrame* chartCard = new QFrame(page);
    chartCard->setProperty("astraCard", true);
    chartCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* chartLay = new QVBoxLayout(chartCard);
    chartLay->setContentsMargins(20, 16, 20, 16);
    chartLay->setSpacing(10);

    QHBoxLayout* chartHeader = new QHBoxLayout();
    chartHeader->setSpacing(8);
    QLabel* chartTitle = new QLabel(chartCard);
    chartTitle->setText("XAUUSD");
    chartTitle->setProperty("astraStyle", "primaryTitle");
    chartHeader->addWidget(chartTitle);

    // Timeframe tabs re-fetch /api/v1/candles with the selected timeframe.
    QButtonGroup* tfGroup = new QButtonGroup(chartCard);
    tfGroup->setExclusive(true);
    const char* tfs[] = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
    for (int i = 0; i < 9; ++i) {
        QPushButton* tab = new QPushButton(QString::fromLatin1(tfs[i]), chartCard);
        tab->setCheckable(true);
        tab->setFixedHeight(24);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setProperty("astraStyle", "tfTab");
        tfGroup->addButton(tab, i);
        chartHeader->addWidget(tab);
        mChartTfButtons.append(tab);
        if (i == 2) tab->setChecked(true);  // M15 default (matches ChartPage)
        connect(tab, &QPushButton::toggled, this, [this, tab](bool checked) {
            applyTabStyle(tab);
            if (checked) requestChartCandles(tab->text());
        });
    }
    chartHeader->addStretch();
    chartLay->addLayout(chartHeader);

    mChart = new CandleChart(chartCard);
    mChart->setMinimumHeight(180);
    mChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartLay->addWidget(mChart);
    row2->addWidget(chartCard, 6);

    // Signals card — /analysis/history
    QFrame* histCard = new QFrame(page);
    histCard->setProperty("astraCard", true);
    histCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* histLay = new QVBoxLayout(histCard);
    histLay->setContentsMargins(20, 16, 20, 16);
    histLay->setSpacing(8);

    QLabel* histCaption = new QLabel(histCard);
    histCaption->setText("SIGNALS \u2014 /analysis/history?limit=20");
    histCaption->setProperty("astraStyle", "cardCaption");
    histLay->addWidget(histCaption);

    mSignalsStack = new QStackedWidget(histCard);
    mSignalsTable = new QTableWidget(mSignalsStack);
    mSignalsTable->setProperty("astraStyle", "signalsTable");
    mSignalsTable->setColumnCount(4);
    mSignalsTable->setHorizontalHeaderLabels({"Time", "Dir", "Score", "Tier"});
    mSignalsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mSignalsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mSignalsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mSignalsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mSignalsTable->verticalHeader()->setVisible(false);
    mSignalsTable->verticalHeader()->setDefaultSectionSize(26);
    mSignalsTable->setSelectionMode(QAbstractItemView::NoSelection);
    mSignalsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mSignalsTable->setShowGrid(false);
    mSignalsTable->setFocusPolicy(Qt::NoFocus);

    mSignalsEmpty = new QLabel(mSignalsStack);
    mSignalsEmpty->setText("No signals yet \u2014 waiting for\n/api/v1/analysis/history");
    mSignalsEmpty->setAlignment(Qt::AlignCenter);
    mSignalsEmpty->setProperty("astraStyle", "emptyHint");

    mSignalsStack->addWidget(mSignalsEmpty);  // 0 = empty state
    mSignalsStack->addWidget(mSignalsTable);  // 1 = data
    histLay->addWidget(mSignalsStack);
    row2->addWidget(histCard, 4);

    mainLayout->addLayout(row2);

    // ─────────────── ROW 3 — timeframe matrix (60%) + risk panel (40%) ───
    QHBoxLayout* row3 = new QHBoxLayout();
    row3->setSpacing(16);
    row3->setStretch(0, 6);
    row3->setStretch(1, 4);

    QFrame* matrixCard = new QFrame(page);
    matrixCard->setProperty("astraCard", true);
    matrixCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* matrixLay = new QVBoxLayout(matrixCard);
    matrixLay->setContentsMargins(20, 16, 20, 16);
    matrixLay->setSpacing(8);

    QLabel* matrixCaption = new QLabel(matrixCard);
    matrixCaption->setText("TIMEFRAME MATRIX \u2014 unknown fields shown as \u2014");
    matrixCaption->setProperty("astraStyle", "cardCaption");
    matrixLay->addWidget(matrixCaption);

    mMatrix = new QTableWidget(matrixCard);
    mMatrix->setObjectName("timeframeMatrix");  // stable handle for tests
    mMatrix->setProperty("astraStyle", "matrixTable");
    mMatrix->setColumnCount(7);
    mMatrix->setHorizontalHeaderLabels(
        {"Timeframe", "Health", "Quality", "Sequence", "Freshness",
         "Last Closed Bar", "Signal/Setup"});
    mMatrix->setRowCount(9);
    const char* rowTfs[] = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
    for (int r = 0; r < 9; ++r) {
        mMatrixRowTfs.append(QString::fromLatin1(rowTfs[r]));
        QTableWidgetItem* tfItem = new QTableWidgetItem(QString::fromLatin1(rowTfs[r]));
        tfItem->setForeground(primaryText());
        QFont mono("JetBrains Mono", 12); mono.setWeight(QFont::DemiBold);
        tfItem->setFont(mono);
        mMatrix->setItem(r, 0, tfItem);
        // Every per-timeframe field is unavailable on the allowed endpoints
        // (/analysis/latest, /analysis/history, /context/latest, /health).
        for (int c = 1; c < 7; ++c) {
            QTableWidgetItem* item = new QTableWidgetItem(kEm);
            item->setForeground(mutedText());
            QFont m2("JetBrains Mono", 12);
            item->setFont(m2);
            mMatrix->setItem(r, c, item);
        }
        mMatrix->setRowHeight(r, 24);
    }
    // Size the fixed columns to their widest header/cell (never clip a
    // header), and let the last column absorb the remainder so the table
    // never scrolls sideways.
    for (int c = 0; c < 6; ++c) {
        mMatrix->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    mMatrix->horizontalHeader()->setStretchLastSection(true);
    mMatrix->verticalHeader()->setVisible(false);
    // QAbstractScrollArea::sizeHint() is small; ask for header + 9 rows so
    // the card grows instead of putting the matrix behind an inner scrollbar.
    mMatrix->setMinimumHeight(26 + 9 * 24 + 6);
    mMatrix->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mMatrix->setSelectionMode(QAbstractItemView::NoSelection);
    mMatrix->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mMatrix->setShowGrid(false);
    mMatrix->setFocusPolicy(Qt::NoFocus);
    matrixLay->addWidget(mMatrix);

    // Deterministic column fit: size every column to its content, then give
    // the spare width to the last column (or scale all columns down if the
    // content is wider than the viewport). No sideways scrollbar, no clipped
    // headers. stretchLastSection keeps this correct on window resizes.
    QTimer::singleShot(250, mMatrix, [this]() {
        if (!mMatrix || !mMatrix->horizontalHeader()) return;
        QHeaderView* h = mMatrix->horizontalHeader();
        const int cols = mMatrix->columnCount();
        const int lastCol = cols - 1;

        // 1) Measure the content once...
        h->setStretchLastSection(false);
        mMatrix->resizeColumnsToContents();

        // ...2) freeze the modes so late ResizeToContents passes cannot
        //     re-measure and re-inflate the columns after we fit them...
        for (int c = 0; c < cols; ++c) {
            h->setSectionResizeMode(c, QHeaderView::Interactive);
        }

        // 3) fit: spare width goes to the last column, or scale everything
        //    down if the content is wider than the viewport.
        int total = 0;
        for (int c = 0; c < cols; ++c) total += h->sectionSize(c);
        const int vp = mMatrix->viewport()->width();
        if (vp > 0 && total < vp) {
            h->resizeSection(lastCol, h->sectionSize(lastCol) + (vp - total));
        } else if (vp > 0 && total > vp) {
            const double k = double(vp) / double(total);
            for (int c = 0; c < cols; ++c) {
                h->resizeSection(c, qMax(24, int(h->sectionSize(c) * k)));
            }
        }

        // 4) keep the last column filling remaining width on window resizes.
        h->setStretchLastSection(true);
    });
    row3->addWidget(matrixCard, 6);

    // Risk panel — placeholder
    QFrame* riskPanel = new QFrame(page);
    riskPanel->setProperty("astraCard", true);
    riskPanel->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* riskLay = new QVBoxLayout(riskPanel);
    riskLay->setContentsMargins(20, 16, 20, 16);
    riskLay->setSpacing(8);

    QLabel* riskCaption = new QLabel(riskPanel);
    riskCaption->setText("RISK PANEL");
    riskCaption->setProperty("astraStyle", "cardCaption");
    riskLay->addWidget(riskCaption);

    riskLay->addStretch();
    QLabel* riskBody = new QLabel(riskPanel);
    riskBody->setText("Enabled when the risk module ships.");
    riskBody->setAlignment(Qt::AlignCenter);
    riskBody->setProperty("astraStyle", "mutedBody");
    riskLay->addWidget(riskBody);
    riskLay->addStretch();
    row3->addWidget(riskPanel, 4);

    mainLayout->addLayout(row3);

    // ─────────────── ROW 4 — 6 quick cards ───────────────
    QHBoxLayout* row4 = new QHBoxLayout();
    row4->setSpacing(16);
    const char* quickLabels[] = {"Research", "Knowledge", "Candidates",
                                 "Validation", "Approval Center", "Schedule"};
    for (int i = 0; i < 6; ++i) {
        QFrame* card = makeCard(page, QString::fromLatin1(quickLabels[i]).toUpper());
        QLabel* value = makeCardValue(card);
        value->setProperty("astraStyle", "quickValue");
        card->layout()->addWidget(value);
        mQuickValues.append(value);
        row4->addWidget(card, 1);
    }
    mainLayout->addLayout(row4);

    // ─────────────── BOTTOM ACTION BAR ───────────────
    QHBoxLayout* actionBar = new QHBoxLayout();
    actionBar->setSpacing(8);
    actionBar->addStretch();

    auto makeAction = [actionBar, page](const QString& text, bool enabled) {
        QPushButton* b = new QPushButton(text, page);
        b->setFixedHeight(32);
        b->setEnabled(enabled);
        b->setAttribute(Qt::WA_AlwaysShowToolTips);
        b->setProperty("astraStyle", "actionButton");
        if (!enabled) {
            b->setToolTip("Enabled when the backend supports this action.");
        }
        actionBar->addWidget(b);
        return b;
    };

    mRefreshBtn = makeAction("Refresh", true);
    mRefreshBtn->setObjectName("dashRefreshButton");
    makeAction("Checkpoint", false);
    makeAction("Pause", false);
    makeAction("Resume", false);
    makeAction("Stop", false);
    makeAction("Recovery", false);

    connect(mRefreshBtn, &QPushButton::clicked, this, [this]() {
        if (!mApiClient) return;
        mApiClient->fetchAnalysisLatest();
        mApiClient->fetchHealth();
        mApiClient->fetchAnalysisHistory(20);
    });

    mainLayout->addLayout(actionBar);
    mainLayout->addStretch();

    // Initial honest state — nothing claimed before the first response
    setCardValue(mHealthValue, kEm, QString());
    mHealthSub->setText("waiting for /api/v1/health");
    setCardValue(mStreamsValue, kEm, QString());
    mStreamsSub->setText("waiting for /api/v1/health");
    mSignalsStack->setCurrentIndex(0);  // signals empty state
}

// ── data updates ─────────────────────────────────────────────────────────────

void DashboardPage::setOnline(bool online) {
    mOnline = online;
    if (!online) {
        setCardValue(mHealthValue, "OFFLINE", kRed);
        mHealthSub->setText("backend unreachable");
        setCardValue(mStreamsValue, "OFFLINE", kRed);
        mStreamsSub->setText("bridge unreachable");
    } else if (mHasHealth) {
        updateFromHealth(mLastHealth);
    }
}

void DashboardPage::updateFromHealth(const HealthResponse& resp) {
    mLastHealth = resp;
    mHasHealth = true;
    mOnline = true;

    // SYSTEM HEALTH
    if (resp.data.status == "ok") {
        setCardValue(mHealthValue, "HEALTHY", kGreen);
        mHealthSub->setText("status ok");
    } else if (resp.data.status == "degraded") {
        setCardValue(mHealthValue, "DEGRADED", kAmber);
        mHealthSub->setText("status degraded");
    } else if (resp.data.status == "offline") {
        setCardValue(mHealthValue, "OFFLINE", kRed);
        mHealthSub->setText("status offline");
    } else {
        setCardValue(mHealthValue, kEm, QString());
        mHealthSub->setText(kEm);
    }

    // DATA STREAMS — bridge status (no stream counts in the contract)
    if (resp.data.bridge == "ok") {
        setCardValue(mStreamsValue, "OK", kGreen);
        mStreamsSub->setText("bridge ok");
    } else if (resp.data.bridge == "stale") {
        setCardValue(mStreamsValue, "STALE", kAmber);
        mStreamsSub->setText("bridge stale");
    } else if (resp.data.bridge == "offline") {
        setCardValue(mStreamsValue, "OFFLINE", kRed);
        mStreamsSub->setText("bridge offline");
    } else {
        setCardValue(mStreamsValue, kEm, QString());
        mStreamsSub->setText(kEm);
    }
}

void DashboardPage::updateFromAnalysis(const AnalysisResponse& resp) {
    // SIGNALS sub-caption: latest direction + raw score (never a probability)
    const Signal& sig = resp.data.signal;
    QString dir = sig.direction.isEmpty() ? QString(kEm) : sig.direction;
    const QString scoreText = sig.score.has_value()
        ? QString::number(sig.score.value(), 'f', 3)
        : QString(kEm);
    mSignalsSub->setText(
        QString("latest %1 \u00B7 score %2").arg(dir, scoreText));
    if (!mHasHistory) {
        setCardValue(mSignalsValue, kEm, QString());
    }
}

void DashboardPage::updateFromHistory(const QVector<AnalysisData>& items) {
    mHistoryCount = items.size();
    mHasHistory = true;

    if (mHistoryCount == 0) {
        setCardValue(mSignalsValue, kEm, QString());
        mSignalsStack->setCurrentIndex(0);  // empty state
        return;
    }

    setCardValue(mSignalsValue, QString::number(mHistoryCount), kAccent);
    if (mSignalsSub->text().isEmpty() || mSignalsSub->text() == kEm) {
        mSignalsSub->setText(QString("last %1").arg(mHistoryCount));
    }

    const int rows = qMin(20, mHistoryCount);
    mSignalsTable->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const AnalysisData& d = items[i];

        // Time — null timestamp renders as em dash
        QTableWidgetItem* timeItem = new QTableWidgetItem(
            d.timestamp.has_value() ? d.timestamp.value() : QString(kEm));
        timeItem->setForeground(secondaryText());
        mSignalsTable->setItem(i, 0, timeItem);

        // Direction
        QTableWidgetItem* dirItem;
        if (d.signal.direction == "UP") {
            dirItem = new QTableWidgetItem(QStringLiteral("\u25B2 UP"));
            dirItem->setForeground(QColor(kGreen));
        } else if (d.signal.direction == "DOWN") {
            dirItem = new QTableWidgetItem(QStringLiteral("\u25BC DOWN"));
            dirItem->setForeground(QColor(kRed));
        } else {
            dirItem = new QTableWidgetItem(kEm);
            dirItem->setForeground(mutedText());
        }
        mSignalsTable->setItem(i, 1, dirItem);

        // Score — raw score, never labelled "probability"; "—" when absent
        QTableWidgetItem* scoreItem = new QTableWidgetItem(
            d.signal.score.has_value()
                ? QString::number(d.signal.score.value(), 'f', 3)
                : QString(kEm));
        scoreItem->setForeground(primaryText());
        mSignalsTable->setItem(i, 2, scoreItem);

        // Coverage tier
        QTableWidgetItem* tierItem = new QTableWidgetItem(
            d.meta.coverageTier.isEmpty() ? QString(kEm) : d.meta.coverageTier);
        tierItem->setForeground(mutedText());
        mSignalsTable->setItem(i, 3, tierItem);
    }
    mSignalsStack->setCurrentIndex(1);  // table
}

void DashboardPage::updateFromTimeframes(const QVector<TimeframeData>& items) {
    if (!mMatrix) return;

    // Index whatever the endpoint returned, then walk the canonical row order so
    // the 9 rows are always present and in order even if the feed is partial.
    QHash<QString, TimeframeData> byTf;
    for (const TimeframeData& d : items) byTf.insert(d.timeframe, d);

    auto cell = [&](int r, int c, const QString& text, const QColor& color) {
        QTableWidgetItem* item = mMatrix->item(r, c);
        if (!item) {
            item = new QTableWidgetItem();
            QFont mono("JetBrains Mono", 12);
            item->setFont(mono);
            mMatrix->setItem(r, c, item);
        }
        item->setText(text);
        item->setForeground(color);
    };

    for (int r = 0; r < mMatrixRowTfs.size(); ++r) {
        const QString tf = mMatrixRowTfs[r];
        const auto it = byTf.constFind(tf);
        if (it == byTf.constEnd()) {
            // Not reported at all: every field is unavailable.
            for (int c = 1; c < 7; ++c) cell(r, c, kEm, mutedText());
            continue;
        }
        const TimeframeData& d = it.value();
        const bool observed = d.observed.value_or(false);
        const QString freshState = d.freshness.has_value() ? d.freshness->state
                                                           : QString();
        const bool isFresh = d.freshness.has_value() && d.freshness->isFresh;

        // Health — derived overall status; unobserved is never "OK".
        if (!observed) {
            cell(r, 1, "MISSING", QColor(kRed));
        } else if (freshState == "STALE") {
            cell(r, 1, "STALE", QColor(kAmber));
        } else if (d.qualityState == "VALID" && isFresh) {
            cell(r, 1, "OK", QColor(kGreen));
        } else {
            cell(r, 1, kEm, mutedText());
        }

        // Quality — the reported data-quality state.
        if (d.qualityState.isEmpty()) {
            cell(r, 2, kEm, mutedText());
        } else {
            const QString q = d.qualityState;
            cell(r, 2, q, (q == "VALID") ? QColor(kGreen)
                          : (q == "UNKNOWN") ? mutedText()
                                             : QColor(kAmber));
        }

        // Sequence — "—" when the backend omits it.
        cell(r, 3, d.sequence.has_value()
                       ? QString::number(d.sequence.value())
                       : QString(kEm),
             d.sequence.has_value() ? primaryText() : mutedText());

        // Freshness — state + age, "—" when the freshness block is null.
        QString freshText = kEm;
        QColor freshColor = mutedText();
        if (d.freshness.has_value()) {
            const TimeframeFreshness& f = d.freshness.value();
            freshText = f.state.isEmpty() ? "UNKNOWN" : f.state;
            freshColor = f.isFresh ? QColor(kGreen)
                       : (f.state == "STALE") ? QColor(kAmber)
                                              : mutedText();
        }
        cell(r, 4, freshText, freshColor);

        // Last closed bar — epoch seconds to local time, "—" when absent.
        if (d.lastClosedBarOpen.has_value()) {
            cell(r, 5,
                 QDateTime::fromSecsSinceEpoch(d.lastClosedBarOpen.value())
                     .toString("MM-dd HH:mm"),
                 secondaryText());
        } else {
            cell(r, 5, kEm, mutedText());
        }

        // Signal/Setup — first capability impact, "—" when none reported.
        if (!d.capabilityImpact.isEmpty()) {
            const TimeframeCapabilityImpact& ci = d.capabilityImpact.first();
            const QString text = ci.impact.isEmpty() ? QString(kEm) : ci.impact;
            if (ci.reason.isEmpty()) {
                cell(r, 6, text, secondaryText());
            } else {
                cell(r, 6, text, secondaryText());
                mMatrix->item(r, 6)->setToolTip(ci.reason);
            }
        } else {
            cell(r, 6, kEm, mutedText());
        }
    }
}

}  // namespace astra
