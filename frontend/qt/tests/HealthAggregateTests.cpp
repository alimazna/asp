// Offscreen regression tests for backend/bridge health mapping.
//
// The bug: the frontend mapped everything other than "ONLINE" to "OFFLINE",
// so a healthy backend in the correct DEGRADED/stale state for a closed-market
// weekend rendered "OFFLINE / backend unreachable". These tests pin the honest
// mapping at the pure-helper layer and at each UI surface (Dashboard card,
// MainWindow liveness chip + bottom bar, HealthPage rows).

#include "api/ApiTypes.h"
#include "api/ApiClient.h"
#include "pages/DashboardPage.h"
#include "pages/HealthPage.h"
#include "MainWindow.h"

#include <QApplication>
#include <QLabel>
#include <QStringList>
#include <QVector>

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
    for (QLabel* l : root->findChildren<QLabel*>()) {
        if (l->text().contains(needle)) return true;
    }
    return false;
}

QStringList allLabelTexts(QWidget* root) {
    QStringList out;
    for (QLabel* l : root->findChildren<QLabel*>()) out << l->text();
    return out;
}

HealthResponse make(const QString& aggregate,
                    const QString& processState,
                    const QString& v1status = QString(),
                    const QString& v1bridge = QString()) {
    HealthResponse r;
    r.data.aggregate = aggregate;
    r.data.processState = processState;
    r.data.status = v1status;
    r.data.bridge = v1bridge;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Pure mapping helpers ────────────────────────────────────────────────
    {
        HealthData d; d.aggregate = "ONLINE";
        check(backendHealthDisplay(d).text == "ONLINE", "aggregate ONLINE -> ONLINE");
        check(backendHealthDisplay(d).level == HealthLevel::Ok, "aggregate ONLINE -> Ok");
    }
    {
        HealthData d; d.aggregate = "DEGRADED";
        check(backendHealthDisplay(d).text == "DEGRADED", "aggregate DEGRADED -> DEGRADED");
        check(backendHealthDisplay(d).level == HealthLevel::Warn, "aggregate DEGRADED -> Warn");
        check(backendHealthDisplay(d).text != "OFFLINE", "aggregate DEGRADED is not OFFLINE");
    }
    {
        HealthData d; d.aggregate = "STARTING";
        check(backendHealthDisplay(d).text == "STARTING", "aggregate STARTING -> STARTING");
        check(backendHealthDisplay(d).level == HealthLevel::Warn, "aggregate STARTING -> Warn");
    }
    {
        HealthData d; d.aggregate = "RECOVERING";
        check(backendHealthDisplay(d).text == "RECOVERING", "aggregate RECOVERING -> RECOVERING");
    }
    {
        HealthData d; d.aggregate = "OFFLINE";
        check(backendHealthDisplay(d).text == "OFFLINE", "aggregate OFFLINE -> OFFLINE");
        check(backendHealthDisplay(d).level == HealthLevel::Error, "aggregate OFFLINE -> Error");
    }
    {
        HealthData d; d.aggregate = "ERROR";
        check(backendHealthDisplay(d).text == "ERROR", "aggregate ERROR -> ERROR");
        check(backendHealthDisplay(d).level == HealthLevel::Error, "aggregate ERROR -> Error");
    }
    {
        HealthData d; d.aggregate = "WAT";
        check(backendHealthDisplay(d).text == "UNKNOWN", "unknown aggregate -> UNKNOWN");
        check(backendHealthDisplay(d).level == HealthLevel::Unknown, "unknown aggregate -> Unknown");
    }
    {
        // /health/v1 fallback when the aggregate is absent.
        HealthData d; d.status = "degraded";
        check(backendHealthDisplay(d).text == "DEGRADED", "v1 degraded fallback -> DEGRADED");
        HealthData d2; d2.status = "ok";
        check(backendHealthDisplay(d2).text == "ONLINE", "v1 ok fallback -> ONLINE");
    }

    {
        // Bridge process_state.
        HealthData d; d.processState = "ONLINE";
        check(bridgeHealthDisplay(d).text == "OK", "process_state ONLINE -> OK");
        check(bridgeHealthDisplay(d).level == HealthLevel::Ok, "process_state ONLINE -> Ok");
        HealthData d2; d2.processState = "OFFLINE";
        check(bridgeHealthDisplay(d2).text == "OFFLINE", "process_state OFFLINE -> OFFLINE");
        check(bridgeHealthDisplay(d2).level == HealthLevel::Error, "process_state OFFLINE -> Error");
        HealthData d3; d3.processState = "DEGRADED";
        check(bridgeHealthDisplay(d3).text == "DEGRADED", "process_state DEGRADED -> DEGRADED");
        check(bridgeHealthDisplay(d3).level == HealthLevel::Warn, "process_state DEGRADED -> Warn");
        HealthData d4; d4.processState = "STARTING";
        check(bridgeHealthDisplay(d4).text == "STARTING", "process_state STARTING -> STARTING");
        HealthData d5; d5.processState = "NONSENSE";
        check(bridgeHealthDisplay(d5).text == "UNKNOWN", "unknown process_state -> UNKNOWN");
        // Fallback to /health/v1 vocab when process_state absent.
        HealthData d6; d6.bridge = "stale";
        check(bridgeHealthDisplay(d6).text == "STALE", "bridge stale fallback -> STALE");
        HealthData d7; d7.bridge = "ok";
        check(bridgeHealthDisplay(d7).text == "OK", "bridge ok fallback -> OK");
    }

    // ── DashboardPage card ──────────────────────────────────────────────────
    {
        astra::DashboardPage page;
        page.updateFromHealth(make("DEGRADED", "ONLINE", "degraded", "ok"));
        check(anyLabelContains(&page, "DEGRADED"), "dashboard shows DEGRADED for aggregate DEGRADED");
        check(!anyLabelContains(&page, "backend unreachable"),
              "dashboard does not say backend unreachable when degraded");
        check(anyLabelContains(&page, "OK"), "dashboard shows bridge OK for process_state ONLINE");
    }
    {
        astra::DashboardPage page;
        page.updateFromHealth(make("ONLINE", "ONLINE", "ok", "ok"));
        check(anyLabelContains(&page, "ONLINE"), "dashboard shows ONLINE for aggregate ONLINE");
    }
    {
        // Truly unreachable backend: no aggregate -> OFFLINE via setOnline(false).
        astra::DashboardPage page;
        page.setOnline(false);
        check(anyLabelContains(&page, "OFFLINE"), "dashboard OFFLINE when backend unreachable");
        check(anyLabelContains(&page, "backend unreachable"),
              "dashboard says backend unreachable only when unreachable");
    }

    // ── MainWindow liveness chip + bottom bar ───────────────────────────────
    {
        MainWindow win;
        ApiClient client;
        win.setApiClient(&client);
        // Feed the aggregate through the client's public response accessor by
        // emitting the client's own signal (client owns currentHealth()).
        HealthResponse r = make("DEGRADED", "ONLINE", "degraded", "ok");
        emit client.healthReceived(r);
        check(anyLabelContains(&win, "SYSTEM DEGRADED"), "chip shows SYSTEM DEGRADED");
        check(!anyLabelContains(&win, "SYSTEM OFFLINE"), "chip not SYSTEM OFFLINE when degraded");
        check(anyLabelContains(&win, "Runtime: DEGRADED"), "bottom bar Runtime DEGRADED");
        check(anyLabelContains(&win, "Streams: OK"), "bottom bar Streams OK for process_state ONLINE");
        check(anyLabelContains(&win, "System Health: DEGRADED"), "bottom bar System Health DEGRADED");
    }
    {
        MainWindow win;
        ApiClient client;
        win.setApiClient(&client);
        emit client.healthReceived(make("ONLINE", "ONLINE", "ok", "ok"));
        check(anyLabelContains(&win, "SYSTEM HEALTHY"), "chip SYSTEM HEALTHY for aggregate ONLINE");
        check(anyLabelContains(&win, "Runtime: ONLINE"), "bottom bar Runtime ONLINE");
    }
    {
        // Backend unreachable.
        MainWindow win;
        ApiClient client;
        win.setApiClient(&client);
        emit client.offline();
        check(anyLabelContains(&win, "SYSTEM OFFLINE"), "chip SYSTEM OFFLINE when unreachable");
        check(anyLabelContains(&win, "Runtime: OFFLINE"), "bottom bar Runtime OFFLINE when unreachable");
        check(anyLabelContains(&win, "Streams: OFFLINE"), "bottom bar Streams OFFLINE when unreachable");
    }

    // ── HealthPage rows ─────────────────────────────────────────────────────
    {
        astra::HealthPage page;
        page.updateFromHealth(make("DEGRADED", "ONLINE", "degraded", "ok"));
        check(anyLabelContains(&page, "DEGRADED"), "HealthPage Backend row DEGRADED");
        check(!anyLabelContains(&page, "OFFLINE"), "HealthPage not OFFLINE when degraded");
        check(anyLabelContains(&page, "OK"), "HealthPage Bridge row OK for process_state ONLINE");
    }
    {
        astra::HealthPage page;
        page.updateFromHealth(make("OFFLINE", "OFFLINE", "offline", "offline"));
        check(anyLabelContains(&page, "OFFLINE"), "HealthPage OFFLINE when aggregate OFFLINE");
    }
    {
        // Absent/unknown: never a fabricated green.
        astra::HealthPage page;
        page.updateFromHealth(make("", "", "", ""));
        const QString em = QString::fromUtf8("\u2014");
        check(!anyLabelContains(&page, "ONLINE"), "HealthPage unknown is not ONLINE");
        check(!anyLabelContains(&page, "OK"), "HealthPage unknown bridge is not OK");
        check(anyLabelContains(&page, em), "HealthPage unknown renders em dash");
    }

    if (g_failures == 0) {
        std::printf("Health mapping tests: all passed\n");
    } else {
        std::fprintf(stderr, "Health mapping tests: %d failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
