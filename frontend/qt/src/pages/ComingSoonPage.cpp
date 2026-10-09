#include "ComingSoonPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>

namespace astra {

ComingSoonPage::ComingSoonPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("comingSoonPage");  // stable handle for tests
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(8);
    layout->addStretch();

    QHBoxLayout* row = new QHBoxLayout();
    row->addStretch();

    QVBoxLayout* center = new QVBoxLayout();
    center->setSpacing(10);

    mTitle = new QLabel(this);
    mTitle->setObjectName("comingSoonTitle");
    mTitle->setAlignment(Qt::AlignHCenter);
    center->addWidget(mTitle);

    mBadge = new QLabel(this);
    mBadge->setText("COMING SOON");
    mBadge->setAlignment(Qt::AlignHCenter);
    mBadge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    center->addWidget(mBadge, 0, Qt::AlignHCenter);

    mBody = new QLabel(this);
    mBody->setObjectName("comingSoonBody");
    mBody->setText("Enabled when the backend module ships.");
    mBody->setAlignment(Qt::AlignHCenter);
    center->addWidget(mBody, 0, Qt::AlignHCenter);

    row->addLayout(center);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();

    restyle();
}

void ComingSoonPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void ComingSoonPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString surface = pal.color(QPalette::Base).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString accent = pal.color(QPalette::Highlight).name();

    mTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 22px; font-weight: 600; "
                                  "letter-spacing: 1px; }").arg(textPrimary));
    mBadge->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 500; "
                                  "letter-spacing: 0.08em; border: 1px solid %2; "
                                  "border-radius: 8px; padding: 4px 12px; background: %3; }")
                              .arg(accent, border, surface));
    mBody->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; }").arg(textSecondary));
}

void ComingSoonPage::setModuleName(const QString& name) {
    mTitle->setText(name);
}

}  // namespace astra
