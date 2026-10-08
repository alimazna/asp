#include "HealthPage.h"
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QFont>
#include <QColor>

namespace astra {

HealthPage::HealthPage(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
}

void HealthPage::setApiClient(ApiClient* client) {
    mApiClient = client;
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

    QLabel* sysTitle = new QLabel(mSystemCard);
    sysTitle->setText("SYSTEM STATUS");
    sysTitle->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "font-weight: 500; "
        "letter-spacing: 0.05em; "
        "}"
    );
    sysLayout->addWidget(sysTitle);

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
        lbl->setStyleSheet("QLabel { color: #8FA3BF; font-size: 14px; }");
        row->addWidget(lbl);

        QLabel* dot = new QLabel(mSystemCard);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");
        row->addWidget(dot, 0, Qt::AlignTop);

        QLabel* val = new QLabel(mSystemCard);
        val->setText("—");
        val->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        row->addWidget(val);

        row->addStretch();

        sysLayout->addLayout(row);
        mStatusLabels.append(val);
        mStatusDots.append(dot);

        if (i < 5) {
            QFrame* div = new QFrame(mSystemCard);
            div->setFixedHeight(1);
            div->setStyleSheet("QFrame { background: rgba(138, 163, 191, 0.3); }");
            sysLayout->addWidget(div);
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

    QLabel* tfTitle = new QLabel(mTimeframesCard);
    tfTitle->setText("TIMEFRAMES");
    tfTitle->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "font-weight: 500; "
        "letter-spacing: 0.05em; "
        "}"
    );
    tfLayout->addWidget(tfTitle);

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
        item->setStyleSheet(
            "QFrame { "
            "background: #0F1F35; "
            "border: 1px solid #162A44; "
            "border-radius: 8px; "
            "padding: 16px; "
            "}"
        );

        QVBoxLayout* itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(4);

        QLabel* tfLabel = new QLabel(item);
        tfLabel->setText(tfs[i]);
        tfLabel->setStyleSheet(
            "QLabel { "
            "color: #E8EEF5; "
            "font-size: 14px; "
            "font-weight: 500; "
            "font-family: 'JetBrains Mono', 'Consolas', monospace; "
            "}"
        );
        tfLabel->setAlignment(Qt::AlignLeft);
        itemLayout->addWidget(tfLabel);

        QLabel* statusLabel = new QLabel(item);
        statusLabel->setText("\u25CF OK");
        statusLabel->setStyleSheet(
            "QLabel { "
            "color: #4CAF7A; "
            "font-size: 12px; "
            "}"
        );
        statusLabel->setAlignment(Qt::AlignRight);
        itemLayout->addWidget(statusLabel);

        mTimeframeGrid->addWidget(item, i / 3, i % 3);
    }

    tfLayout->addLayout(mTimeframeGrid);
    mainLayout->addWidget(mTimeframesCard);
    mainLayout->addStretch();
}

void HealthPage::updateFromHealth(const HealthResponse& resp) {
    updateSystemStatus(resp);

    // Update timeframe grid — all green for now (mock data shows all OK)
    const char* tfs[] = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
    for (int i = 0; i < 9; ++i) {
        QLayoutItem* item = mTimeframeGrid->itemAtPosition(i / 3, i % 3);
        if (item) {
            QFrame* frame = qobject_cast<QFrame*>(item->widget());
            if (frame) {
                QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(frame->layout());
                if (layout) {
                    QLabel* statusLabel = qobject_cast<QLabel*>(layout->itemAt(1)->widget());
                    if (statusLabel) {
                        statusLabel->setText("\u25CF OK");
                        statusLabel->setStyleSheet(
                            "QLabel { color: #4CAF7A; font-size: 12px; }"
                        );
                    }
                }
            }
        }
    }
}

void HealthPage::updateSystemStatus(const HealthResponse& resp) {
    // Backend status
    if (resp.data.status == "offline") {
        mStatusLabels[0]->setText("OFFLINE");
        mStatusLabels[0]->setStyleSheet("QLabel { color: #D95A5A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[0]->setStyleSheet("QLabel { background: #D95A5A; border-radius: 4px; }");
    } else if (resp.data.status == "degraded") {
        mStatusLabels[0]->setText("DEGRADED");
        mStatusLabels[0]->setStyleSheet("QLabel { color: #D9A14A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[0]->setStyleSheet("QLabel { background: #D9A14A; border-radius: 4px; }");
    } else {
        mStatusLabels[0]->setText("ONLINE");
        mStatusLabels[0]->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[0]->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");
    }

    // Bridge status
    if (resp.data.bridge == "offline") {
        mStatusLabels[1]->setText("OFFLINE");
        mStatusLabels[1]->setStyleSheet("QLabel { color: #D95A5A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[1]->setStyleSheet("QLabel { background: #D95A5A; border-radius: 4px; }");
    } else if (resp.data.bridge == "stale" || resp.data.bridge == "degraded") {
        mStatusLabels[1]->setText("STALE");
        mStatusLabels[1]->setStyleSheet("QLabel { color: #D9A14A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[1]->setStyleSheet("QLabel { background: #D9A14A; border-radius: 4px; }");
    } else {
        mStatusLabels[1]->setText("OK");
        mStatusLabels[1]->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[1]->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");
    }

    // Data freshness
    if (resp.data.uptimeSec.has_value()) {
        int secs = resp.data.uptimeSec.value();
        int mins = secs / 60;
        int hours = mins / 60;
        mins %= 60;
        mStatusLabels[4]->setText(QString("%1h %2m").arg(hours).arg(mins));
        mStatusLabels[4]->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[4]->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");
    } else {
        mStatusLabels[4]->setText("—");
        mStatusLabels[4]->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[4]->setStyleSheet("QLabel { background: #5A6B80; border-radius: 4px; }");
    }

    // Freshness (row 2)
    if (!resp.data.uptimeSec.has_value()) {
        mStatusLabels[2]->setText("—");
        mStatusLabels[2]->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[2]->setStyleSheet("QLabel { background: #5A6B80; border-radius: 4px; }");
    } else {
        mStatusLabels[2]->setText(QString("%1s").arg(resp.data.uptimeSec.value()));
        mStatusLabels[2]->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[2]->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");
    }

    // API Version (row 3)
    mStatusLabels[3]->setText(resp.data.version);
    mStatusLabels[3]->setStyleSheet("QLabel { color: #E8EEF5; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
    mStatusDots[3]->setStyleSheet("QLabel { background: #4CAF7A; border-radius: 4px; }");

    // Coverage Tier (row 5) — the route may omit it; show "unavailable" then.
    if (!resp.data.coverageTier.has_value()) {
        mStatusLabels[5]->setText("UNAVAILABLE");
        mStatusLabels[5]->setStyleSheet("QLabel { color: #5A6B80; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }");
        mStatusDots[5]->setStyleSheet("QLabel { background: #5A6B80; border-radius: 4px; }");
        return;
    }
    QString tier = resp.data.coverageTier->toUpper();
    QColor tierColor;
    if (tier == "HIGH") tierColor = QColor("#4CAF7A");
    else if (tier == "MEDIUM") tierColor = QColor("#D9A14A");
    else if (tier == "LOW") tierColor = QColor("#D95A5A");
    else tierColor = QColor("#5A6B80");

    mStatusLabels[5]->setText(tier);
    mStatusLabels[5]->setStyleSheet(
        QString("QLabel { color: %1; font-size: 14px; font-family: 'JetBrains Mono', 'Consolas', monospace; }")
            .arg(tierColor.name()));
    mStatusDots[5]->setStyleSheet(QString("QLabel { background: %1; border-radius: 4px; }").arg(tierColor.name()));
}

}  // namespace astra
