#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QEvent>

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
    // NOTE: QWidget::setVisible(bool) is a non-virtual QWidget member.
    // Do not redeclare it here — a declaration without a definition hides
    // the base implementation and breaks the link (undefined reference).

protected:
    void changeEvent(QEvent* event) override;

private:
    void restyle();

    QLabel* mBar = nullptr;
    QLabel* mValueLabel = nullptr;
    double mValue = -1.0;  // <0 means no value set
};

}  // namespace astra
