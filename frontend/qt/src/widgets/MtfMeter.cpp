#include "MtfMeter.h"
#include <QPalette>

namespace astra {

MtfMeter::MtfMeter(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(24);

    mBar = new QLabel(this);
    mBar->setFixedHeight(6);
    mBar->setFixedWidth(0);

    mValueLabel = new QLabel(this);
    mValueLabel->setText("N/A");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(mBar);
    layout->addWidget(mValueLabel);

    restyle();
    setVisible(false);
}

void MtfMeter::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void MtfMeter::restyle() {
    const QPalette pal = palette();
    const QString tertiary = pal.color(QPalette::PlaceholderText).name();
    if (mValue < 0.0) {
        mBar->setStyleSheet(QString("QLabel { background: %1; border-radius: 3px; }").arg(tertiary));
        mValueLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 14px; "
                    "font-family: 'JetBrains Mono', 'Consolas', monospace; }").arg(tertiary));
    } else {
        setAgreement(mValue);
    }
}

void MtfMeter::setAgreement(double value) {
    mValue = value;
    int pct = qRound(qBound(0.0, value, 1.0) * 100.0);
    mValueLabel->setText(QString("%1%").arg(pct));

    // Bar width proportional (max ~120px)
    int barWidth = qRound(pct * 1.2);
    mBar->setFixedWidth(qMax(4, barWidth));

    // Color
    QString color;
    if (pct < 50) {
        color = palette().color(QPalette::PlaceholderText).name();
    } else if (pct < 70) {
        color = "#D9A14A";  // warning
    } else if (pct < 85) {
        color = "#4A90D9";  // accent-blue
    } else {
        color = "#4CAF7A";  // bull
    }

    mBar->setStyleSheet(QString("QLabel { background: %1; border-radius: 3px; }").arg(color));
    mValueLabel->setStyleSheet(
        QString("QLabel { "
                "color: %1; "
                "font-size: 14px; "
                "font-family: 'JetBrains Mono', 'Consolas', monospace; "
                "}")
            .arg(color)
    );
}

}  // namespace astra
