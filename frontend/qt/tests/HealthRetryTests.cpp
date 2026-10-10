// Offscreen regression for the auto-start retry bug.
//
// The bug: the frontend probed 8790 once, right after spawning the backend. The
// backend binds 8790 only after the bridge handshake (2-5s), so that first probe
// always failed and the UI declared OFFLINE forever (never retried). These tests
// drive the real MainWindow::setApiClient path with a mock backend that answers
// only after a delay, and assert the banner does NOT appear and the UI lands on
// DEGRADED once the backend answers.

#include "MainWindow.h"
#include "api/ApiClient.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
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

// Simple loopback HTTP server on 8790. It routes by path so it can model the
// closed-market case the human hit: /health answers DEGRADED (the API server is
// up) while /analysis/latest fails (no closed bar -> dependency_unavailable).
// Before the fix, that analysis failure emitted offline() and painted the whole
// UI OFFLINE even though /health kept answering.
class MockBackend : public QTcpServer {
public:
    explicit MockBackend(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this]() { onConnection(); });
    }
    bool startListening() { return listen(QHostAddress::LocalHost, 8790); }

    // When > 0, /analysis/latest is answered only after this delay, modelling an
    // error that lands AFTER the health success (the window the old code latched).
    int analysisErrorDelayMs = 0;

private:
    // status must be the full status line code + reason, e.g. "200 OK".
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

                if (path.startsWith("/api/v1/analysis/latest")) {
                    const QByteArray err =
                        "{\"api\":\"v1\",\"schema\":\"1.0\",\"error\":\"true\","
                        "\"code\":\"dependency_unavailable\","
                        "\"message\":\"no closed bar for M15 (market closed)\"}";
                    const QByteArray resp = httpResponse(err, "503 Service Unavailable");
                    if (analysisErrorDelayMs > 0) {
                        QTimer::singleShot(analysisErrorDelayMs, this, [this, s, resp]() {
                            writeReply(s, resp);
                        });
                        return;
                    }
                    writeReply(s, resp);
                    return;
                }

                if (path.startsWith("/api/v1/health")) {
                    const QByteArray body =
                        "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":"
                        "{\"aggregate\":\"DEGRADED\",\"status\":\"degraded\","
                        "\"bridge\":\"ok\",\"process_state\":\"ONLINE\","
                        "\"degraded_reasons\":[\"no_real_data\"]}}";
                    writeReply(s, httpResponse(body));
                    return;
                }

                // /bridge/status, /timeframes, /analysis/history, etc.
                const QByteArray body = "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":{}}";
                writeReply(s, httpResponse(body));
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }
};

// Spin the event loop for `ms` so queued network replies and timers fire.
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

    // ── Backend answers only after ~3s: no banner, UI lands on DEGRADED ──────
    {
        MainWindow win;
        ApiClient client;
        win.setBackendAutostartInProgress(true);  // as main() does after a spawn
        win.setApiClient(&client);

        // The backend is not listening yet (models the 2-5s bake-in).
        MockBackend backend;
        QTimer::singleShot(3000, [&backend]() { backend.startListening(); });

        // 1.5s in: still baking in, the banner must NOT be up.
        waitMs(1500);
        check(!win.startupBannerShown(),
              "no red banner while the backend is still binding 8790");
        check(anyLabelContains(&win, "OFFLINE"),
              "UI shows OFFLINE transiently before the backend answers (honest)");

        // Give the backend time to answer and the UI to react.
        waitMs(2500);
        check(!win.startupBannerShown(),
              "no red banner after the backend answers");
        check(anyLabelContains(&win, "DEGRADED"),
              "UI transitions to DEGRADED once the backend answers");
        check(!anyLabelContains(&win, "backend unreachable"),
              "dashboard no longer says backend unreachable");
    }

    // ── Backend never answers: banner appears only after the window expires ──
    {
        MainWindow win;
        ApiClient client;
        win.setAutostartWindowMsForTest(2500);  // shorten the 20s window
        win.setBackendAutostartInProgress(true);
        win.setApiClient(&client);  // nothing listening on 8790

        waitMs(1200);
        check(!win.startupBannerShown(),
              "no banner before the auto-start window expires");

        waitMs(2500);
        check(win.startupBannerShown(),
              "banner appears once the full window expires with no response");
    }

    // ── A failing /analysis/latest must NOT latch the UI OFFLINE ────────────
    // This is the human's exact case: the API server answers /api/v1/health with
    // DEGRADED while the analysis route is unavailable (market closed). Before
    // the fix fetchAnalysisLatest's error emitted offline() and painted the UI
    // OFFLINE even though the backend was up.
    {
        MockBackend backend;
        check(backend.startListening(), "mock backend binds 8790 for the analysis-offline case");
        backend.analysisErrorDelayMs = 800;

        MainWindow win;
        ApiClient client;
        // Steady state (no auto-start): /health polls every 10s, /analysis every
        // 5s. The initial /health succeeds immediately; the /analysis error lands
        // ~800ms later. In the OLD code that error latched the whole UI OFFLINE,
        // and nothing re-probed /health for the next ~10s. Assert well inside that
        // window: the UI must still show DEGRADED.
        win.setApiClient(&client);
        waitMs(2500);

        check(anyLabelContains(&win, "DEGRADED"),
              "UI shows DEGRADED despite a failing /analysis/latest");
        check(!anyLabelContains(&win, "backend unreachable"),
              "a failing /analysis/latest must not say backend unreachable");
        check(!anyLabelContains(&win, "OFFLINE"),
              "a failing /analysis/latest must not show OFFLINE anywhere");
    }

    // ── "reachable after Ns" is measured from the spawn, not the poll ────────
    {
        MockBackend backend;
        check(backend.startListening(), "mock backend binds 8790 for the spawn-clock case");

        MainWindow win;
        ApiClient client;
        // Simulate main() spawning ~30s before setApiClient() runs (a synchronous
        // startup step), then the backend answering immediately.
        win.setBackendAutostartSpawnedAt(
            QDateTime::currentMSecsSinceEpoch() - 30000);
        win.setBackendAutostartInProgress(true);
        win.setApiClient(&client);
        waitMs(1500);
        check(anyLabelContains(&win, "DEGRADED"),
              "backend answering immediately after a long startup gap still recovers");
        check(!win.startupBannerShown(),
              "no failure banner once the backend answers");
    }

    if (g_failures == 0) {
        std::printf("Health retry tests: all passed\n");
    } else {
        std::fprintf(stderr, "Health retry tests: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
