#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// MtfMeter — progress bar for MTF agreement (0-100%)
// <50%: text-tertiary, 50-70%: warning, >70%: accent-blue, >85%: bull
// ──────────────────────────────────────────────────────────────────────────────

class MtfMeter : public QWidget {
    Q_OBJECT

public:
    explicit MtfMeter(QWidget* parent = nullptr);

    void setAgreement(double value);  // 0.0 to 1.0

private:
    QLabel* mBar = nullptr;
    QLabel* mValueLabel = nullptr;
};

}  // namespace astra
