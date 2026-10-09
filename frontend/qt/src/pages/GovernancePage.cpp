#include "GovernancePage.h"
#include "widgets/InfoCard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>

namespace astra {

GovernancePage::GovernancePage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void GovernancePage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void GovernancePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!mLoaded) refresh();
}

void GovernancePage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::governanceReceived,
                this, &GovernancePage::onGovernance);
    }
}

void GovernancePage::refresh() {
    if (!mApiClient) return;
    mLoaded = true;
    mApiClient->fetchGovernanceStatus();
}

void GovernancePage::setupLayout() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    QHBoxLayout* top = new QHBoxLayout();
    mTitle = new QLabel("GOVERNANCE", this);
    top->addWidget(mTitle);
    top->addStretch();
    mRefreshBtn = new QPushButton("Refresh", this);
    connect(mRefreshBtn, &QPushButton::clicked, this, &GovernancePage::refresh);
    top->addWidget(mRefreshBtn);
    main->addLayout(top);

    mAuthCard = new InfoCard(this);
    mAuthCard->setTitle("LIVE-TRADING AUTHORISATION");
    mValAuthorised = mAuthCard->addRow("Live trading authorised");
    mValMode = mAuthCard->addRow("Mode");
    mValExecution = mAuthCard->addRow("Execution path");
    mAuthCard->body()->addSpacing(6);
    mAuthCard->addWidget(makeEmptyState(mAuthCard,
        "SHADOW only \u2014 this build never authorises live execution."));
    main->addWidget(mAuthCard);

    mAllowCard = new InfoCard(this);
    mAllowCard->setTitle("SYMBOL ALLOW-LIST");
    mAllowCard->addWidget(makeEmptyState(mAllowCard,
        "No allow-list endpoint exists in API v1. The bridge resolves a single "
        "symbol (XAUUSD) for market data; that is not a trading allow-list."));
    main->addWidget(mAllowCard);

    mPendingCard = new InfoCard(this);
    mPendingCard->setTitle("PENDING APPROVALS");
    mValPending = mPendingCard->addRow("Pending requests");
    mPendingCard->body()->addSpacing(6);
    mPendingCard->addWidget(makeEmptyState(mPendingCard,
        "Approval decisions are made in the Approval Center."));
    main->addWidget(mPendingCard);

    main->addStretch();
}

void GovernancePage::restyle() {
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
}

void GovernancePage::onGovernance(const GovernanceData& data) {
    mValAuthorised->setText(data.liveTradingAuthorised ? "YES" : "no");
    mValMode->setText("SHADOW");
    mValExecution->setText("none \u2014 not exposed");
    mValPending->setText(QString::number(data.pendingCount));
}

}  // namespace astra
