#include "HistoryPage.h"
#include <QHeaderView>
#include <QDateTime>
#include <QFont>
#include <QMessageBox>
#include <QPalette>

namespace astra {

HistoryPage::HistoryPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void HistoryPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void HistoryPage::setApiClient(ApiClient* client) {
    mApiClient = client;
}

void HistoryPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString textTertiary = pal.color(QPalette::PlaceholderText).name();
    const QString surface = pal.color(QPalette::Base).name();
    const QString surfaceAlt = pal.color(QPalette::AlternateBase).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString accent = pal.color(QPalette::Highlight).name();

    if (mTitleLabel) {
        mTitleLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 16px; "
                                           "font-weight: 600; }").arg(textPrimary));
    }
    if (mFilterCombo) {
        mFilterCombo->setStyleSheet(
            QString("QComboBox { background: %1; border: 1px solid %2; color: %3; "
                    "border-radius: 8px; padding: 8px 12px; font-size: 14px; }"
                    "QComboBox:hover { border-color: %4; }"
                    "QComboBox:focus { border-color: %4; }")
                .arg(surface, border, textPrimary, accent));
    }
    if (mRefreshBtn) {
        mRefreshBtn->setStyleSheet(
            QString("QPushButton { background: transparent; border: 1px solid %1; "
                    "color: %2; border-radius: 8px; padding: 8px 16px; font-weight: 500; }"
                    "QPushButton:hover { background: %3; border-color: %4; color: %5; }")
                .arg(border, textSecondary, surfaceAlt, accent, textPrimary));
    }
    if (mTable) {
        mTable->horizontalHeader()->setStyleSheet(
            QString("QHeaderView::section { background: %1; color: %2; padding: 10px 12px; "
                    "border: none; border-bottom: 1px solid %3; font-size: 11px; "
                    "font-weight: 500; text-transform: uppercase; letter-spacing: 0.05em; }")
                .arg(surfaceAlt, textSecondary, border));
        mTable->setStyleSheet(
            QString("QTableWidget { background: %1; border: 1px solid %2; "
                    "border-radius: 12px; gridline-color: %2; }"
                    "QTableWidget::item { padding: 8px 12px; border-bottom: 1px solid %2; "
                    "font-size: 13px; font-family: 'JetBrains Mono', 'Consolas', monospace; }"
                    "QTableWidget::item:alternate { background: %3; }"
                    "QTableWidget::item:selected { background: %4; color: %5; }")
                .arg(surface, border, surfaceAlt, surfaceAlt, textPrimary));
    }
    for (QLabel* l : {mSummaryCaption1, mSummaryCaption2, mSummaryCaption3}) {
        if (l) {
            l->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; "
                                     "font-weight: 500; letter-spacing: 0.05em; }")
                                 .arg(textTertiary));
        }
    }
    for (QLabel* l : {mSummaryValue1, mSummaryValue2, mSummaryValue3}) {
        if (l) {
            l->setStyleSheet(QString("QLabel { color: %1; font-size: 24px; "
                                     "font-weight: 600; "
                                     "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                                 .arg(textPrimary));
        }
    }
    // Re-apply value colors (semantic win/loss) over the base style.
    if (!mEntries.isEmpty()) {
        populateTable(mEntries);
        updateSummary(mEntries);
    }
}

void HistoryPage::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // Top row: title + filter + refresh
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    mTitleLabel = new QLabel(this);
    mTitleLabel->setText("HISTORY");
    topRow->addWidget(mTitleLabel);

    // Filter: [All ▼] [Last 50 ▼]
    mFilterCombo = new QComboBox(this);
    mFilterCombo->addItem("All");
    mFilterCombo->addItem("Last 50");
    mFilterCombo->addItem("Last 20");
    mFilterCombo->addItem("Last 10");
    topRow->addWidget(mFilterCombo);

    mRefreshBtn = new QPushButton(this);
    mRefreshBtn->setText("Refresh");
    connect(mRefreshBtn, &QPushButton::clicked, this, &HistoryPage::refresh);
    topRow->addWidget(mRefreshBtn);

    topRow->addStretch();
    mainLayout->addLayout(topRow);

    // Table
    mTable = new QTableWidget(this);
    mTable->setColumnCount(5);
    mTable->setHorizontalHeaderLabels({"Time", "Dir", "Prob", "Tier", "Outcome"});
    mTable->setColumnWidth(0, 120);
    mTable->setColumnWidth(1, 80);
    mTable->setColumnWidth(2, 80);
    mTable->setColumnWidth(3, 100);
    mTable->setColumnWidth(4, 120);
    mTable->verticalHeader()->setDefaultSectionSize(36);
    mTable->setAlternatingRowColors(true);
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->verticalHeader()->setVisible(false);
    mTable->horizontalHeader()->setSectionsClickable(true);
    mainLayout->addWidget(mTable);

    // Summary cards
    QHBoxLayout* summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(16);
    summaryRow->setStretch(0, 1);
    summaryRow->setStretch(1, 1);
    summaryRow->setStretch(2, 1);

    auto buildCard = [this](QFrame*& card, QLabel*& caption, QLabel*& value,
                            const QString& captionText) {
        card = new QFrame(this);
        card->setProperty("astraCard", true);
        card->setFrameStyle(QFrame::NoFrame);
        QVBoxLayout* lay = new QVBoxLayout(card);
        lay->setContentsMargins(20, 16, 20, 16);
        caption = new QLabel(card);
        caption->setText(captionText);
        lay->addWidget(caption);
        value = new QLabel(card);
        value->setText("—");
        lay->addWidget(value);
    };
    buildCard(mSummaryCard1, mSummaryCaption1, mSummaryValue1, "Win Rate");
    buildCard(mSummaryCard2, mSummaryCaption2, mSummaryValue2, "Total R");
    buildCard(mSummaryCard3, mSummaryCaption3, mSummaryValue3, "PF");
    summaryRow->addWidget(mSummaryCard1);
    summaryRow->addWidget(mSummaryCard2);
    summaryRow->addWidget(mSummaryCard3);

    mainLayout->addLayout(summaryRow);

    // Initial empty state
    QVector<HistoryEntry> empty;
    populateTable(empty);
    updateSummary(empty);
}

void HistoryPage::updateFromHistory(const QVector<AnalysisData>& items) {
    // v1 SHADOW mode has no realised outcomes — nothing is fabricated here.
    QVector<HistoryEntry> entries;
    entries.reserve(items.size());
    for (const AnalysisData& d : items) {
        HistoryEntry e;
        e.timestamp = d.timestamp.has_value() ? d.timestamp.value() : QStringLiteral("\u2014");
        e.direction = d.signal.direction;
        e.score = d.signal.score;
        e.probabilityCalibrated = d.signal.probabilityCalibrated;
        e.coverageTier = d.meta.coverageTier;
        e.outcome = "pending";
        e.rMultiple = 0.0;
        entries.append(e);
    }
    populateTable(entries);
    updateSummary(entries);
}

void HistoryPage::refresh() {
    if (mApiClient) {
        mApiClient->fetchAnalysisHistory(50);
    }
}

void HistoryPage::populateTable(const QVector<HistoryEntry>& entries) {
    mEntries = entries;
    const QPalette pal = palette();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString textTertiary = pal.color(QPalette::PlaceholderText).name();
    const QColor green("#4CAF7A");
    const QColor red("#D95A5A");
    const QColor grey(textTertiary);

    mTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];

        // Time
        QTableWidgetItem* timeItem = new QTableWidgetItem(e.timestamp);
        timeItem->setForeground(QColor(textSecondary));
        timeItem->setFont(QFont("JetBrains Mono", 13));
        mTable->setItem(i, 0, timeItem);

        // Dir — arrow + color
        QTableWidgetItem* dirItem = new QTableWidgetItem();
        QString arrow;
        QColor dirColor;
        if (e.direction == "UP" || e.direction == "LONG") {
            arrow = "\u25B2";
            dirColor = green;
        } else if (e.direction == "DOWN" || e.direction == "SHORT") {
            arrow = "\u25BC";
            dirColor = red;
        } else {
            arrow = "\u2014";
            dirColor = grey;
        }
        dirItem->setText(arrow);
        dirItem->setForeground(dirColor);
        dirItem->setFont(QFont("JetBrains Mono", 13));
        mTable->setItem(i, 1, dirItem);

        // Prob — "63%" or "Score"; never fabricate a missing score
        QTableWidgetItem* probItem = new QTableWidgetItem();
        if (e.probabilityCalibrated && e.score.has_value()) {
            probItem->setText(QString("%1%").arg(qRound(e.score.value() * 100)));
        } else if (e.score.has_value()) {
            probItem->setText("Score");
        } else {
            probItem->setText("\u2014");
        }
        probItem->setForeground(QColor(textSecondary));
        probItem->setFont(QFont("JetBrains Mono", 13));
        mTable->setItem(i, 2, probItem);

        // Tier
        QTableWidgetItem* tierItem = new QTableWidgetItem(e.coverageTier.toUpper());
        tierItem->setForeground(grey);
        tierItem->setFont(QFont("JetBrains Mono", 13));
        mTable->setItem(i, 3, tierItem);

        // Outcome
        QTableWidgetItem* outcomeItem = new QTableWidgetItem();
        if (e.outcome == "win") {
            outcomeItem->setText("\u2713 +" + QString::number(e.rMultiple, 'f', 1) + "R");
            outcomeItem->setForeground(green);
        } else if (e.outcome == "loss") {
            outcomeItem->setText("\u2717 " + QString::number(e.rMultiple, 'f', 1) + "R");
            outcomeItem->setForeground(red);
        } else {
            outcomeItem->setText("\u2014 pending");
            outcomeItem->setForeground(grey);
        }
        outcomeItem->setFont(QFont("JetBrains Mono", 13));
        mTable->setItem(i, 4, outcomeItem);
    }
}

void HistoryPage::updateSummary(const QVector<HistoryEntry>& entries) {
    int wins = 0;
    int losses = 0;
    double totalR = 0;

    for (const auto& e : entries) {
        if (e.outcome == "win") {
            wins++;
            totalR += e.rMultiple;
        } else if (e.outcome == "loss") {
            losses++;
            totalR += e.rMultiple;
        }
    }

    int total = wins + losses;
    double winRate = total > 0 ? (double)wins / total * 100.0 : 0;

    const QString em = QStringLiteral("\u2014");
    const QString tertiary = palette().color(QPalette::PlaceholderText).name();
    const QString mono =
        "font-size: 24px; font-weight: 600; font-family: 'JetBrains Mono', 'Consolas', monospace;";

    QLabel* winRateVal = mSummaryValue1;
    QLabel* totalRVal = mSummaryValue2;
    QLabel* pfVal = mSummaryValue3;

    if (total == 0) {
        // No realised outcomes in SHADOW mode — every summary stays "—".
        if (winRateVal) {
            winRateVal->setText(em);
            winRateVal->setStyleSheet(QString("QLabel { color: %1; %2 }").arg(tertiary, mono));
        }
        if (totalRVal) {
            totalRVal->setText(em);
            totalRVal->setStyleSheet(QString("QLabel { color: %1; %2 }").arg(tertiary, mono));
        }
        if (pfVal) {
            pfVal->setText(em);
            pfVal->setStyleSheet(QString("QLabel { color: %1; %2 }").arg(tertiary, mono));
        }
        return;
    }

    if (winRateVal) {
        winRateVal->setText(QString("%1%").arg(qRound(winRate)));
    }

    if (totalRVal) {
        QString totalRText = QString::number(totalR, 'f', 1);
        if (totalR >= 0) totalRText.prepend('+');
        totalRText += 'R';
        totalRVal->setText(totalRText);
        totalRVal->setStyleSheet(totalR < 0
            ? QString("QLabel { color: #D95A5A; %1 }").arg(mono)
            : QString("QLabel { color: #4CAF7A; %1 }").arg(mono));
    }

    if (pfVal) {
        double realizedPF = totalR / total;
        pfVal->setText(QString::number(qAbs(realizedPF), 'f', 2));
    }
}

}  // namespace astra
