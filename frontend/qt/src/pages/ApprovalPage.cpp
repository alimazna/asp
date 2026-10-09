#include "ApprovalPage.h"
#include "widgets/InfoCard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPalette>

namespace astra {

ApprovalPage::ApprovalPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void ApprovalPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void ApprovalPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!mLoaded) refresh();
}

void ApprovalPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::governanceReceived,
                this, &ApprovalPage::onGovernance);
    }
}

void ApprovalPage::refresh() {
    if (!mApiClient) return;
    mLoaded = true;
    mApiClient->fetchGovernanceStatus();
}

void ApprovalPage::setupLayout() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    QHBoxLayout* top = new QHBoxLayout();
    mTitle = new QLabel("APPROVAL CENTER", this);
    top->addWidget(mTitle);
    top->addStretch();
    mRefreshBtn = new QPushButton("Refresh", this);
    connect(mRefreshBtn, &QPushButton::clicked, this, &ApprovalPage::refresh);
    top->addWidget(mRefreshBtn);
    main->addLayout(top);

    mStateCard = new InfoCard(this);
    mStateCard->setTitle("GOVERNANCE STATE");
    mValLiveAuth = mStateCard->addRow("Live trading authorised");
    mValPending = mStateCard->addRow("Pending requests");
    mValMode = mStateCard->addRow("Execution path");
    main->addWidget(mStateCard);

    mPendingCard = new InfoCard(this);
    mPendingCard->setTitle("PENDING REQUESTS");
    mPending = makeLedgerTable(mPendingCard,
        {"REQUEST", "KIND", "SUBJECT", "STATUS", "REQUESTED BY"});
    fillLedgerEmpty(mPending, 5, "No pending requests");
    mPendingCard->addWidget(mPending);
    mPendingEmpty = makeEmptyState(mPendingCard,
        "Read-only in this build \u2014 SHADOW mode exposes no execution command, "
        "so requests cannot be approved or rejected here.");
    mPendingCard->addWidget(mPendingEmpty);
    main->addWidget(mPendingCard);

    mHistoryCard = new InfoCard(this);
    mHistoryCard->setTitle("DECISION HISTORY");
    mHistory = makeLedgerTable(mHistoryCard,
        {"REQUEST", "KIND", "SUBJECT", "STATUS", "REQUESTED BY"});
    fillLedgerEmpty(mHistory, 5, "No decisions recorded");
    mHistoryCard->addWidget(mHistory);
    mHistoryEmpty = makeEmptyState(mHistoryCard,
        "No approval decisions have been recorded.");
    mHistoryCard->addWidget(mHistoryEmpty);
    main->addWidget(mHistoryCard);

    main->addStretch();
}

void ApprovalPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString surfaceAlt = pal.color(QPalette::AlternateBase).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString accent = pal.color(QPalette::Highlight).name();

    if (mTitle) {
        mTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 16px; "
                                      "font-weight: 600; }").arg(textPrimary));
    }
    if (mRefreshBtn) {
        mRefreshBtn->setStyleSheet(
            QString("QPushButton { background: transparent; border: 1px solid %1; "
                    "color: %2; border-radius: 8px; padding: 8px 16px; font-weight: 500; }"
                    "QPushButton:hover { background: %3; border-color: %4; color: %5; }")
                .arg(border, textSecondary, surfaceAlt, accent, textPrimary));
    }
    styleLedgerTable(mPending);
    styleLedgerTable(mHistory);
    for (QLabel* l : {mPendingEmpty, mHistoryEmpty}) {
        if (l) l->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }")
                                    .arg(textSecondary));
    }
}

void ApprovalPage::onGovernance(const GovernanceData& data) {
    mValLiveAuth->setText(data.liveTradingAuthorised ? "YES" : "no");
    mValLiveAuth->setStyleSheet(
        QString("QLabel { color: %1; font-size: 13px; font-weight: 600; "
                "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
            .arg(data.liveTradingAuthorised
                     ? palette().color(QPalette::Highlight).name()
                     : palette().color(QPalette::Text).name()));
    mValPending->setText(QString::number(data.pendingCount));
    mValMode->setText("SHADOW only");

    auto fill = [](QTableWidget* table, int cols,
                   const QVector<ApprovalRequest>& rows, const QString& emptyText) {
        if (rows.isEmpty()) {
            fillLedgerEmpty(table, cols, emptyText);
            return;
        }
        table->clearSpans();
        table->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const ApprovalRequest& r = rows[i];
            table->setItem(i, 0, new QTableWidgetItem(orDash(r.requestId)));
            table->setItem(i, 1, new QTableWidgetItem(orDash(r.kind)));
            table->setItem(i, 2, new QTableWidgetItem(orDash(r.subjectId)));
            table->setItem(i, 3, new QTableWidgetItem(orDash(r.status)));
            table->setItem(i, 4, new QTableWidgetItem(orDash(r.requestedBy)));
        }
    };
    fill(mPending, 5, data.pending, "No pending requests");
    fill(mHistory, 5, data.history, "No decisions recorded");
    mPendingEmpty->setVisible(data.pending.isEmpty());
    mHistoryEmpty->setVisible(data.history.isEmpty());
}

}  // namespace astra
