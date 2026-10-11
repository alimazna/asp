// Offscreen regression test for the candle-fetch transfer-timeout race.
//
// The backend answers /health from memory but *proxies* /candles to the Python
// bridge, whose own read timeout is 5s. With the frontend's manager-wide
// transfer timeout also at 5s, a slow-but-successful candle read was aborted by
// the frontend a fraction of a second before the backend's honest envelope was
// sent, surfacing as "ui candles: fetch failed (network_error): Connection
// closed" — the chart then showed its error overlay while the backend was
// healthy. Candle fetches must outlast the backend's ~5.4s worst case.
//
// This test drives a local HTTP server that answers candles after a delay
// longer than the backend's bridge timeout, and asserts the reply is still
// rendered as bars. A health probe against the same slow server must fail, which
// proves the candle budget is what was raised (not the whole client).

#include "api/ApiClient.h"

#include <QApplication>
#include <QByteArray>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

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
    "\"timeframe\":\"M15\",\"symbol\":\"XAUUSD\",\"count\":1,\"bars\":["
    "{\"time\":1700000000,\"open\":1.0,\"high\":2.0,\"low\":0.5,\"close\":1.5,"
    "\"tick_volume\":10,\"spread\":1,\"real_volume\":0}]}}";

// A one-shot HTTP server: accepts a request and answers with `body` after
// `delayMs`. The response is a full HTTP/1.1 reply with Connection: close.
class SlowServer : public QTcpServer {
public:
    SlowServer(int delayMs, QByteArray body) : mDelayMs(delayMs), mBody(std::move(body)) {
        connect(this, &QTcpServer::newConnection, this, &SlowServer::onConnection);
    }
    bool start() { return listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return serverPort(); }

private:
    void onConnection() {
        QTcpSocket* sock = nextPendingConnection();
        QByteArray body = mBody;
        QTimer::singleShot(mDelayMs, sock, [sock, body]() {
            QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                             "Content-Length: " + QByteArray::number(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body;
            sock->write(out);
            sock->flush();
            sock->disconnectFromHost();
        });
    }
    int mDelayMs;
    QByteArray mBody;
};

// Run the event loop until `stop` is true or timeoutMs elapses. Returns the
// elapsed milliseconds.
int runUntil(QEventLoop& loop, bool& stop, int timeoutMs) {
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, &stop]() { if (stop) loop.quit(); });
    poll.start(50);
    QElapsedTimer clk;
    clk.start();
    loop.exec();
    return static_cast<int>(clk.elapsed());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // The delay exceeds the backend's bridge read timeout (5s), so a client
    // still on a 5s budget would abort before the reply lands.
    const int kServerDelayMs = 6500;

    // ── Candles: a slow bridge reply must still render as bars. ──────────────
    {
        SlowServer server(kServerDelayMs, kCandlesBody);
        check(server.start(), "candle test server listens");

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(server.port()));

        bool received = false;
        bool failed = false;
        QObject::connect(&client, &ApiClient::candlesReceived,
                         [&received](const CandlesResponse&) { received = true; });
        QObject::connect(&client, &ApiClient::candlesFailed,
                         [&failed](const QString&, const QString&) { failed = true; });

        client.fetchCandles(QStringLiteral("M15"), 500);
        QEventLoop loop;
        bool stop = false;
        QObject::connect(&client, &ApiClient::candlesReceived, [&stop]() { stop = true; });
        QObject::connect(&client, &ApiClient::candlesFailed, [&stop]() { stop = true; });
        const int elapsed = runUntil(loop, stop, 20000);

        check(!failed, "a slow-but-successful candle fetch is not aborted");
        check(received, "the slow candle reply is delivered as bars");
        check(elapsed > kServerDelayMs - 500,
              "the reply waited past the backend's 5s bridge timeout");
    }

    // ── Health keeps the tight budget: the same slow answer must time out. ────
    {
        SlowServer server(kServerDelayMs, QByteArray("{\"api\":\"v1\",\"schema\":\"1.0\",\"data\":{}}"));
        check(server.start(), "health test server listens");

        ApiClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(server.port()));

        bool healthOk = false;
        bool healthFailed = false;
        QObject::connect(&client, &ApiClient::healthReceived, [&healthOk](const HealthResponse&) { healthOk = true; });
        QObject::connect(&client, &ApiClient::healthProbeFailed, [&healthFailed]() { healthFailed = true; });

        client.fetchHealth();
        QEventLoop loop;
        bool stop = false;
        QObject::connect(&client, &ApiClient::healthReceived, [&stop]() { stop = true; });
        QObject::connect(&client, &ApiClient::healthProbeFailed, [&stop]() { stop = true; });
        runUntil(loop, stop, 8000);

        check(healthFailed, "health still enforces the 5s budget (override is candles-only)");
        check(!healthOk, "health did not accept the late reply");
    }

    if (g_failures == 0) {
        std::printf("all candle-timeout checks passed\n");
    } else {
        std::fprintf(stderr, "candle-timeout: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
