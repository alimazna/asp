#include "TimeframeSwitcher.h"

namespace astra {

TimeframeSwitcher::TimeframeSwitcher(QWidget* parent)
    : QWidget(parent)
    , mCurrentTf("M15")
{
    setFixedHeight(44);
    setupButtons();
    updateButtonStyles();
}

void TimeframeSwitcher::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        updateButtonStyles();
    }
    QWidget::changeEvent(event);
}

void TimeframeSwitcher::setupButtons() {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);  // 4px gap between buttons

    for (int i = 0; i < 9; ++i) {
        QPushButton* btn = new QPushButton(this);
        btn->setText(TIMEFRAMES[i]);
        btn->setFixedSize(44, 32);
        layout->addWidget(btn);
        mButtons.append(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            mCurrentTf = TIMEFRAMES[i];
            updateButtonStyles();
            emit timeframeChanged(mCurrentTf);
        });
    }
}

void TimeframeSwitcher::setTimeframe(const QString& tf) {
    // Validate against known timeframes
    for (int i = 0; i < 9; ++i) {
        if (TIMEFRAMES[i] == tf) {
            mCurrentTf = tf;
            updateButtonStyles();
            return;
        }
    }
}

void TimeframeSwitcher::resetToDefault() {
    setTimeframe("M15");
}

void TimeframeSwitcher::updateActiveButton() {
    updateButtonStyles();
}

void TimeframeSwitcher::updateButtonStyles() {
    const QPalette pal = palette();
    const QString border = pal.color(QPalette::Mid).name();
    const QString text = pal.color(QPalette::WindowText).name();
    const QString textActive = pal.color(QPalette::Text).name();
    const QString accent = pal.color(QPalette::Highlight).name();
    const QString inset = pal.color(QPalette::AlternateBase).name();

    for (int i = 0; i < mButtons.size(); ++i) {
        QPushButton* btn = mButtons[i];
        const bool active = (TIMEFRAMES[i] == mCurrentTf);
        if (active) {
            btn->setStyleSheet(
                QString("QPushButton { background: %1; border: 1px solid %1; color: white; "
                        "border-radius: 6px; font-size: 12px; "
                        "font-family: 'JetBrains Mono', 'Consolas', monospace; "
                        "font-weight: 500; }").arg(accent));
        } else {
            btn->setStyleSheet(
                QString("QPushButton { background: transparent; border: 1px solid %1; "
                        "color: %2; border-radius: 6px; font-size: 12px; "
                        "font-family: 'JetBrains Mono', 'Consolas', monospace; "
                        "font-weight: 500; }"
                        "QPushButton:hover { background: %3; color: %4; }")
                    .arg(border, text, inset, textActive));
        }
    }
}

}  // namespace astra
