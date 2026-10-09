#include "FreshnessBar.h"
#include <QVBoxLayout>
#include <QPalette>

namespace astra {

FreshnessBar::FreshnessBar(QWidget* parent)
    : QWidget(parent)
    , mState("unavailable")
{
    mLabel = new QLabel(this);
    mLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mLabel);

    restyle();
}

void FreshnessBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void FreshnessBar::restyle() {
    const QString secondary = palette().color(QPalette::WindowText).name();
    const QString tertiary = palette().color(QPalette::PlaceholderText).name();
    if (mState == "fresh") {
        mLabel->setText(QString("Fresh %1s").arg(mSeconds));
        mLabel->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
    } else if (mState == "stale") {
        mLabel->setText(mSeconds > 0 ? QString("Stale %1s").arg(mSeconds) : "Stale");
        mLabel->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
    } else {
        mLabel->setText("Fresh: \u2014");
        mLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }")
                                  .arg(tertiary.isEmpty() ? secondary : tertiary));
    }
}

void FreshnessBar::setFreshness(int seconds) {
    mSeconds = seconds;
    mState = seconds > 60 ? "stale" : "fresh";
    restyle();
}

void FreshnessBar::setStale() {
    mSeconds = 0;
    mState = "stale";
    restyle();
}

void FreshnessBar::setUnavailable() {
    mSeconds = 0;
    mState = "unavailable";
    restyle();
}

}  // namespace astra
