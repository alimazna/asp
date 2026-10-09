#include "RecoveryPage.h"
#include "widgets/InfoCard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>

namespace astra {

RecoveryPage::RecoveryPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void RecoveryPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void RecoveryPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!mLoaded) refresh();
}

void RecoveryPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::systemStateReceived,
                this, &RecoveryPage::onSystemState);
    }
}

void RecoveryPage::refresh() {
    if (!mApiClient) return;
    mLoaded = true;
    mApiClient->fetchSystemState();
}

void RecoveryPage::setupLayout() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    QHBoxLayout* top = new QHBoxLayout();
    mTitle = new QLabel("RECOVERY", this);
    top->addWidget(mTitle);
    top->addStretch();
    mRefreshBtn = new QPushButton("Refresh", this);
    connect(mRefreshBtn, &QPushButton::clicked, this, &RecoveryPage::refresh);
    top->addWidget(mRefreshBtn);
    main->addLayout(top);

    mStateCard = new InfoCard(this);
    mStateCard->setTitle("SYSTEM STATE");
    mValMode = mStateCard->addRow("Mode");
    mValShadow = mStateCard->addRow("Shadow only");
    mValReady = mStateCard->addRow("Ready");
    mValBridge = mStateCard->addRow("Bridge state");
    mValStage = mStateCard->addRow("Startup stage");
    mStateCard->body()->addSpacing(6);
    mStateCard->addWidget(makeEmptyState(mStateCard,
        "Read-only \u2014 API v1 exposes no recovery command. Recovery is a "
        "supervised, human-driven operation."));
    main->addWidget(mStateCard);

    main->addStretch();
}

void RecoveryPage::restyle() {
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

void RecoveryPage::onSystemState(const SystemStateData& data) {
    mValMode->setText(orDash(data.mode));
    mValShadow->setText(data.shadowOnly ? "yes" : "no");
    mValReady->setText(data.ready ? "yes" : "no");
    mValBridge->setText(orDash(data.bridgeState));
    mValStage->setText(orDash(data.startupStage));
}

}  // namespace astra
