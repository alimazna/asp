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
// IncidentsPage — GET /api/v1/audit/recent
// Active incidents + the recent audit ledger. Read-only. Empty data renders as
// an honest empty table, never a fabricated record.
// ──────────────────────────────────────────────────────────────────────────────

class IncidentsPage : public QWidget {
    Q_OBJECT

public:
    explicit IncidentsPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void refresh();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void onAudit(const AuditData& data);

    QLabel* mTitle = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    InfoCard* mSummaryCard = nullptr;
    InfoCard* mActiveCard = nullptr;
    InfoCard* mAuditCard = nullptr;
    QLabel* mValCount = nullptr;
    QLabel* mValActive = nullptr;
    QLabel* mValStream = nullptr;
    QLabel* mActiveEmpty = nullptr;
    QTableWidget* mActive = nullptr;
    QTableWidget* mAudit = nullptr;

    ApiClient* mApiClient = nullptr;
    bool mLoaded = false;
};

}  // namespace astra
