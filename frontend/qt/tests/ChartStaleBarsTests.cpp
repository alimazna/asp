// Offscreen regression for the closed-market candle path (Bug 1, frontend half).
//
// The field symptom was: the chart showed "No candle data available — Connect to
// backend" and never rendered, while the backend was healthy. Root cause: with
// the market closed the bridge refuses a strict candle fetch with
// MARKET_DATA_STALE, so no bars ever reached the chart.
//
// The backend now answers allow_stale=true with the last real bars, flagged
// stale. ChartPage must render those bars and show the non-blocking
// "Market closed — showing last available data" notice instead of the dead-end
// empty overlay.

#include "api/ApiClient.h"
#include "pages/ChartPage.h"

#include <QApplication>
#include <QLabel>

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

CandlesResponse makeStaleResponse() {
    CandlesResponse resp;
    resp.data.timeframe = QStringLiteral("M15");
    resp.data.symbol = QStringLiteral("XAUUSD");
    resp.data.freshness = QStringLiteral("STALE");
    resp.data.stale = true;
    resp.data.ageSeconds = 7200;
    for (int i = 0; i < 5; ++i) {
        Candle c;
        c.time = 1700000000 + i * 900;
        c.open = 2000.0 + i;
        c.high = 2001.0 + i;
        c.low = 1999.0 + i;
        c.close = 2000.5 + i;
        c.tickVolume = 10 + i;
        resp.data.bars.append(c);
    }
    resp.data.available = true;
    return resp;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Stale bars render with the closed-market notice (no dead-end). ──────
    {
        ApiClient client;
        ChartPage page;
        page.setApiClient(&client);
        page.resize(1000, 700);
        page.show();

        emit client.candlesReceived(makeStaleResponse());

        check(page.candleCountForTest() == 5, "stale bars are rendered");
        check(!page.overlayShownForTest(),
              "no empty/error overlay while stale bars are shown");
        const QString notice = page.staleNoticeTextForTest();
        check(!notice.isEmpty(), "closed-market notice is shown");
        check(notice.contains(QStringLiteral("Market closed")),
              "notice names the closed market");
        check(notice.contains(QStringLiteral("last available")),
              "notice says the last available data is shown");
    }

    // ── A live (fresh) reply shows no notice and still renders. ─────────────
    {
        ApiClient client;
        ChartPage page;
        page.setApiClient(&client);
        page.resize(1000, 700);
        page.show();

        CandlesResponse fresh = makeStaleResponse();
        fresh.data.stale = false;
        fresh.data.freshness = QStringLiteral("FRESH");
        fresh.data.ageSeconds = -1;
        emit client.candlesReceived(fresh);

        check(page.candleCountForTest() == 5, "fresh bars are rendered");
        check(page.staleNoticeTextForTest().isEmpty(),
              "no closed-market notice for a live market");
    }

    if (g_failures == 0)
        std::printf("all chart stale-bars checks passed\n");
    else
        std::fprintf(stderr, "chart stale-bars: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
