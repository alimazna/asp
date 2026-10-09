#include "SignalCard.h"
#include <QTimer>
#include <QFont>
#include <QPalette>

namespace astra {

SignalCard::SignalCard(QWidget* parent)
    : QWidget(parent)
{
    setupLayout();
    restyle();
}

void SignalCard::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void SignalCard::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString textTertiary = pal.color(QPalette::PlaceholderText).name();
    const QString surfaceAlt = pal.color(QPalette::AlternateBase).name();

    mLabelLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; "
                                       "font-weight: 500; letter-spacing: 0.05em; }")
                                   .arg(textSecondary));
    mValueLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 32px; "
                                       "font-weight: 600; "
                                       "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                                   .arg(textPrimary));
    mBar->setStyleSheet(QString("QProgressBar { background: %1; border: none; "
                                "border-radius: 3px; height: 6px; }"
                                "QProgressBar::chunk { background: %2; border-radius: 3px; }")
                            .arg(surfaceAlt, textTertiary));
    mConfidenceLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; "
                                            "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                                        .arg(textSecondary));
    mHorizonLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textSecondary));
    mModelLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textTertiary));
    mUpdatedLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textSecondary));

    if (mHasSignal) {
        updateFromSignal(mLastSignal, mLastMeta);
    }
}

void SignalCard::setupLayout() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // Direction indicator — large ▲/▼/—
    mDirectionLabel = new QLabel(this);
    mDirectionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(mDirectionLabel);

    // Probability / Score
    QHBoxLayout* scoreRow = new QHBoxLayout();
    scoreRow->setSpacing(12);

    mLabelLabel = new QLabel(this);
    mLabelLabel->setText("SCORE");
    scoreRow->addWidget(mLabelLabel);

    mValueLabel = new QLabel(this);
    mValueLabel->setText("\u2014");
    scoreRow->addWidget(mValueLabel);

    layout->addLayout(scoreRow);

    // Bar
    mBar = new QProgressBar(this);
    mBar->setMaximum(100);
    mBar->setFixedHeight(6);
    layout->addWidget(mBar);

    // Confidence interval — hidden in uncalibrated mode
    mConfidenceLabel = new QLabel(this);
    mConfidenceLabel->setText("[\u2014 \u2014]");
    mConfidenceLabel->setVisible(false);
    layout->addWidget(mConfidenceLabel);

    // Horizon — hidden in uncalibrated mode
    mHorizonLabel = new QLabel(this);
    mHorizonLabel->setText("Horizon: \u2014");
    mHorizonLabel->setVisible(false);
    layout->addWidget(mHorizonLabel);

    // Model version — hidden in uncalibrated mode
    mModelLabel = new QLabel(this);
    mModelLabel->setText("Model: \u2014");
    mModelLabel->setVisible(false);
    layout->addWidget(mModelLabel);

    // Updated timestamp
    mUpdatedLabel = new QLabel(this);
    mUpdatedLabel->setText("Updated: just now");
    layout->addWidget(mUpdatedLabel);

    layout->addStretch();
}

void SignalCard::updateFromSignal(const Signal& signal, const Meta& meta) {
    mLastSignal = signal;
    mLastMeta = meta;
    mHasSignal = true;
    updateDirection(signal.direction);
    updateScoreOrProbability(signal, meta);

    // Confidence, horizon, model version only shown when calibrated
    if (signal.probabilityCalibrated) {
        updateConfidence(signal);
        updateHorizon(signal);
        updateModelVersion(signal);
        mConfidenceLabel->setVisible(true);
        mHorizonLabel->setVisible(true);
        mModelLabel->setVisible(true);
    } else {
        mConfidenceLabel->setVisible(false);
        mHorizonLabel->setVisible(false);
        mModelLabel->setVisible(false);
    }

    // Updated time
    if (meta.dataFreshnessSec.has_value()) {
        mLastUpdateSec = meta.dataFreshnessSec.value();
        mUpdatedLabel->setText(QString("Updated: %1s ago").arg(mLastUpdateSec));
    } else {
        mUpdatedLabel->setText("Updated: \u2014");
    }
}

void SignalCard::updateDirection(const QString& direction) {
    const QString tertiary = palette().color(QPalette::PlaceholderText).name();
    if (direction == "UP" || direction == "LONG") {
        mDirectionLabel->setText("\u25B2 UP");
        mDirectionLabel->setStyleSheet("QLabel { font-size: 32px; font-weight: 600; color: #4CAF7A; }");
    } else if (direction == "DOWN" || direction == "SHORT") {
        mDirectionLabel->setText("\u25BC DOWN");
        mDirectionLabel->setStyleSheet("QLabel { font-size: 32px; font-weight: 600; color: #D95A5A; }");
    } else if (direction == "FLAT") {
        mDirectionLabel->setText("\u2014 FLAT");
        mDirectionLabel->setStyleSheet(QString("QLabel { font-size: 32px; font-weight: 600; color: %1; }").arg(tertiary));
    } else {
        mDirectionLabel->setText("\u2014 NONE");
        mDirectionLabel->setStyleSheet(QString("QLabel { font-size: 32px; font-weight: 600; color: %1; }").arg(tertiary));
    }
}

void SignalCard::updateScoreOrProbability(const Signal& signal, const Meta& meta) {
    const QString surfaceAlt = palette().color(QPalette::AlternateBase).name();
    const QString textTertiary = palette().color(QPalette::PlaceholderText).name();
    auto barStyle = [&](const QString& chunk) {
        return QString("QProgressBar { background: %1; border: none; border-radius: 3px; "
                       "height: 6px; }"
                       "QProgressBar::chunk { background: %2; border-radius: 3px; }")
            .arg(surfaceAlt, chunk);
    };
    auto chunkFor = [&](int pct) -> QString {
        if (pct < 55) return textTertiary;
        if (pct < 60) return QString("#D9A14A");
        if (pct < 65) return QString("#4A90D9");
        return QString("#4CAF7A");
    };

    if (signal.probabilityCalibrated && signal.probability.has_value()) {
        // Show probability
        double prob = signal.probability.value();
        int pct = qRound(qBound(0.0, prob, 1.0) * 100.0);
        mLabelLabel->setText("PROBABILITY");
        mValueLabel->setText(QString("%1%").arg(pct));
        mBar->setStyleSheet(barStyle(chunkFor(pct)));
        mBar->setValue(pct);
        mBar->setVisible(true);
    } else {
        // Show score (uncalibrated or missing probability)
        double score = signal.score;
        int pct = qRound(qBound(0.0, score, 1.0) * 100.0);
        mLabelLabel->setText("SCORE");
        mValueLabel->setText(QString("%1%").arg(pct));
        mBar->setStyleSheet(barStyle(chunkFor(pct)));
        mBar->setValue(pct);
        mBar->setVisible(true);
    }

    // Mute styling if degraded
    if (meta.degraded) {
        mValueLabel->setStyleSheet(
            "QLabel { "
            "color: #D9A14A; "
            "font-size: 32px; "
            "font-weight: 600; "
            "font-family: 'JetBrains Mono', 'Consolas', monospace; "
            "}"
        );
    }
}

void SignalCard::updateConfidence(const Signal& signal) {
    if (signal.confidenceLo.has_value() && signal.confidenceHi.has_value()) {
        double lo = signal.confidenceLo.value();
        double hi = signal.confidenceHi.value();
        mConfidenceLabel->setText(
            QString("[%1 \u2014 %2]")
                .arg(QString::number(lo, 'f', 2))
                .arg(QString::number(hi, 'f', 2))
        );
        mConfidenceLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 14px; "
                    "font-family: 'JetBrains Mono', 'Consolas', monospace; }")
                .arg(palette().color(QPalette::WindowText).name())
        );
    } else {
        mConfidenceLabel->setText("[\u2014 \u2014]");
    }
}

void SignalCard::updateHorizon(const Signal& signal) {
    if (signal.horizon.has_value()) {
        QString h = signal.horizon.value();
        mHorizonLabel->setText(QString("Horizon: %1").arg(h));
    } else {
        mHorizonLabel->setText("Horizon: \u2014");
    }
}

void SignalCard::updateModelVersion(const Signal& signal) {
    if (signal.modelVersion.has_value()) {
        mModelLabel->setText(QString("Model: %1").arg(signal.modelVersion.value()));
        mModelLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 12px; }")
                .arg(palette().color(QPalette::PlaceholderText).name())
        );
    } else {
        mModelLabel->setText("Model: \u2014");
    }
}

}  // namespace astra
