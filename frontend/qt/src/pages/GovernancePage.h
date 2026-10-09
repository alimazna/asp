#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include "api/ApiClient.h"

namespace astra {

class InfoCard;

// ──────────────────────────────────────────────────────────────────────────────
// GovernancePage — live-trading authorisation + symbol allow-list.
// Authorisation comes from GET /api/v1/governance/status. There is no allow-list
// endpoint in API v1, so that card states the honest truth (SHADOW only, no
// execution path) rather than inventing a list.
// ──────────────────────────────────────────────────────────────────────────────

class GovernancePage : public QWidget {
    Q_OBJECT

public:
    explicit GovernancePage(QWidget* parent = nullptr);

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
    InfoCard* mAuthCard = nullptr;
    InfoCard* mAllowCard = nullptr;
    InfoCard* mPendingCard = nullptr;
    QLabel* mValAuthorised = nullptr;
    QLabel* mValMode = nullptr;
    QLabel* mValExecution = nullptr;
    QLabel* mValPending = nullptr;

    ApiClient* mApiClient = nullptr;
    bool mLoaded = false;
};

}  // namespace astra
