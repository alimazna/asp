#pragma once
#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QEvent>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// RegimeChip — pill-shaped indicator for market regime
// TREND: bull, RANGE: accent-blue, VOLATILE: bear, QUIET: accent-silver
// ──────────────────────────────────────────────────────────────────────────────

class RegimeChip : public QWidget {
    Q_OBJECT

public:
    explicit RegimeChip(QWidget* parent = nullptr);

    void setRegime(const QString& regime);
    QString currentRegime() const { return mRegime; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateStyle();

    QLabel* mLabel = nullptr;
    QString mRegime;
};

}  // namespace astra
