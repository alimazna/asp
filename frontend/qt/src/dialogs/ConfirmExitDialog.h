#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// Exit confirmation dialog
// Icon: warning (warning color), Title: "Exit ASTRA?",
// Body: "Are you sure you want to exit the application?"
// Buttons: [Cancel] [Exit] — Cancel default, ESC cancels
// ──────────────────────────────────────────────────────────────────────────────

class ConfirmExitDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfirmExitDialog(QWidget* parent = nullptr);

private:
    QLabel* mIconLabel = nullptr;
    QLabel* mTitleLabel = nullptr;
    QLabel* mBodyLabel = nullptr;
    QPushButton* mCancelBtn = nullptr;
    QPushButton* mExitBtn = nullptr;
};

}  // namespace astra
