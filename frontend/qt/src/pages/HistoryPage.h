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
#include <QEvent>
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

    // Feed rows from ApiClient::historyReceived (AnalysisData -> HistoryEntry)
    void updateFromHistory(const QVector<AnalysisData>& items);

    // Test seam: feed summary rows directly (outcome != "pending").
    void setSummaryEntriesForTest(const QVector<HistoryEntry>& entries) {
        updateSummary(entries);
    }

protected:
    void changeEvent(QEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void populateTable(const QVector<HistoryEntry>& entries);
    void updateSummary(const QVector<HistoryEntry>& entries);

    QTableWidget* mTable = nullptr;
    QComboBox* mFilterCombo = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    QLabel* mTitleLabel = nullptr;

    // Summary cards
    QFrame* mSummaryCard1 = nullptr;  // Win Rate
    QFrame* mSummaryCard2 = nullptr;  // Total R
    QFrame* mSummaryCard3 = nullptr;  // PF
    QLabel* mSummaryCaption1 = nullptr;
    QLabel* mSummaryCaption2 = nullptr;
    QLabel* mSummaryCaption3 = nullptr;
    QLabel* mSummaryValue1 = nullptr;
    QLabel* mSummaryValue2 = nullptr;
    QLabel* mSummaryValue3 = nullptr;

    QVector<HistoryEntry> mEntries;

    ApiClient* mApiClient = nullptr;
};

}  // namespace astra
