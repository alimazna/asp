#pragma once
#include "api/ApiTypes.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// LevelsCard — Entry, SL, TP, RR, Risk %
// If ALL levels null: show muted "Levels unavailable — calibration pending"
// If SOME non-null: show only non-null rows
// ──────────────────────────────────────────────────────────────────────────────

class LevelsCard : public QWidget {
    Q_OBJECT

public:
    explicit LevelsCard(QWidget* parent = nullptr);

    void updateFromLevels(const Levels& levels);

private:
    void setupLayout();
    void showAllUnavailable();
    void showLevels(const Levels& levels);

    QVBoxLayout* mBodyLayout = nullptr;
};

}  // namespace astra
