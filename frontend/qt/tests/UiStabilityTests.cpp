// Offscreen regression for the UI flip-flop + chart-data bug (Bug 1).
//
// The real log showed the UI alternating "ui health ok" / "ui health offline"
// every ~10s while curl to /api/v1/health ALWAYS succeeded: a single slow/timed
// out probe flipped the whole UI OFFLINE, so the Chart never had a stable window
// to fetch and render candles. These tests drive the real ApiClient + ChartPage
// path with a mock backend that answers health on every OTHER probe, and pin:
//   - one failed probe keeps the UI DEGRADED (no OFFLINE flip),
//   - two CONSECUTIVE failures do flip to OFFLINE,
//   - the chart keeps its rendered candles across a transient flip.

#include "api/ApiClient.h"
#include "pages/ChartPage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

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

bool anyLabelContains(QWidget* root, const QString& needle) {
    for (QLabel* l : root->findChildren<QLabel*>())
        if (l->text().contains(needle)) return true;
    return false;
}

// Loopback HTTP server on 8790. /health answers only on every OTHER probe when
// flapHealth is set (the "success / timeout / success / timeout ..." pattern).
// It never answers when dropHealth is set (models two consecutive failures).
class MockBackend : public QTcpServer {
public:
    explicit MockBackend(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this]() { onConnection(); });
    }
    bool startListening() { return listen(QHostAddress::LocalHost, 8790); }

    bool flapHealth = false;   // answer health on every other probe
    bool dropHealth = false;   // never answer health (hold the socket open)

private:
    static QByteArray httpResponse(const QByteArray& body, const char* status = "200 OK") {
        return QByteArray("HTTP/1.1 ") + status + "\r\nContent-Type: application/json\r\n"
               "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) +
               "\r\n\r\n" + body;
    }

    void writeReply(QTcpSocket* s, const QByteArray& resp) {
        s->write(resp);
        s->flush();
        s->disconnectFromHost();
    }

    void onConnection() {
        while (hasPendingConnections()) {
            QTcpSocket* s = nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, this, [this, s]() {
                const QByteArray req = s->readAll();
                const QByteArray path = req.split(' ').value(1);

                if (path.startsWith("/api/v1/health")) {
                    if (dropHealth) return;  // hold open: the client times out
                    if (flapHealth) {
                        mHealthProbes++;
                        if (mHealthProbes % 2 == 0) return;  // every other probe: no answer
                    }
                    const QByteArray body =
                        "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":"
                        "{\"aggregate\":\"DEGRADED\",\"status\":\"degraded\","
                        "\"bridge\":\"ok\",\"process_state\":\"ONLINE\","
                        "\"degraded_reasons\":[\"no_real_data\"]}}";
                    writeReply(s, httpResponse(body));
                    return;
                }

                if (path.startsWith("/api/v1/candles")) {
                    QByteArray bars;
                    for (int i = 0; i < 5; ++i) {
                        if (i) bars += ",";
                        bars += "{\"time\":" + QByteArray::number(1700000000 + i * 900) +
                                ",\"open\":100,\"high\":101,\"low\":99,\"close\":100.5,"
                                "\"tick_volume\":10,\"spread\":1,\"real_volume\":0}";
                    }
                    const QByteArray body =
                        "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":"
                        "{\"timeframe\":\"M15\",\"symbol\":\"EURUSD\",\"freshness\":\"FRESH\","
                        "\"count\":5,\"bars\":[" + bars + "]}}";
                    writeReply(s, httpResponse(body));
                    return;
                }

                writeReply(s, httpResponse("{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":{}}"));
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

    int mHealthProbes = 0;
};

void waitMs(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Flapping health: one failed probe must NOT flip the UI OFFLINE, and ──
    // the chart must render and KEEP its candles across the transient flip.
    {
        MockBackend backend;
        check(backend.startListening(), "mock backend binds 8790");
        backend.flapHealth = true;

        ApiClient client;
        ChartPage chart;
        chart.resize(800, 600);
        chart.setApiClient(&client);
        chart.show();
        waitMs(400);  // let the initial candle fetch land

        check(chart.candleCountForTest() > 0, "chart rendered candles");
        const int barsBefore = chart.candleCountForTest();

        // Drive health probes directly: probe 1 answers, probe 2 is dropped
        // (timeout), probe 3 answers, probe 4 is dropped, probe 5 answers.
        for (int i = 0; i < 5; ++i) {
            client.cancelHealthProbe();
            client.fetchHealth();
            waitMs(600);  // well under the 5s transfer timeout for the answered ones
        }

        check(client.isOnline(), "client stays online across flapping health");
        check(client.consecutiveHealthFailures() == 0,
              "a success after a failure resets the strike count");
        check(chart.candleCountForTest() == barsBefore,
              "chart keeps its candles across the transient health flip");
        check(!chart.overlayShownForTest(), "no error overlay while candles are rendered");
    }

    // ── Two CONSECUTIVE failures do flip the UI OFFLINE. ────────────────────
    {
        MockBackend backend;
        check(backend.startListening(), "mock backend binds 8790 for the outage case");
        backend.dropHealth = true;  // never answer: every probe times out

        ApiClient client;
        bool wentOffline = false;
        QObject::connect(&client, &ApiClient::offline, [&]() { wentOffline = true; });

        client.fetchHealth();
        waitMs(5600);  // first probe times out (5s), strike 1
        check(!wentOffline, "a single failed probe does not flip offline");
        check(client.consecutiveHealthFailures() == 1, "one strike after one failure");

        client.cancelHealthProbe();
        client.fetchHealth();
        waitMs(5600);  // second probe times out, strike 2 -> offline
        check(wentOffline, "two consecutive failures do flip offline");
        check(!client.isOnline(), "client reports offline after two strikes");
    }

    // ── A successful data fetch must NOT resurrect liveness. ────────────────
    // Only /api/v1/health owns mIsOnline. After two health strikes the client is
    // OFFLINE; a successful /candles, /timeframes or /analysis/history must not
    // set mIsOnline back to true. Before the fix each of those set
    // mIsOnline = true on success, so a working chart masked a dead health probe
    // and the UI oscillated ONLINE <-> OFFLINE.
    {
        MockBackend backend;
        check(backend.startListening(), "mock backend binds 8790 for the liveness-ownership case");
        backend.dropHealth = true;  // /health never answers; data routes still do

        ApiClient client;
        bool wentOffline = false;
        QObject::connect(&client, &ApiClient::offline, [&]() { wentOffline = true; });

        client.fetchHealth();
        waitMs(5600);  // strike 1
        client.cancelHealthProbe();
        client.fetchHealth();
        waitMs(5600);  // strike 2 -> offline
        check(wentOffline && !client.isOnline(),
              "client offline after two consecutive health strikes");

        // These all answer successfully against the mock while /health stays dead.
        client.fetchCandles("M15", 500);
        client.fetchTimeframes();
        client.fetchAnalysisHistory(20);
        waitMs(500);

        check(!client.isOnline(),
              "a successful /candles, /timeframes or /analysis/history must not set liveness");
    }

    if (g_failures == 0)
        std::printf("UI stability tests: all passed\n");
    else
        std::fprintf(stderr, "UI stability tests: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
