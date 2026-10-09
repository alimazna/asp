#pragma once
#include <QWidget>
#include <QLabel>
#include <QEvent>

namespace astra {

class InfoCard;

// ──────────────────────────────────────────────────────────────────────────────
// KnowledgePage — static reference surface.
// There is no /knowledge endpoint in API v1 (the manifest has none), so this
// page presents the concepts the system actually uses and is explicit that it
// is static reference, not live data. No fabricated metrics.
// ──────────────────────────────────────────────────────────────────────────────

class KnowledgePage : public QWidget {
    Q_OBJECT

public:
    explicit KnowledgePage(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void setupLayout();
    void restyle();

    QLabel* mTitle = nullptr;
    QLabel* mSubtitle = nullptr;
};

}  // namespace astra
