// Offscreen widget test for SignalCard honesty on a missing score.
//
// Regression teeth for the honesty rule: ApiClient used to substitute score=0.5
// when the backend omitted "score", so the card rendered a fabricated "50%".
// Now the score is optional and an absent score must render "\u2014" with no bar.

#include "widgets/SignalCard.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QStringList>

#include <cstdio>

using namespace astra;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    if (!ok) {
        std::fprintf(stderr, "  CHECK failed: %s\n", name);
        ++g_failures;
    } else {
        std::printf("[PASS] %s\n", name);
    }
}

QStringList labelTexts(const SignalCard& card) {
    QStringList out;
    for (QLabel* l : card.findChildren<QLabel*>()) {
        out << l->text();
    }
    return out;
}

bool anyContains(const QStringList& texts, const QString& needle) {
    for (const QString& t : texts) {
        if (t.contains(needle)) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Missing score + uncalibrated probability → no fabricated percentage.
    {
        SignalCard card;
        Signal signal;
        signal.direction = "UP";
        signal.probabilityCalibrated = false;
        signal.score = std::nullopt;  // backend omitted it
        Meta meta;
        card.updateFromSignal(signal, meta);

        const QStringList texts = labelTexts(card);
        check(!anyContains(texts, "50%"), "missing score is not a fabricated 50%");
        check(!anyContains(texts, "%"), "missing score renders no percentage");
        check(anyContains(texts, QString::fromUtf8("\u2014")),
              "missing score renders an em dash");

        bool barVisible = false;
        for (QProgressBar* bar : card.findChildren<QProgressBar*>()) {
            if (bar->isVisible()) barVisible = true;
        }
        check(!barVisible, "no score bar is shown when the score is absent");
    }

    // A present score still renders its percentage.
    {
        SignalCard card;
        Signal signal;
        signal.direction = "UP";
        signal.probabilityCalibrated = false;
        signal.score = 0.42;
        Meta meta;
        card.updateFromSignal(signal, meta);
        check(anyContains(labelTexts(card), "42%"), "present score renders 42%");
    }

    // A degraded signal mutes the value; a later healthy signal must clear it
    // (the amber style used to stick until the next palette change).
    {
        SignalCard card;
        Signal signal;
        signal.direction = "UP";
        signal.score = 0.7;

        Meta degraded;
        degraded.degraded = true;
        card.updateFromSignal(signal, degraded);

        Meta healthy;
        healthy.degraded = false;
        card.updateFromSignal(signal, healthy);

        bool amberStuck = false;
        for (QLabel* l : card.findChildren<QLabel*>()) {
            if (l->text().contains("70%") && l->styleSheet().contains("#D9A14A")) {
                amberStuck = true;
            }
        }
        check(!amberStuck, "degraded value style is cleared on a healthy update");
    }

    if (g_failures == 0) {
        std::printf("SignalCard score-honesty tests: all passed\n");
    } else {
        std::fprintf(stderr, "SignalCard score-honesty tests: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
