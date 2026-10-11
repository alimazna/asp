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

// The backend returns this 503 while the bridge is still handshaking: the API
// is up (so /health answers) but the /candles proxy cannot reach the bridge
// yet. It is transient by definition and must be retried, or the cold-start
// chart stays empty until the next launch.
const char* kBridgeWarmingBody =
    "{\"api\":\"v1\",\"schema\":\"1.0\",\"error\":\"true\","
    "\"code\":\"dependency_unavailable\",\"message\":\"python bridge not reachable\"}";

// A reachable, fully-started bridge that honestly reports MT5 has no terminal
// or cannot resolve the symbol. Retrying changes nothing, so this is terminal.
const char* kNoTerminalBody =
    "{\"api\":\"v1\",\"schema\":\"1.0\",\"error\":\"true\","
    "\"code\":\"MT5_TERMINAL_UNAVAILABLE\",\"message\":\"no MT5 terminal\"}";

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

    // Answer a fixed number of requests with `warmStatus`/`warmBody`, then
    // answer `mStatus`/`mBody` forever after. Models the real cold start: the
    // first /candles requests race the bridge handshake (503
    // dependency_unavailable) and later ones succeed with bars.
    void setWarmup(int count, int warmStatus, QByteArray warmBody) {
        mWarmupRemaining = count;
        mWarmStatus = warmStatus;
        mWarmBody = std::move(warmBody);
    }

private:
    void onConnection() {
        QTcpSocket* sock = nextPendingConnection();
        ++mRequests;
        // Respond only after the request bytes arrive, then close gracefully.
        // Replying the instant the socket is accepted — before the client has
        // finished writing — lets Windows reset the connection and the reply is
        // lost; the real backend always reads the request first.
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            sock->readAll();
            const bool warming = mWarmupRemaining > 0;
            if (warming) --mWarmupRemaining;
            const int status = warming ? mWarmStatus : mStatus;
            const QByteArray& body = warming ? mWarmBody : mBody;
            const char* reason = status == 200 ? "OK" : "Service Unavailable";
            QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                             "\r\nContent-Type: application/json\r\nContent-Length: " +
                             QByteArray::number(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body;
            sock->write(out);
            sock->flush();
            sock->disconnectFromHost();
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }

    int mStatus;
    QByteArray mBody;
    int mRequests = 0;
    int mWarmupRemaining = 0;
    int mWarmStatus = 200;
    QByteArray mWarmBody;
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

// Drive a real event loop until `predicate` is true or `timeoutMs` elapses.
// Returns true if the predicate was satisfied. Polling on the condition (rather
// than sleeping a fixed time) keeps the test independent of platform network
// timing: a loopback round-trip is not guaranteed to complete in a fixed window.
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs) {
    QElapsedTimer clk;
    clk.start();
    while (!predicate() && clk.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(25, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

// Let the event loop run for a fixed window so a request that is going to be
// issued has had a chance to be (used only where there is nothing to poll on).
void pump(int ms) {
    QElapsedTimer clk;
    clk.start();
    while (clk.elapsed() < ms) {
        QEventLoop loop;
        QTimer::singleShot(25, &loop, &QEventLoop::quit);
        loop.exec();
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

        bool firstFailed = false;
        int receivedCount = 0;
        QStringList failCodes;
        QObject::connect(&client, &ApiClient::candlesReceived,
                         [&receivedCount](const CandlesResponse&) { ++receivedCount; });
        QObject::connect(&client, &ApiClient::candlesFailed,
                         [&firstFailed, &failCodes](const QString&, const QString& code) {
                             firstFailed = true;
                             failCodes.append(code);
                         });

        DashboardPage page;
        page.setApiClient(&client);  // fires the first fetch against no listener

        // The port has no listener: the fetch must fail (connection refused),
        // leaving the default page's chart empty — the reported cold start.
        check(waitFor([&]() { return firstFailed; }, 10000),
              "case1: the raced first fetch failed");
        check(page.chartCandleCountForTest() == 0,
              "case1: the raced first fetch left the chart empty");

        // The backend binds now; MainWindow does exactly this on online().
        AnswerServer server(/*status=*/200, kCandlesBody);
        check(server.start(port), "case1: backend binds the port");
        page.setOnline(true);

        const bool retried = waitFor([&]() { return server.requestCount() >= 1; }, 10000);
        const bool rendered = waitFor([&]() { return page.chartCandleCountForTest() == 1; }, 10000);
        std::printf("  [diag] retried=%d requests=%d received=%d chart=%d failCodes=%s\n",
                    retried ? 1 : 0, server.requestCount(), receivedCount,
                    page.chartCandleCountForTest(),
                    failCodes.join(",").toUtf8().constData());
        check(retried, "case1: the chart re-fetched once the backend came online");
        check(rendered, "case1: the retry rendered the bars");
    }

    // ── Case 2: a transient bridge-warming 503 must be retried until it
    // resolves. This is the exact cold-start sequence the real backend produces:
    // /health answers as soon as 8790 binds, but /candles proxies to the bridge,
    // which returns 503 dependency_unavailable until the handshake lands. ─────
    {
        const quint16 port = reserveFreePort();
        AnswerServer server(/*status=*/200, kCandlesBody);
        server.setWarmup(/*count=*/2, /*warmStatus=*/503, kBridgeWarmingBody);
        check(server.start(port), "case2: server listens");

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(port));

        DashboardPage page;
        page.setApiClient(&client);

        // Health keeps reporting STARTING while the bridge warms: each distinct
        // bridge state must drive a retry until bars arrive.
        HealthResponse starting;
        starting.data.aggregate = "STARTING";
        starting.data.processState = "STARTING";
        HealthResponse online;
        online.data.aggregate = "DEGRADED";
        online.data.processState = "ONLINE";

        const bool rendered = waitFor(
            [&]() {
                page.updateFromHealth(starting);
                page.updateFromHealth(online);
                return page.chartCandleCountForTest() == 1;
            },
            15000);
        check(server.requestCount() >= 3,
              "case2: the bridge-warming 503 was retried until it resolved");
        check(rendered, "case2: the chart rendered once the bridge was ready");
    }

    // ── Case 3: an honest terminal failure is not retried forever. ───────────
    {
        const quint16 port = reserveFreePort();
        AnswerServer server(/*status=*/503, kNoTerminalBody);
        check(server.start(port), "case3: server listens");

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(port));

        DashboardPage page;
        page.setApiClient(&client);
        check(waitFor([&]() { return server.requestCount() >= 1; }, 10000),
              "case3: the first fetch reached the backend");
        pump(500);  // let the terminal failure land and mark the chart settled
        const int afterFirst = server.requestCount();

        // A reachable bridge answered "no MT5 terminal": re-probing changes
        // nothing, so the card must not hammer it on every health update.
        HealthResponse resp;
        resp.data.aggregate = "DEGRADED";
        resp.data.processState = "ONLINE";
        for (int i = 0; i < 10; ++i) {
            resp.data.aggregate = QStringLiteral("DEGRADED%1").arg(i);  // force "progress"
            page.updateFromHealth(resp);
        }
        pump(1000);

        check(server.requestCount() == afterFirst,
              "case3: a terminal MT5 failure is not retried");
        check(page.chartCandleCountForTest() == 0,
              "case3: the chart stays honestly empty (never fabricated)");
    }

    if (g_failures == 0) {
        std::printf("all dashboard cold-start retry checks passed\n");
    } else {
        std::fprintf(stderr, "dashboard-retry: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
