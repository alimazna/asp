// Offscreen regression test for the ChartPage stale-reply race.
//
// Sequence: the user switches timeframe (M15 -> M1). The reply for the old
// timeframe then arrives *before* the new one. The old code cleared the
// in-flight flag (mAwaitingCandles) when that stale reply landed, so a genuine
// error for the *current* (M1) request was ignored and the "Loading candles…"
// overlay stayed up until the next 10s poll. The stale reply must not touch the
// current request's in-flight flag.

#include "pages/ChartPage.h"
#include "api/ApiClient.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>
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

QString overlayTitle(ChartPage& page) {
    QFrame* overlay = page.findChild<QFrame*>("chartOverlay");
    if (!overlay) return QString();
    for (QLabel* l : overlay->findChildren<QLabel*>()) {
        const QString t = l->text();
        if (t == QStringLiteral("Loading candles\u2026") ||
            t == QStringLiteral("Candles unavailable")) {
            return t;
        }
    }
    return QString();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ApiClient client;
    ChartPage page;
    page.setApiClient(&client);
    page.resize(1000, 700);
    page.show();

    // Switch to M1; this issues the current in-flight request.
    page.onTimeframeKey(0);  // index 0 == M1

    // A reply for the timeframe we switched away from must be ignored *and*
    // must not clear the current request's in-flight flag.
    CandlesResponse stale;
    stale.data.timeframe = QStringLiteral("M15");
    stale.data.available = false;
    emit client.candlesReceived(stale);

    // Now the *current* M1 request fails. The error must be honoured, replacing
    // the loading overlay with the error overlay.
    emit client.error(QStringLiteral("backend unreachable"),
                      QStringLiteral("network_error"));

    const QString title = overlayTitle(page);
    check(!title.isEmpty(), "overlay title found");
    check(title == QStringLiteral("Candles unavailable"),
          "genuine current-tf error replaces loading overlay");
    check(title != QStringLiteral("Loading candles\u2026"),
          "loading overlay not stranded after stale reply");

    if (g_failures == 0) {
        std::printf("all chart stale-reply checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
