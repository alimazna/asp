#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include "api/ApiClient.h"

namespace astra {

class InfoCard;

// ──────────────────────────────────────────────────────────────────────────────
// RecoveryPage — GET /api/v1/system/state
// Reports the runtime mode, readiness, bridge state and startup stage. Read-only:
// no recovery command is exposed by API v1.
// ──────────────────────────────────────────────────────────────────────────────

class RecoveryPage : public QWidget {
    Q_OBJECT

public:
    explicit RecoveryPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void refresh();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void onSystemState(const SystemStateData& data);

    QLabel* mTitle = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    InfoCard* mStateCard = nullptr;
    QLabel* mValMode = nullptr;
    QLabel* mValShadow = nullptr;
    QLabel* mValReady = nullptr;
    QLabel* mValBridge = nullptr;
    QLabel* mValStage = nullptr;

    ApiClient* mApiClient = nullptr;
    bool mLoaded = false;
};

}  // namespace astra
