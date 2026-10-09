#include "IncidentsPage.h"
#include "widgets/InfoCard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QDateTime>
#include <QPalette>

namespace astra {

IncidentsPage::IncidentsPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void IncidentsPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void IncidentsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!mLoaded) refresh();
}

void IncidentsPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::auditReceived,
                this, &IncidentsPage::onAudit);
    }
}

void IncidentsPage::refresh() {
    if (!mApiClient) return;
    mLoaded = true;
    mApiClient->fetchAuditRecent();
}

void IncidentsPage::setupLayout() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    QHBoxLayout* top = new QHBoxLayout();
    mTitle = new QLabel("INCIDENTS", this);
    top->addWidget(mTitle);
    top->addStretch();
    mRefreshBtn = new QPushButton("Refresh", this);
    connect(mRefreshBtn, &QPushButton::clicked, this, &IncidentsPage::refresh);
    top->addWidget(mRefreshBtn);
    main->addLayout(top);

    mSummaryCard = new InfoCard(this);
    mSummaryCard->setTitle("INCIDENT STATE");
    mValCount = mSummaryCard->addRow("Active incidents");
    mValStream = mSummaryCard->addRow("Audit records");
    main->addWidget(mSummaryCard);

    mActiveCard = new InfoCard(this);
    mActiveCard->setTitle("ACTIVE INCIDENTS");
    mActive = makeLedgerTable(mActiveCard, {"INCIDENT", "SEVERITY", "STATE", "TITLE"}, 90);
    fillLedgerEmpty(mActive, 4, "No active incidents");
    mActiveCard->addWidget(mActive);
    mActiveEmpty = makeEmptyState(mActiveCard, "No active incidents.");
    mActiveCard->addWidget(mActiveEmpty);
    main->addWidget(mActiveCard);

    mAuditCard = new InfoCard(this);
    mAuditCard->setTitle("RECENT AUDIT");
    mAudit = makeLedgerTable(mAuditCard,
        {"SEQ", "ACTION", "OUTCOME", "ACTOR", "SUBJECT"}, 180);
    fillLedgerEmpty(mAudit, 5, "No audit records");
    mAuditCard->addWidget(mAudit);
    main->addWidget(mAuditCard);

    main->addStretch();
}

void IncidentsPage::restyle() {
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
    if (mActiveEmpty) {
        mActiveEmpty->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }")
                                        .arg(textSecondary));
    }
    styleLedgerTable(mActive);
    styleLedgerTable(mAudit);
}

void IncidentsPage::onAudit(const AuditData& data) {
    mValCount->setText(QString::number(data.incidents.size()));
    mValStream->setText(QString::number(data.auditStreamSize));

    if (data.incidents.isEmpty()) {
        fillLedgerEmpty(mActive, 4, "No active incidents");
    } else {
        mActive->clearSpans();
        mActive->setRowCount(data.incidents.size());
        for (int i = 0; i < data.incidents.size(); ++i) {
            const IncidentRecord& r = data.incidents[i];
            mActive->setItem(i, 0, new QTableWidgetItem(orDash(r.incidentId)));
            mActive->setItem(i, 1, new QTableWidgetItem(orDash(r.severity)));
            mActive->setItem(i, 2, new QTableWidgetItem(orDash(r.state)));
            mActive->setItem(i, 3, new QTableWidgetItem(orDash(r.title)));
        }
    }
    mActiveEmpty->setVisible(data.incidents.isEmpty());

    if (data.records.isEmpty()) {
        fillLedgerEmpty(mAudit, 5, "No audit records");
    } else {
        mAudit->clearSpans();
        mAudit->setRowCount(data.records.size());
        for (int i = 0; i < data.records.size(); ++i) {
            const AuditRecord& r = data.records[i];
            mAudit->setItem(i, 0, new QTableWidgetItem(QString::number(r.sequence)));
            mAudit->setItem(i, 1, new QTableWidgetItem(orDash(r.action)));
            mAudit->setItem(i, 2, new QTableWidgetItem(orDash(r.outcome)));
            mAudit->setItem(i, 3, new QTableWidgetItem(orDash(r.actor)));
            mAudit->setItem(i, 4, new QTableWidgetItem(orDash(r.subject)));
        }
    }
}

}  // namespace astra
