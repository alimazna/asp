// Offscreen widget tests for HealthPage status rendering.
//
// Regression teeth for the honesty rule: an unrecognised/absent backend or
// bridge status must render unavailable ("\u2014"), never a fabricated green
// "ONLINE"/"OK". The page previously fell through to the OK branch for any
// token it did not recognise.

#include "pages/HealthPage.h"

#include <QApplication>
#include <QLabel>
#include <QStringList>

#include <cstdio>

using namespace astra;

namespace {

int g_failures = 0;

void check(bool ok, const char* name, const QString& detail = QString()) {
    if (!ok) {
        std::fprintf(stderr, "  CHECK failed: %s %s\n", name, qPrintable(detail));
        ++g_failures;
    } else {
        std::printf("[PASS] %s\n", name);
    }
}

QStringList statusTexts(const HealthPage& page) {
    QStringList out;
    for (QLabel* l : page.findChildren<QLabel*>()) {
        out << l->text();
    }
    return out;
}

bool contains(const QStringList& texts, const QString& needle) {
    for (const QString& t : texts) {
        if (t.contains(needle)) return true;
    }
    return false;
}

HealthResponse makeResponse(const QString& status, const QString& bridge) {
    HealthResponse r;
    r.data.status = status;
    r.data.bridge = bridge;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString em = QString::fromUtf8("\u2014");

    // Happy path: recognised tokens render ONLINE / OK.
    {
        HealthPage page;
        page.updateFromHealth(makeResponse("ok", "ok"));
        const QStringList texts = statusTexts(page);
        check(contains(texts, "ONLINE"), "ok status renders ONLINE");
        check(contains(texts, "OK"), "ok bridge renders OK");
    }

    // Degraded/offline are distinct, honest states.
    {
        HealthPage page;
        page.updateFromHealth(makeResponse("degraded", "stale"));
        const QStringList texts = statusTexts(page);
        check(contains(texts, "DEGRADED"), "degraded status renders DEGRADED");
        check(contains(texts, "STALE"), "stale bridge renders STALE");
    }
    {
        HealthPage page;
        page.updateFromHealth(makeResponse("offline", "offline"));
        const QStringList texts = statusTexts(page);
        check(contains(texts, "OFFLINE"), "offline renders OFFLINE");
    }

    // Unknown/absent status must NOT be claimed as safe.
    {
        HealthPage page;
        page.updateFromHealth(makeResponse("bogus", "weird"));
        const QStringList texts = statusTexts(page);
        check(!contains(texts, "ONLINE"), "unknown status is not ONLINE");
        check(!contains(texts, "OK"), "unknown bridge is not OK");
        check(contains(texts, em), "unknown status renders unavailable dash");
    }

    if (g_failures == 0) {
        std::printf("HealthPage status tests: all passed\n");
    } else {
        std::fprintf(stderr, "HealthPage status tests: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
