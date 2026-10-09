#include "ConfirmExitDialog.h"
#include <QApplication>
#include <QKeyEvent>
#include <QPalette>

namespace astra {

ConfirmExitDialog::ConfirmExitDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Exit ASTRA?");
    setFixedSize(400, 220);
    setModal(true);

    // Override window flags to remove window decorations and use custom
    // QDialog styling via QSS (the dialog has its own border/radius from QSS)
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // Icon: warning symbol (text fallback since SVG rendering needs QtSvg)
    mIconLabel = new QLabel(this);
    mIconLabel->setText("\u26A0");  // ⚠ warning sign
    mIconLabel->setAlignment(Qt::AlignCenter);
    mIconLabel->setStyleSheet("QLabel { color: #D9A14A; font-size: 32px; }");
    mainLayout->addWidget(mIconLabel, 0, Qt::AlignLeft);

    // Title
    mTitleLabel = new QLabel(this);
    mTitleLabel->setText("Exit ASTRA?");
    mTitleLabel->setAlignment(Qt::AlignLeft);
    mainLayout->addWidget(mTitleLabel);

    // Body
    mBodyLabel = new QLabel(this);
    mBodyLabel->setText("Are you sure you want to exit the application?");
    mBodyLabel->setAlignment(Qt::AlignLeft);
    mBodyLabel->setWordWrap(true);
    mainLayout->addWidget(mBodyLabel);

    // Spacer
    mainLayout->addStretch();

    // Buttons — right-aligned, 8px gap
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    mCancelBtn = new QPushButton(this);
    mCancelBtn->setText("Cancel");
    mCancelBtn->setFixedSize(80, 36);
    mCancelBtn->setDefault(true);
    connect(mCancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(mCancelBtn, 0, Qt::AlignRight);

    mExitBtn = new QPushButton(this);
    mExitBtn->setText("Exit");
    mExitBtn->setFixedSize(80, 36);
    mExitBtn->setStyleSheet(
        "QPushButton { "
        "background: #D95A5A; "
        "color: white; "
        "border: 1px solid #D95A5A; "
        "border-radius: 8px; "
        "padding: 8px 16px; "
        "font-weight: 500; "
        "}"
        "QPushButton:hover { "
        "background: #E96A6A; "
        "}"
        "QPushButton:pressed { "
        "background: #C94A4A; "
        "}"
    );
    // Exit accepts the dialog; callers read the result from exec().
    connect(mExitBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(mExitBtn, 0, Qt::AlignRight);

    mainLayout->addLayout(btnLayout);

    restyle();

    // ESC key cancels — QDialog::keyPressEvent already rejects on Escape.
}

void ConfirmExitDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QDialog::changeEvent(event);
}

void ConfirmExitDialog::restyle() {
    const QPalette pal = palette();
    mTitleLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 18px; font-weight: 600; }")
            .arg(pal.color(QPalette::Text).name()));
    mBodyLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 14px; }")
            .arg(pal.color(QPalette::WindowText).name()));
}

}  // namespace astra
