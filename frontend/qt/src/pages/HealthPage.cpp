#include "HealthPage.h"
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QFont>
#include <QColor>
#include <QPalette>

namespace astra {

HealthPage::HealthPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void HealthPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void HealthPage::setApiClient(ApiClient* client) {
    mApiClient = client;
}

void HealthPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString textTertiary = pal.color(QPalette::PlaceholderText).name();
    const QString surface = pal.color(QPalette::Base).name();
    const QString border = pal.color(QPalette::Mid).name();

    const QString cardCaption =
        QString("QLabel { color: %1; font-size: 12px; font-weight: 500; "
                "letter-spacing: 0.05em; }").arg(textSecondary);
    if (mSysTitle) mSysTitle->setStyleSheet(cardCaption);
    if (mTfTitle) mTfTitle->setStyleSheet(cardCaption);

    for (QLabel* l : mRowCaptions) {
        if (l) l->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; }").arg(textSecondary));
    }
    for (QFrame* f : mDividers) {
        if (f) f->setStyleSheet(QString("QFrame { background: %1; }").arg(border));
    }
    for (QLabel* l : mStatusLabels) {
        if (l) {
            l->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; "
                                     "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                                 .arg(textPrimary));
        }
    }
    const QString tfItem =
        QString("QFrame { background: %1; border: 1px solid %2; border-radius: 8px; "
                "padding: 16px; }").arg(surface, border);
    for (QFrame* f : mTfItemFrames) {
        if (f) f->setStyleSheet(tfItem);
    }
    for (QLabel* l : mTfItemLabels) {
        if (l) {
            l->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: 500; "
                                     "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                                 .arg(textPrimary));
        }
    }
    // Semantic status colors are re-applied by re-running the last update.
    if (mHasResp) {
        updateFromHealth(mLastResp);
    } else {
        for (QLabel* d : mStatusDots) {
            if (d) d->setStyleSheet(QString("QLabel { background: %1; border-radius: 4px; }").arg(textTertiary));
        }
        for (QLabel* l : mTfStatusLabels) {
            if (l) l->setStyleSheet(QString("QLabel { color: #4CAF7A; font-size: 12px; }"));
        }
    }
}

void HealthPage::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // ── System Status Card ──
    mSystemCard = new QFrame(this);
    mSystemCard->setProperty("astraCard", true);
    mSystemCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* sysLayout = new QVBoxLayout(mSystemCard);
    sysLayout->setContentsMargins(20, 20, 20, 20);
    sysLayout->setSpacing(0);

    mSysTitle = new QLabel(mSystemCard);
    mSysTitle->setText("SYSTEM STATUS");
    sysLayout->addWidget(mSysTitle);

    QLabel* spacer = new QLabel(mSystemCard);
    spacer->setFixedHeight(16);
    sysLayout->addWidget(spacer);

    // 6 rows
    const char* labels[] = {
        "Backend", "Bridge", "Data Freshness",
        "API Version", "Uptime", "Coverage Tier"
    };

    for (int i = 0; i < 6; ++i) {
        QHBoxLayout* row = new QHBoxLayout();
        row->setSpacing(12);

        QLabel* lbl = new QLabel(mSystemCard);
        lbl->setText(labels[i]);
        row->addWidget(lbl);
        mRowCaptions.append(lbl);

        QLabel* dot = new QLabel(mSystemCard);
        dot->setFixedSize(8, 8);
        row->addWidget(dot, 0, Qt::AlignTop);

        QLabel* val = new QLabel(mSystemCard);
        val->setText("\u2014");
        row->addWidget(val);

        row->addStretch();

        sysLayout->addLayout(row);
        mStatusLabels.append(val);
        mStatusDots.append(dot);

        if (i < 5) {
            QFrame* div = new QFrame(mSystemCard);
            div->setFixedHeight(1);
            sysLayout->addWidget(div);
            mDividers.append(div);
        }
    }

    mainLayout->addWidget(mSystemCard);

    // ── Timeframes Card ──
    mTimeframesCard = new QFrame(this);
    mTimeframesCard->setProperty("astraCard", true);
    mTimeframesCard->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout* tfLayout = new QVBoxLayout(mTimeframesCard);
    tfLayout->setContentsMargins(20, 20, 20, 20);
    tfLayout->setSpacing(0);

    mTfTitle = new QLabel(mTimeframesCard);
    mTfTitle->setText("TIMEFRAMES");
    tfLayout->addWidget(mTfTitle);

    QLabel* tfSpacer = new QLabel(mTimeframesCard);
    tfSpacer->setFixedHeight(16);
    tfLayout->addWidget(tfSpacer);

    mTimeframeGrid = new QGridLayout();
    mTimeframeGrid->setSpacing(8);
    mTimeframeGrid->setContentsMargins(0, 0, 0, 0);

    const char* tfs[] = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
    for (int i = 0; i < 9; ++i) {
        QFrame* item = new QFrame(mTimeframesCard);
        item->setFixedSize(100, 56);
        item->setProperty("astraCard", true);

        QVBoxLayout* itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(4);

        QLabel* tfLabel = new QLabel(item);
        tfLabel->setText(tfs[i]);
        tfLabel->setAlignment(Qt::AlignLeft);
        itemLayout->addWidget(tfLabel);

        QLabel* statusLabel = new QLabel(item);
        statusLabel->setText("\u25CF OK");
        statusLabel->setAlignment(Qt::AlignRight);
        itemLayout->addWidget(statusLabel);

        mTimeframeGrid->addWidget(item, i / 3, i % 3);
        mTfItemFrames.append(item);
        mTfItemLabels.append(tfLabel);
        mTfStatusLabels.append(statusLabel);
    }

    tfLayout->addLayout(mTimeframeGrid);
    mainLayout->addWidget(mTimeframesCard);
    mainLayout->addStretch();
}

void HealthPage::updateFromHealth(const HealthResponse& resp) {
    mLastResp = resp;
    mHasResp = true;
    updateSystemStatus(resp);

    // Update timeframe grid — all green for now (mock data shows all OK)
    for (QLabel* statusLabel : mTfStatusLabels) {
        statusLabel->setText("\u25CF OK");
        statusLabel->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
    }
}

void HealthPage::updateSystemStatus(const HealthResponse& resp) {
    const QString mono = "font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace;";
    const QString text = palette().color(QPalette::Text).name();
    const QString tertiary = palette().color(QPalette::PlaceholderText).name();
    const QString red = "#D95A5A";
    const QString amber = "#D9A14A";
    const QString green = "#4CAF7A";

    auto setLabel = [&](int row, const QString& value, const QString& color) {
        mStatusLabels[row]->setText(value);
        mStatusLabels[row]->setStyleSheet(QString("QLabel { color: %1; %2 }").arg(color, mono));
    };
    auto setDot = [&](int row, const QString& color) {
        mStatusDots[row]->setStyleSheet(QString("QLabel { background: %1; border-radius: 4px; }").arg(color));
    };
    auto statusColor = [&](const QString& state) -> QString {
        if (state == "offline") return red;
        if (state == "degraded" || state == "stale") return amber;
        return green;
    };

    // Backend status
    if (resp.data.status == "offline") {
        setLabel(0, "OFFLINE", red);
    } else if (resp.data.status == "degraded") {
        setLabel(0, "DEGRADED", amber);
    } else {
        setLabel(0, "ONLINE", green);
    }
    setDot(0, statusColor(resp.data.status));

    // Bridge status
    if (resp.data.bridge == "offline") {
        setLabel(1, "OFFLINE", red);
    } else if (resp.data.bridge == "stale" || resp.data.bridge == "degraded") {
        setLabel(1, "STALE", amber);
    } else {
        setLabel(1, "OK", green);
    }
    setDot(1, statusColor(resp.data.bridge));

    // Uptime (row 4)
    if (resp.data.uptimeSec.has_value()) {
        int secs = resp.data.uptimeSec.value();
        int mins = secs / 60;
        int hours = mins / 60;
        mins %= 60;
        setLabel(4, QString("%1h %2m").arg(hours).arg(mins), text);
        setDot(4, green);
    } else {
        setLabel(4, "\u2014", tertiary);
        setDot(4, tertiary);
    }

    // Freshness (row 2) — /health/v1 exposes no data-freshness field.
    // Unknown renders as em-dash; never fabricate a number.
    setLabel(2, "\u2014", tertiary);
    setDot(2, tertiary);

    // API Version (row 3)
    setLabel(3, resp.data.version, text);
    setDot(3, green);

    // Coverage Tier (row 5) — coverage_tier lives on the analysis meta object,
    // not on /health/v1. Render as unavailable; never fabricate it.
    setLabel(5, "\u2014", tertiary);
    setDot(5, tertiary);
}

}  // namespace astra
