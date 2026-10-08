#include "DashboardPage.h"
#include <QApplication>
#include <QTimer>
#include <QJsonObject>

namespace astra {

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
}

void DashboardPage::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // ── Top row: 60% Context + 40% Signal ──
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(16);
    topRow->setStretch(0, 6);
    topRow->setStretch(1, 4);

    // Context card (left, 60%)
    mContextCard = new QFrame(this);
    mContextCard->setProperty("astraCard", true);
    mContextCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* ctxLayout = new QVBoxLayout(mContextCard);
    ctxLayout->setContentsMargins(20, 20, 20, 20);
    ctxLayout->setSpacing(0);

    mContextTitle = new QLabel(mContextCard);
    mContextTitle->setText("CONTEXT");
    mContextTitle->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "font-weight: 500; "
        "letter-spacing: 0.05em; "
        "}"
    );
    ctxLayout->addWidget(mContextTitle);

    // 4px spacer after title
    QLabel* titleSpacer = new QLabel(mContextCard);
    titleSpacer->setFixedHeight(16);
    ctxLayout->addWidget(titleSpacer);

    // Regime row
    QHBoxLayout* regimeRow = new QHBoxLayout();
    regimeRow->setSpacing(12);
    QLabel* regimeLbl = new QLabel(mContextCard);
    regimeLbl->setText("Regime");
    regimeLbl->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    regimeRow->addWidget(regimeLbl);
    mRegimeChip = new RegimeChip(mContextCard);
    regimeRow->addWidget(mRegimeChip, 0, Qt::AlignLeft);
    ctxLayout->addLayout(regimeRow);

    // 1px divider
    QFrame* div1 = new QFrame(mContextCard);
    div1->setFixedHeight(1);
    div1->setStyleSheet("QFrame { background: rgba(138, 163, 191, 0.3); }");
    ctxLayout->addWidget(div1);

    // H4 Bias row
    QHBoxLayout* h4Row = new QHBoxLayout();
    h4Row->setSpacing(12);
    mH4BiasLabel = new QLabel(mContextCard);
    mH4BiasLabel->setText("H4 Bias");
    mH4BiasLabel->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    h4Row->addWidget(mH4BiasLabel);
    mH4BiasValue = new QLabel(mContextCard);
    mH4BiasValue->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    h4Row->addWidget(mH4BiasValue);
    ctxLayout->addLayout(h4Row);

    QFrame* div2 = new QFrame(mContextCard);
    div2->setFixedHeight(1);
    div2->setStyleSheet("QFrame { background: rgba(138, 163, 191, 0.3); }");
    ctxLayout->addWidget(div2);

    // M15 Trigger row
    QHBoxLayout* m15Row = new QHBoxLayout();
    m15Row->setSpacing(12);
    mM15TriggerLabel = new QLabel(mContextCard);
    mM15TriggerLabel->setText("M15 Trigger");
    mM15TriggerLabel->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    m15Row->addWidget(mM15TriggerLabel);
    mM15TriggerValue = new QLabel(mContextCard);
    mM15TriggerValue->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    m15Row->addWidget(mM15TriggerValue);
    ctxLayout->addLayout(m15Row);

    QFrame* div3 = new QFrame(mContextCard);
    div3->setFixedHeight(1);
    div3->setStyleSheet("QFrame { background: rgba(138, 163, 191, 0.3); }");
    ctxLayout->addWidget(div3);

    // MTF Agreement row (hidden if null)
    QHBoxLayout* mtfRow = new QHBoxLayout();
    mtfRow->setSpacing(12);
    mMtfAgreementLabel = new QLabel(mContextCard);
    mMtfAgreementLabel->setText("Mtf Agreement");
    mMtfAgreementLabel->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    mtfRow->addWidget(mMtfAgreementLabel);
    mMtfAgreementBar = new QLabel(mContextCard);
    mMtfAgreementBar->setFixedHeight(6);
    mMtfAgreementBar->setStyleSheet("QLabel { background: #162A44; border-radius: 3px; }");
    mMtfAgreementBar->setVisible(false);
    mtfRow->addWidget(mMtfAgreementBar);
    mMtfAgreementValue = new QLabel(mContextCard);
    mMtfAgreementValue->setText("N/A");
    mMtfAgreementValue->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    mtfRow->addWidget(mMtfAgreementValue);
    ctxLayout->addLayout(mtfRow);

    topRow->addWidget(mContextCard);

    // Signal card (right, 40%)
    mSignalCard = new QFrame(this);
    mSignalCard->setProperty("astraCard", true);
    mSignalCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* sigLayout = new QVBoxLayout(mSignalCard);
    sigLayout->setContentsMargins(20, 20, 20, 20);
    sigLayout->setSpacing(0);

    QLabel* sigTitle = new QLabel(mSignalCard);
    sigTitle->setText("SIGNAL");
    sigTitle->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "font-weight: 500; "
        "letter-spacing: 0.05em; "
        "}"
    );
    sigLayout->addWidget(sigTitle);

    QLabel* spacer2 = new QLabel(mSignalCard);
    spacer2->setFixedHeight(16);
    sigLayout->addWidget(spacer2);

    mSignalCardWidget = new SignalCard(mSignalCard);
    sigLayout->addWidget(mSignalCardWidget);

    topRow->addWidget(mSignalCard);

    mainLayout->addLayout(topRow);

    // ── Full-width: Levels card ──
    mLevelsCard = new QFrame(this);
    mLevelsCard->setProperty("astraCard", true);
    mLevelsCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* lvlLayout = new QVBoxLayout(mLevelsCard);
    lvlLayout->setContentsMargins(20, 20, 20, 20);
    lvlLayout->setSpacing(0);

    QLabel* lvlTitle = new QLabel(mLevelsCard);
    lvlTitle->setText("LEVELS");
    lvlTitle->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "font-weight: 500; "
        "letter-spacing: 0.05em; "
        "}"
    );
    lvlLayout->addWidget(lvlTitle);

    QLabel* spacer3 = new QLabel(mLevelsCard);
    spacer3->setFixedHeight(16);
    lvlLayout->addWidget(spacer3);

    mLevelsCardWidget = new LevelsCard(mLevelsCard);
    lvlLayout->addWidget(mLevelsCardWidget);

    mainLayout->addWidget(mLevelsCard);
    mainLayout->addStretch();
}

void DashboardPage::updateFromAnalysis(const AnalysisResponse& resp) {
    updateContextCard(resp.data);
    updateSignalCard(resp.data);
    updateLevelsCard(resp.data);
}

void DashboardPage::updateContextCard(const AnalysisData& data) {
    // Regime — map API values to display
    QString regime = data.context.regime;
    mRegimeChip->setRegime(regime);

    // H4 Bias
    QString h4 = data.context.h4Bias;
    if (h4 == "UP" || h4 == "BULLISH") {
        mH4BiasValue->setText("\u25B2 UP");
        mH4BiasValue->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else if (h4 == "DOWN" || h4 == "BEARISH") {
        mH4BiasValue->setText("\u25BC DOWN");
        mH4BiasValue->setStyleSheet("QLabel { color: #D95A5A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else if (h4 == "NEUTRAL" || h4 == "NONE") {
        mH4BiasValue->setText("\u2014 NEUTRAL");
        mH4BiasValue->setStyleSheet("QLabel { color: #8FA3BF; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else {
        mH4BiasValue->setText("unavailable");
        mH4BiasValue->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    }

    // M15 Trigger
    QString m15 = data.context.m15Trigger;
    if (m15 == "LONG" || m15 == "UP") {
        mM15TriggerValue->setText("\u25B2 LONG");
        mM15TriggerValue->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else if (m15 == "SHORT" || m15 == "DOWN") {
        mM15TriggerValue->setText("\u25BC SHORT");
        mM15TriggerValue->setStyleSheet("QLabel { color: #D95A5A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else if (m15 == "NONE") {
        mM15TriggerValue->setText("\u2014 NONE");
        mM15TriggerValue->setStyleSheet("QLabel { color: #8FA3BF; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else {
        mM15TriggerValue->setText("unavailable");
        mM15TriggerValue->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    }

    // MTF Agreement — hide if null
    if (data.context.mtfAgreement.has_value()) {
        double val = data.context.mtfAgreement.value();
        int pct = qRound(qBound(0.0, val, 1.0) * 100.0);
        mMtfAgreementBar->setVisible(true);
        mMtfAgreementBar->setFixedWidth(qRound(pct * 0.8));  // proportional width
        // Color
        if (pct < 50) {
            mMtfAgreementBar->setStyleSheet("QLabel { background: #5A6B80; border-radius: 3px; }");
        } else if (pct < 70) {
            mMtfAgreementBar->setStyleSheet("QLabel { background: #D9A14A; border-radius: 3px; }");
        } else {
            mMtfAgreementBar->setStyleSheet("QLabel { background: #4A90D9; border-radius: 3px; }");
        }
        mMtfAgreementValue->setText(QString("%1%").arg(pct));
        mMtfAgreementValue->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    } else {
        mMtfAgreementBar->setVisible(false);
        mMtfAgreementValue->setText("unavailable");
        mMtfAgreementValue->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    }
}

void DashboardPage::updateSignalCard(const AnalysisData& data) {
    mSignalCardWidget->updateFromSignal(data.signal, data.meta);
}

void DashboardPage::updateLevelsCard(const AnalysisData& data) {
    mLevelsCardWidget->updateFromLevels(data.levels);
}

}  // namespace astra
