#include "FreshnessBar.h"

namespace astra {

FreshnessBar::FreshnessBar(QWidget* parent)
    : QWidget(parent)
{
    mLabel = new QLabel(this);
    mLabel->setText("Fresh: \u2014");
    mLabel->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    mLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mLabel);
}

void FreshnessBar::setFreshness(int seconds) {
    if (seconds > 60) {
        mLabel->setText(QString("Stale %1s").arg(seconds));
        mLabel->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
    } else {
        mLabel->setText(QString("Fresh %1s").arg(seconds));
        mLabel->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
    }
}

void FreshnessBar::setStale() {
    mLabel->setText("Stale");
    mLabel->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
}

void FreshnessBar::setUnavailable() {
    mLabel->setText("Fresh: \u2014");
    mLabel->setStyleSheet("QLabel { color: #5A6B80; font-size: 12px; }");
}

}  // namespace astra
