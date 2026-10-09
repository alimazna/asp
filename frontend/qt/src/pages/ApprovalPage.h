#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include "api/ApiClient.h"

class QTableWidget;


namespace astra {

class InfoCard;

// ──────────────────────────────────────────────────────────────────────────────
// ApprovalPage — GET /api/v1/governance/status (the approval queue).
// Read-only in this build: SHADOW mode exposes no execution command, so this
// page reports the queue and its history but offers no approve/reject action.
// ──────────────────────────────────────────────────────────────────────────────

class ApprovalPage : public QWidget {
    Q_OBJECT

public:
    explicit ApprovalPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void refresh();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void onGovernance(const GovernanceData& data);

    QLabel* mTitle = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    InfoCard* mStateCard = nullptr;
    InfoCard* mPendingCard = nullptr;
    InfoCard* mHistoryCard = nullptr;
    QLabel* mValLiveAuth = nullptr;
    QLabel* mValPending = nullptr;
    QLabel* mValMode = nullptr;
    QLabel* mPendingEmpty = nullptr;
    QLabel* mHistoryEmpty = nullptr;
    QTableWidget* mPending = nullptr;
    QTableWidget* mHistory = nullptr;

    ApiClient* mApiClient = nullptr;
    bool mLoaded = false;
};

}  // namespace astra
