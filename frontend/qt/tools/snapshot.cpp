// ASTRA Desktop — offscreen snapshot tool.
//
// Renders the real shell (same startup sequence as main.cpp: same QSS, same
// fonts, same ThemeManager init) and saves PNG grabs of every page. All
// pixels come from the running Qt widget tree — nothing is fabricated.
//
// Usage:
//   astra_snapshot <output-directory>
//
// Produces, under <output-directory>:
//   dashboard_dark.png      dashboard_light.png
//   chart_dark.png          history_dark.png
//   health_dark.png         settings_dark.png
//   coming_soon_dark.png    fullscreen_dark.png
//
// The bare offscreen QPA platform reports an 800x800 screen, so normal pages
// are resized to 1440x900 and grabbed with QWidget::grab() (independent of the
// screen size). Fullscreen is exercised by posting a real F11 key event to
// MainWindow. Under a real 1440x900 X server (Xvfb + xcb) the fullscreen grab
// is exactly 1440x900; under bare offscreen it degrades to 800x800 and a note
// is printed.

#include "MainWindow.h"
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImageWriter>
#include <QKeyEvent>
#include <QMetaObject>
#include <QDebug>
#include <QPixmap>
#include <QScreen>
#include <QDir>

#include <cstdio>

using astra::MainWindow;

namespace {

void emitKey(MainWindow* win, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, key, mods, QString());
    QKeyEvent release(QEvent::KeyRelease, key, mods, QString());
    QApplication::sendEvent(win, &press);
    QApplication::sendEvent(win, &release);
}

// Replicates the startup sequence of frontend/qt/src/main.cpp verbatim so the
// snapshots show exactly what the shipped app shows.
void setupApp(QApplication& app) {
    QFont font = QApplication::font();
    font.setFamily("Inter");
    font.setPointSize(14);
    QApplication::setFont(font);

    QFile qssFile(":/theme/theme-dark.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());
        app.setStyleSheet(qss);
        qssFile.close();
        qDebug() << "Loaded dark theme QSS";
    } else {
        qWarning() << "Failed to load theme QSS from resources";
    }
}

void settle(int ms) {
    QApplication::processEvents();
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
}

bool writePng(const QString& path, const QPixmap& px) {
    QImageWriter writer(path, "png");
    writer.setOptimizedWrite(true);
    if (!writer.write(px.toImage())) {
        fprintf(stderr, "FAILED to write %s: %s\n",
                qPrintable(path), qPrintable(writer.errorString()));
        return false;
    }
    return true;
}

bool capture(MainWindow& win, const char* name, const QString& outDir,
             int w = 1440, int h = 900) {
    win.resize(w, h);
    win.show();
    settle(500);

    QPixmap px = win.grab();
    const QString path = outDir + "/" + QLatin1String(name);
    if (!writePng(path, px)) return false;

    QFileInfo fi(path);
    const bool sidebarVisible = win.findChild<QWidget*>("astraSidebar") &&
                                win.findChild<QWidget*>("astraSidebar")->isVisible();
    printf("%-22s  %dx%d  %lld bytes  isFullScreen=%s  sidebarVisible=%s\n",
           name, px.width(), px.height(), (long long)fi.size(),
           win.isFullScreen() ? "true" : "false",
           sidebarVisible ? "true" : "false");
    fflush(stdout);
    return true;
}

// Drive a private MainWindow slot by meta-name (nav indices match MainWindow's
// onNavClicked: 0 Dashboard, 1 Chart, 2 History, 3 Health, 4 Research, ...).
void nav(MainWindow& win, int index) {
    QMetaObject::invokeMethod(&win, "onNavClicked", Qt::DirectConnection,
                              Q_ARG(int, index));
    QApplication::processEvents();
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    setupApp(app);

    QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString(".");
    QDir().mkpath(outDir);

    // Mock API (same loopback base URL as the real app).
    astra::ApiClient apiClient;
    apiClient.setBaseUrl("http://127.0.0.1:8790/api/v1/");

    MainWindow win;
    win.setApiClient(&apiClient);
    win.resize(1440, 900);
    win.show();

    // Let the first polls from the mock API arrive.
    settle(1500);

    bool ok = true;

    // ── dashboard, dark ────────────────────────────────────────────────────
    nav(win, 0);
    ok &= capture(win, "dashboard_dark.png", outDir);

    // ── dashboard, light (real Ctrl+T theme toggle) ───────────────────────
    emitKey(&win, Qt::Key_T, Qt::ControlModifier);
    QApplication::processEvents();
    ok &= capture(win, "dashboard_light.png", outDir);

    // Back to dark.
    emitKey(&win, Qt::Key_T, Qt::ControlModifier);
    QApplication::processEvents();

    // ── chart page ─────────────────────────────────────────────────────────
    nav(win, 1);
    ok &= capture(win, "chart_dark.png", outDir);

    // ── history ────────────────────────────────────────────────────────────
    nav(win, 2);
    ok &= capture(win, "history_dark.png", outDir);

    // ── health ─────────────────────────────────────────────────────────────
    nav(win, 3);
    ok &= capture(win, "health_dark.png", outDir);

    // ── settings (Configuration, nav index 9) ──────────────────────────────
    nav(win, 9);
    ok &= capture(win, "settings_dark.png", outDir);

    // ── coming soon (Research, nav index 4 -> shared ComingSoonPage) ───────
    nav(win, 4);
    ok &= capture(win, "coming_soon_dark.png", outDir);

    // ── fullscreen, dark (real F11 key event) ─────────────────────────────
    nav(win, 0);
    emitKey(&win, Qt::Key_F11);
    QApplication::processEvents();

    const QScreen* screen = QApplication::primaryScreen();
    if (screen && screen->geometry().width() >= 1440) {
        // Real 1440x900 X server: the fullscreen grab matches the screen.
        settle(300);
        QPixmap px = win.grab();
        const QString path = outDir + "/fullscreen_dark.png";
        ok &= writePng(path, px);
        if (ok) {
            QFileInfo fi(path);
            const bool sidebarVisible = win.findChild<QWidget*>("astraSidebar") &&
                                        win.findChild<QWidget*>("astraSidebar")->isVisible();
            printf("%-22s  %dx%d  %lld bytes  isFullScreen=%s  sidebarVisible=%s\n",
                   "fullscreen_dark.png", px.width(), px.height(),
                   (long long)fi.size(),
                   win.isFullScreen() ? "true" : "false",
                   sidebarVisible ? "true" : "false");
            fflush(stdout);
        }
    } else {
        // There is no public resize API for the offscreen screen, so this
        // platform can only produce an 800x800 fullscreen grab. It is still
        // a genuine fullscreen grab (isFullScreen true, sidebar visible), but
        // not 1440x900 — run under Xvfb+xcb for the exact spec size.
        settle(300);
        QPixmap px = win.grab();
        const QString path = outDir + "/fullscreen_dark.png";
        ok &= writePng(path, px);
        if (ok) {
            QFileInfo fi(path);
            const bool sidebarVisible = win.findChild<QWidget*>("astraSidebar") &&
                                        win.findChild<QWidget*>("astraSidebar")->isVisible();
            printf("%-22s  %dx%d  %lld bytes  isFullScreen=%s  sidebarVisible=%s   "
                   "(note: offscreen screen caps fullscreen at 800x800; "
                   "run under Xvfb+xcb for 1440x900)\n",
                   "fullscreen_dark.png", px.width(), px.height(),
                   (long long)fi.size(),
                   win.isFullScreen() ? "true" : "false",
                   sidebarVisible ? "true" : "false");
            fflush(stdout);
        }
    }
    emitKey(&win, Qt::Key_Escape);
    QApplication::processEvents();

    return ok ? 0 : 1;
}