#include "ResearchPage.h"
#include "widgets/InfoCard.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QPalette>

namespace astra {

ResearchPage::ResearchPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void ResearchPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void ResearchPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!mLoaded) refresh();
}

void ResearchPage::setApiClient(ApiClient* client) {
    mApiClient = client;
    if (mApiClient) {
        connect(mApiClient, &ApiClient::researchReceived,
                this, &ResearchPage::onResearch);
    }
}

void ResearchPage::refresh() {
    if (!mApiClient) return;
    mLoaded = true;
    mApiClient->fetchResearchStatus();
}

void ResearchPage::setupLayout() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    QHBoxLayout* top = new QHBoxLayout();
    mTitle = new QLabel("RESEARCH", this);
    top->addWidget(mTitle);
    top->addStretch();
    mRefreshBtn = new QPushButton("Refresh", this);
    connect(mRefreshBtn, &QPushButton::clicked, this, &ResearchPage::refresh);
    top->addWidget(mRefreshBtn);
    main->addLayout(top);

    mSummaryCard = new InfoCard(this);
    mSummaryCard->setTitle("RESEARCH STATE");
    mValAvailable = mSummaryCard->addRow("Available");
    mValMode = mSummaryCard->addRow("Mode");
    mValExperiments = mSummaryCard->addRow("Experiments");
    mValFailures = mSummaryCard->addRow("Failures");
    mValAuthority = mSummaryCard->addRow("Authority");
    mSummaryCard->body()->addSpacing(6);
    mEmptyNote = makeEmptyState(mSummaryCard,
        "Research output never grants execution authority.");
    mSummaryCard->addWidget(mEmptyNote);
    main->addWidget(mSummaryCard);

    mExperimentsCard = new InfoCard(this);
    mExperimentsCard->setTitle("EXPERIMENTS");
    mExperiments = makeLedgerTable(mExperimentsCard,
        {"EXPERIMENT", "HYPOTHESIS", "METHOD", "OUTCOME", "METRIC"});
    fillLedgerEmpty(mExperiments, 5, "No experiments recorded");
    mExperimentsCard->addWidget(mExperiments);
    main->addWidget(mExperimentsCard);

    mFailuresCard = new InfoCard(this);
    mFailuresCard->setTitle("FAILURE MEMORY");
    mFailures = makeLedgerTable(mFailuresCard,
        {"FAILURE", "CATEGORY", "SUMMARY", "RESOLVED"});
    fillLedgerEmpty(mFailures, 4, "No failures recorded");
    mFailuresCard->addWidget(mFailures);
    main->addWidget(mFailuresCard);

    main->addStretch();
}

void ResearchPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString surface = pal.color(QPalette::Base).name();
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
    Q_UNUSED(surface);
    Q_UNUSED(surfaceAlt);
    Q_UNUSED(border);
    Q_UNUSED(accent);
    styleLedgerTable(mExperiments);
    styleLedgerTable(mFailures);
}

void ResearchPage::onResearch(const ResearchData& data) {
    mValAvailable->setText(data.available ? "yes" : "unavailable");
    mValMode->setText(orDash(data.mode));
    mValExperiments->setText(QString::number(data.experimentCount));
    mValFailures->setText(QString::number(data.failureCount));
    mValAuthority->setText("NONE \u2014 read-only");

    mExperiments->setRowCount(data.experiments.size());
    for (int i = 0; i < data.experiments.size(); ++i) {
        const ResearchExperiment& e = data.experiments[i];
        mExperiments->setItem(i, 0, new QTableWidgetItem(orDash(e.experimentId)));
        mExperiments->setItem(i, 1, new QTableWidgetItem(orDash(e.hypothesisId)));
        mExperiments->setItem(i, 2, new QTableWidgetItem(orDash(e.method)));
        mExperiments->setItem(i, 3, new QTableWidgetItem(orDash(e.outcome)));
        mExperiments->setItem(i, 4, new QTableWidgetItem(
            e.resultMetric.has_value() ? QString::number(*e.resultMetric, 'f', 4)
                                       : QString::fromUtf8(kEmDash)));
    }
    if (data.experiments.isEmpty())
        fillLedgerEmpty(mExperiments, 5, "No experiments recorded");

    mFailures->setRowCount(data.failures.size());
    for (int i = 0; i < data.failures.size(); ++i) {
        const ResearchFailure& f = data.failures[i];
        mFailures->setItem(i, 0, new QTableWidgetItem(orDash(f.failureId)));
        mFailures->setItem(i, 1, new QTableWidgetItem(orDash(f.category)));
        mFailures->setItem(i, 2, new QTableWidgetItem(orDash(f.summary)));
        mFailures->setItem(i, 3, new QTableWidgetItem(f.resolved ? "yes" : "no"));
    }
    if (data.failures.isEmpty())
        fillLedgerEmpty(mFailures, 4, "No failures recorded");
}

}  // namespace astra
