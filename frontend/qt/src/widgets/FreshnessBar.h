#pragma once
#include <QWidget>
#include <QLabel>
#include <QEvent>

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

protected:
    void changeEvent(QEvent* event) override;

private:
    void restyle();

    QLabel* mLabel = nullptr;
    QString mState;  // "fresh", "stale", "unavailable"
    int mSeconds = 0;
};

}  // namespace astra
