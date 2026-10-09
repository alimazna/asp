#include "KnowledgePage.h"
#include "widgets/InfoCard.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QPalette>

namespace astra {

KnowledgePage::KnowledgePage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void KnowledgePage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) restyle();
    QWidget::changeEvent(event);
}

void KnowledgePage::setupLayout() {
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(12);

    mTitle = new QLabel("KNOWLEDGE", this);
    outer->addWidget(mTitle);

    mSubtitle = new QLabel(
        "Static reference. No knowledge endpoint exists in API v1 \u2014 this page "
        "documents the concepts the system actually uses.", this);
    mSubtitle->setWordWrap(true);
    outer->addWidget(mSubtitle);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget* inner = new QWidget(scroll);
    QVBoxLayout* col = new QVBoxLayout(inner);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(12);

    auto card = [&](const QString& title,
                    const QVector<QPair<QString, QString>>& rows) {
        InfoCard* c = new InfoCard(inner);
        c->setTitle(title);
        for (const auto& r : rows) c->addRow(r.first, r.second);
        col->addWidget(c);
    };

    card("SCORE VS PROBABILITY", {
        {"Score", "0\u20131 ranking value; always present"},
        {"Probability", "unavailable until calibrated \u2014 never shown as a score"},
        {"Rule", "a score is never labelled a probability"},
    });

    card("CLOSED-BAR CAUSALITY", {
        {"Input", "closed bars only; the forming bar is never consumed"},
        {"Look-ahead", "prohibited \u2014 no future bar informs a decision"},
        {"Timeframes", "M1 M5 M15 M30 H1 H4 D1 W1 MN1"},
    });

    card("MTF & REGIME", {
        {"H4 bias", "higher-timeframe directional context"},
        {"M15 trigger", "lower-timeframe entry trigger"},
        {"Agreement", "unavailable in v1 on uncalibrated periods"},
        {"Regime", "UNKNOWN / RANGE / TREND / VOLATILE / QUIET"},
    });

    card("MODES & AUTHORITY", {
        {"Mode", "SHADOW only"},
        {"Live execution", "not authorised in this build"},
        {"Authority", "human approval is the only path to any change"},
    });

    col->addStretch();
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
}

void KnowledgePage::restyle() {
    const QPalette pal = palette();
    if (mTitle) {
        mTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 16px; "
                                      "font-weight: 600; }")
                                  .arg(pal.color(QPalette::Text).name()));
    }
    if (mSubtitle) {
        mSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; }")
                                     .arg(pal.color(QPalette::WindowText).name()));
    }
}

}  // namespace astra
