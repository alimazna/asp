// Offscreen regression test for the dashboard chart's cold-start race.
//
// ASTRA's default page is the Dashboard, whose chart card fetches
// /api/v1/candles once, on setApiClient — before the detached backend has bound
// its loopback port. On a real Windows launch that first request hits a socket
// with nothing listening and fails at the transport layer
// ("ui candles: fetch failed (network_error): Connection closed"). The card had
// no retry, so it stayed empty until the next launch: the reported
// "chart never renders" bug.
//
// The fix re-issues the fetch when the backend comes online (DashboardPage's
// setOnline(true), the same call MainWindow makes on ApiClient::online). This
// test drives a server that drops the FIRST connection and answers the SECOND
// with real bars, and asserts the card ends up rendering them. A second case
// asserts a structured failure (a reachable backend that honestly says "no
// data") is terminal and is NOT retried.

#include "pages/DashboardPage.h"
#include "api/ApiClient.h"

#include <QApplication>
#include <QByteArray>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>

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

const char* kCandlesBody =
    "{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":{"
    "\"timeframe\":\"M15\",\"symbol\":\"XAUUSD\",\"count\":1,\"available\":true,"
    "\"bars\":[{\"time\":1700000000,\"open\":1.0,\"high\":2.0,\"low\":0.5,"
    "\"close\":1.5,\"tick_volume\":10,\"spread\":1,\"real_volume\":0}]}}";

const char* kCandlesErrorBody =
    "{\"api\":\"v1\",\"schema\":\"1.0\",\"error\":\"true\","
    "\"code\":\"dependency_unavailable\",\"message\":\"python bridge not reachable\"}";

// Answers every connection with `status` and `body`. Started only once the
// backend is "up"; before that the port has no listener at all, so the client's
// fetch fails with connection-refused — exactly the cold-start race.
class AnswerServer : public QTcpServer {
public:
    AnswerServer(int status, QByteArray body)
        : mStatus(status), mBody(std::move(body)) {
        connect(this, &QTcpServer::newConnection, this, &AnswerServer::onConnection);
    }
    bool start(quint16 port) { return listen(QHostAddress::LocalHost, port); }
    quint16 port() const { return serverPort(); }
    int requestCount() const { return mRequests; }

private:
    void onConnection() {
        QTcpSocket* sock = nextPendingConnection();
        ++mRequests;
        const char* reason = mStatus == 200 ? "OK" : "Service Unavailable";
        QByteArray out = "HTTP/1.1 " + QByteArray::number(mStatus) + " " + reason +
                         "\r\nContent-Type: application/json\r\nContent-Length: " +
                         QByteArray::number(mBody.size()) +
                         "\r\nConnection: close\r\n\r\n" + mBody;
        sock->write(out);
        sock->flush();
        sock->disconnectFromHost();
    }

    int mStatus;
    QByteArray mBody;
    int mRequests = 0;
};

// Reserve an unused localhost port so a client can be pointed at it before any
// listener exists, then a server can bind that exact port later.
quint16 reserveFreePort() {
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

// Spin the event loop for `ms` (or until `stop`), so queued replies are handled.
void pump(int ms, const bool* stop = nullptr) {
    QElapsedTimer clk;
    clk.start();
    while (clk.elapsed() < ms && (stop == nullptr || !*stop)) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Case 1: the cold-start transport failure must be retried and render. ──
    {
        const quint16 port = reserveFreePort();

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(port));

        DashboardPage page;
        page.setApiClient(&client);  // fires the first fetch against no listener

        // The port has no listener: the fetch must fail (connection refused),
        // leaving the default page's chart empty — the reported cold start.
        pump(1000);
        check(page.chartCandleCountForTest() == 0,
              "case1: the raced first fetch left the chart empty");

        // The backend binds now; MainWindow does exactly this on online().
        AnswerServer server(/*status=*/200, kCandlesBody);
        check(server.start(port), "case1: backend binds the port");
        page.setOnline(true);
        pump(1500);

        check(server.requestCount() >= 1,
              "case1: the chart re-fetched once the backend came online");
        check(page.chartCandleCountForTest() == 1,
              "case1: the retry rendered the bars");
    }

    // ── Case 2: an honest structured failure is terminal, not retried. ────────
    {
        const quint16 port = reserveFreePort();
        AnswerServer server(/*status=*/503, kCandlesErrorBody);
        check(server.start(port), "case2: server listens");

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(port));

        DashboardPage page;
        page.setApiClient(&client);
        pump(1000);
        const int afterFirst = server.requestCount();
        check(afterFirst == 1, "case2: the first fetch reached the backend");

        // A reachable backend answered "no data": re-probing it changes nothing.
        page.setOnline(true);
        pump(1000);

        check(server.requestCount() == afterFirst,
              "case2: a structured 'no data' answer is not retried");
        check(page.chartCandleCountForTest() == 0,
              "case2: the chart stays honestly empty (never fabricated)");
    }

    if (g_failures == 0) {
        std::printf("all dashboard cold-start retry checks passed\n");
    } else {
        std::fprintf(stderr, "dashboard-retry: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
