#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QFrame>
#include <QComboBox>
#include <QPushButton>
#include "api/ApiClient.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// HistoryPage — table of recent signals from /analysis/history
// Columns: Time (120px), Dir (80px), Prob (80px), Tier (100px), Outcome (120px)
// Sorting: click header to sort
// Summary: Win Rate, Total R, PF below table
// ──────────────────────────────────────────────────────────────────────────────

struct HistoryEntry {
    QString timestamp;
    QString direction;
    double score;
    bool probabilityCalibrated;
    QString coverageTier;
    QString outcome;  // "win", "loss", "pending"
    double rMultiple;
};

class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);
    void refresh();

private:
    void setupLayout();
    void populateTable(const QVector<HistoryEntry>& entries);
    void updateSummary(const QVector<HistoryEntry>& entries);

    QTableWidget* mTable = nullptr;
    QComboBox* mFilterCombo = nullptr;
    QPushButton* mRefreshBtn = nullptr;

    // Summary cards
    QFrame* mSummaryCard1 = nullptr;  // Win Rate
    QFrame* mSummaryCard2 = nullptr;  // Total R
    QFrame* mSummaryCard3 = nullptr;  // PF

    ApiClient* mApiClient = nullptr;
};

}  // namespace astra
