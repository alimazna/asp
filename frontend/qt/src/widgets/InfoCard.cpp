#include "InfoCard.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPalette>
#include <QFont>
#include <QTableWidget>
#include <QHeaderView>

namespace astra {

QString orDash(const QString& value) {
    return value.isEmpty() ? QString::fromUtf8(kEmDash) : value;
}

QString orUnavailable(bool have) {
    return have ? QString::fromUtf8(kEmDash) : QStringLiteral("unavailable");
}

QLabel* makeEmptyState(QWidget* parent, const QString& text) {
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("astraEmptyState");
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

void styleLedgerTable(QTableWidget* table) {
    if (!table) return;
    const QPalette pal = table->palette();
    const QString surface = pal.color(QPalette::Base).name();
    const QString surfaceAlt = pal.color(QPalette::AlternateBase).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    table->setStyleSheet(
        QString("QTableWidget { background: %1; border: 1px solid %2; "
                "border-radius: 8px; gridline-color: %2; }"
                "QTableWidget::item { padding: 6px 10px; border-bottom: 1px solid %2; "
                "font-size: 12px; font-family: 'JetBrains Mono', 'Consolas', monospace; }"
                "QTableWidget::item:alternate { background: %3; }")
            .arg(surface, border, surfaceAlt));
    table->horizontalHeader()->setStyleSheet(
        QString("QHeaderView::section { background: %1; color: %2; padding: 8px 10px; "
                "border: none; border-bottom: 1px solid %3; font-size: 11px; "
                "font-weight: 500; }").arg(surfaceAlt, textSecondary, border));
}

QTableWidget* makeLedgerTable(QWidget* parent, const QStringList& headers,
                              int minHeight) {
    auto* table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumHeight(minHeight);
    styleLedgerTable(table);
    return table;
}

void fillLedgerEmpty(QTableWidget* table, int cols, const QString& text) {
    table->setRowCount(1);
    table->setSpan(0, 0, 1, cols);
    table->setItem(0, 0, new QTableWidgetItem(text));
}

InfoCard::InfoCard(QWidget* parent)
    : QFrame(parent)
{
    setProperty("astraCard", true);
    setFrameStyle(QFrame::NoFrame);

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 18, 20, 18);
    outer->setSpacing(0);

    mTitle = new QLabel(this);
    mTitle->setProperty("astraStyle", "cardCaption");
    outer->addWidget(mTitle);
    outer->addSpacing(12);

    mBody = new QVBoxLayout();
    mBody->setContentsMargins(0, 0, 0, 0);
    mBody->setSpacing(0);
    outer->addLayout(mBody);

    restyle();
}

void InfoCard::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QFrame::changeEvent(event);
}

void InfoCard::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString border = pal.color(QPalette::Mid).name();

    if (mTitle) {
        mTitle->setStyleSheet(
            QString("QLabel { color: %1; font-size: 12px; font-weight: 500; "
                    "letter-spacing: 0.05em; }").arg(textSecondary));
    }
    for (QLabel* l : mRowCaptions) {
        if (l) l->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; }")
                                    .arg(textSecondary));
    }
    for (QLabel* l : mRowValues) {
        if (l) l->setStyleSheet(
            QString("QLabel { color: %1; font-size: 13px; "
                    "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                .arg(textPrimary));
    }
    for (QFrame* f : mDividers) {
        if (f) f->setStyleSheet(QString("QFrame { background: %1; }").arg(border));
    }
    for (QLabel* l : findChildren<QLabel*>("astraEmptyState")) {
        l->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; }")
                             .arg(textSecondary));
    }
}

void InfoCard::setTitle(const QString& title) {
    mTitle->setText(title);
}

QLabel* InfoCard::addCaption(const QString& caption) {
    QLabel* label = new QLabel(caption, this);
    label->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; font-weight: 500; "
                "letter-spacing: 0.05em; }")
            .arg(palette().color(QPalette::PlaceholderText).name()));
    mBody->addSpacing(6);
    mBody->addWidget(label);
    mRowCaptions.append(label);
    return label;
}

QLabel* InfoCard::addRow(const QString& caption, const QString& value) {
    QWidget* row = new QWidget(this);
    QHBoxLayout* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 7, 0, 7);
    lay->setSpacing(12);

    QLabel* cap = new QLabel(caption, row);
    lay->addWidget(cap);
    mRowCaptions.append(cap);

    lay->addStretch();

    QLabel* val = new QLabel(value.isEmpty() ? QString::fromUtf8(kEmDash) : value, row);
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(val);
    mRowValues.append(val);

    mBody->addWidget(row);

    QFrame* div = new QFrame(this);
    div->setFixedHeight(1);
    mBody->addWidget(div);
    mDividers.append(div);

    restyle();
    return val;
}

void InfoCard::addWidget(QWidget* widget) {
    mBody->addWidget(widget);
}

}  // namespace astra
