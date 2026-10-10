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

// Simple loopback HTTP server on 8790 that returns the health envelope for every
// request. It can be started late to model a backend that binds the port only
// after a delay.
class MockBackend : public QTcpServer {
public:
    explicit MockBackend(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this]() { onConnection(); });
    }
    bool startListening() { return listen(QHostAddress::LocalHost, 8790); }

private:
    void onConnection() {
        while (hasPendingConnections()) {
            QTcpSocket* s = nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, this, [s]() {
                s->readAll();
                const QByteArray body =
                    "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":"
                    "{\"aggregate\":\"DEGRADED\",\"status\":\"degraded\","
                    "\"bridge\":\"ok\",\"process_state\":\"ONLINE\","
                    "\"degraded_reasons\":[\"no_real_data\"]}}";
                QByteArray resp =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Connection: close\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body;
                s->write(resp);
                s->flush();
                s->disconnectFromHost();
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

    if (g_failures == 0) {
        std::printf("Health retry tests: all passed\n");
    } else {
        std::fprintf(stderr, "Health retry tests: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
