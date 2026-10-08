#pragma once
#include <QWidget>
#include <QLabel>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// FreshnessBar — shows data freshness status
// ──────────────────────────────────────────────────────────────────────────────

class FreshnessBar : public QWidget {
    Q_OBJECT

public:
    explicit FreshnessBar(QWidget* parent = nullptr);

    void setFreshness(int seconds);
    void setStale();
    void setUnavailable();

private:
    QLabel* mLabel = nullptr;
};

}  // namespace astra
