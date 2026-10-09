#pragma once
#include "api/ApiTypes.h"
#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include "widgets/SignalCard.h"
#include "widgets/LevelsCard.h"
#include "widgets/RegimeChip.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// Dashboard — 2 columns (60/40) + full-width row below
// Left: Context card. Right: Signal card.
// Full-width below: Levels card.
// ──────────────────────────────────────────────────────────────────────────────

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget* parent = nullptr);

    void updateFromAnalysis(const AnalysisResponse& resp);

private:
    void setupLayout();
    void updateContextCard(const AnalysisData& data);
    void updateSignalCard(const AnalysisData& data);
    void updateLevelsCard(const AnalysisData& data);

    // Cards
    QFrame* mContextCard = nullptr;
    QFrame* mSignalCard = nullptr;
    QFrame* mLevelsCard = nullptr;

    // Context card widgets
    QLabel* mContextTitle = nullptr;
    RegimeChip* mRegimeChip = nullptr;
    QLabel* mH4BiasLabel = nullptr;
    QLabel* mH4BiasValue = nullptr;
    QLabel* mM15TriggerLabel = nullptr;
    QLabel* mM15TriggerValue = nullptr;
    QLabel* mMtfAgreementLabel = nullptr;
    QLabel* mMtfAgreementBar = nullptr;
    QLabel* mMtfAgreementValue = nullptr;

    // Signal card
    SignalCard* mSignalCardWidget = nullptr;

    // Levels card
    LevelsCard* mLevelsCardWidget = nullptr;

    // Polling
    QTimer mPollTimer;
};

}  // namespace astra
