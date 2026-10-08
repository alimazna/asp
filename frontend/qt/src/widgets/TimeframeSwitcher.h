#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVector>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// TimeframeSwitcher — 9 buttons: M1 M5 M15 M30 H1 H4 D1 W1 MN1
// Each: 44x32, 12px mono uppercase, radius 6px
// Active: bg accent-blue, white text. Inactive: transparent, border, text-secondary
// ──────────────────────────────────────────────────────────────────────────────

class TimeframeSwitcher : public QWidget {
    Q_OBJECT

public:
    explicit TimeframeSwitcher(QWidget* parent = nullptr);

    QString currentTimeframe() const { return mCurrentTf; }
    void setTimeframe(const QString& tf);
    void resetToDefault();

signals:
    void timeframeChanged(const QString& tf);

private:
    void setupButtons();
    void updateActiveButton();

    QVector<QPushButton*> mButtons;
    QString mCurrentTf;
    static constexpr const char* TIMEFRAMES[] = {
        "M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"
    };
};

}  // namespace astra
