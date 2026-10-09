#pragma once
#include <QWidget>
#include <QLabel>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// ComingSoonPage — shared destination for every sidebar entry whose backend
// module has not shipped yet. Shows the module name and the honest message
// "Enabled when the backend module ships."
// ──────────────────────────────────────────────────────────────────────────────

class ComingSoonPage : public QWidget {
    Q_OBJECT

public:
    explicit ComingSoonPage(QWidget* parent = nullptr);

    void setModuleName(const QString& name);

private:
    QLabel* mTitle = nullptr;
    QLabel* mBody = nullptr;
};

}  // namespace astra
