#pragma once
#include "api/ApiTypes.h"
#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QEvent>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// SignalCard — displays signal direction, probability/score, confidence, horizon
// Handles uncalibrated mode: label "SCORE", hide confidence/horizon
// ──────────────────────────────────────────────────────────────────────────────

class SignalCard : public QWidget {
    Q_OBJECT

public:
    explicit SignalCard(QWidget* parent = nullptr);

    void updateFromSignal(const Signal& signal, const Meta& meta);

protected:
    void changeEvent(QEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void updateDirection(const QString& direction);
    void updateScoreOrProbability(const Signal& signal, const Meta& meta);
    void updateConfidence(const Signal& signal);
    void updateHorizon(const Signal& signal);
    void updateModelVersion(const Signal& signal);

    // Widgets
    QLabel* mDirectionLabel = nullptr;
    QLabel* mLabelLabel = nullptr;      // "SCORE" or "PROBABILITY"
    QLabel* mValueLabel = nullptr;      // numeric value
    QProgressBar* mBar = nullptr;
    QLabel* mConfidenceLabel = nullptr;
    QLabel* mHorizonLabel = nullptr;
    QLabel* mModelLabel = nullptr;
    QLabel* mUpdatedLabel = nullptr;
    QTimer mUpdatedTimer;
    int mLastUpdateSec = 0;
    bool mHasSignal = false;
    Signal mLastSignal;
    Meta mLastMeta;
};

}  // namespace astra
