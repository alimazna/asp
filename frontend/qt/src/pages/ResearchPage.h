#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include "api/ApiClient.h"

class QTableWidget;


namespace astra {

class InfoCard;

// ──────────────────────────────────────────────────────────────────────────────
// ResearchPage — GET /api/v1/research/status
// Shows the research ledger (experiments + failure memory). Read-only; research
// output never grants execution authority (the backend states this and the page
// repeats it). Missing/empty data renders as "\u2014", not fabricated rows.
// ──────────────────────────────────────────────────────────────────────────────

class ResearchPage : public QWidget {
    Q_OBJECT

public:
    explicit ResearchPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void refresh();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void onResearch(const ResearchData& data);

    InfoCard* mSummaryCard = nullptr;
    InfoCard* mExperimentsCard = nullptr;
    InfoCard* mFailuresCard = nullptr;
    QLabel* mValAvailable = nullptr;
    QLabel* mValMode = nullptr;
    QLabel* mValExperiments = nullptr;
    QLabel* mValFailures = nullptr;
    QLabel* mValAuthority = nullptr;
    QLabel* mTitle = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    QTableWidget* mExperiments = nullptr;
    QTableWidget* mFailures = nullptr;
    QLabel* mEmptyNote = nullptr;

    ApiClient* mApiClient = nullptr;
    bool mLoaded = false;
};

}  // namespace astra
