#include "MainWindow.h"
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"
#include <QApplication>
#include <QSettings>
#include <QIcon>
#include <QDebug>
#include <QStyleFactory>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QTcpSocket>
#include <QTextStream>

namespace {

constexpr quint16 kBackendPort = 8790;

// Append a line to <appDir>/logs/frontend-autostart.log. The desktop app is
// built WIN32 (no console), so qWarning() output goes nowhere a user can see;
// a file is the only place a failed auto-start can be diagnosed from.
void appendAutostartLog(const QString& line) {
    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath("logs");
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath("frontend-autostart.log"));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << "[" << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "] "
        << line << "\n";
}

// True when something already answers on the backend's loopback port. This is
// the real "already running" signal: the previous code opened a mutex that no
// component ever creates, so the guard never fired and a second backend was
// always spawned (its bind then fails and the frontend is left with an API
// server it cannot reach).
bool backendListening() {
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, kBackendPort);
    return probe.waitForConnected(300);
}

void startBackendHost() {
    if (qEnvironmentVariableIsSet("AURA_NO_BACKEND")) {
        appendAutostartLog("auto-start suppressed by AURA_NO_BACKEND");
        return;
    }
    if (backendListening()) {
        appendAutostartLog("backend already listening on 127.0.0.1:8790; not spawning another");
        return;
    }
    const QString exe = QDir(QCoreApplication::applicationDirPath())
                            .filePath("aura_backend_host.exe");
    if (!QFileInfo::exists(exe)) {
        appendAutostartLog("FAILED: backend host not found next to frontend: " + exe);
        return;
    }
    // Detached: the backend owns its own supervised bridge child and must
    // outlive the frontend window.
    if (!QProcess::startDetached(exe, {"--api-port", QString::number(kBackendPort)})) {
        appendAutostartLog("FAILED: QProcess::startDetached returned false for " + exe);
        return;
    }
    // Do not block the GUI thread waiting for the port: the backend starts its
    // loopback API only after the bridge handshake (up to 15s when the bridge is
    // unavailable), so a short wait would log a false failure. The spawn result
    // is recorded here; the health poll surfaces a backend that dies afterwards.
    appendAutostartLog("spawned backend: " + exe);
}

}  // namespace
#endif

// Force meta-object compilation for QObject-derived classes
// (MOC handles this automatically via qt_standard_project_setup)

int main(int argc, char* argv[]) {
    // High-DPI rounding policy must be set BEFORE the QApplication exists.
    // (Qt::AA_EnableHighDpiScaling is a no-op on Qt 6 — high-DPI is always on.)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Window icon (also the taskbar/alt-tab icon). The .exe icon itself is set
    // from resources/app.rc on Windows; this covers the running window. Prefer
    // the multi-size .ico for crisp taskbar/Alt-Tab rendering, falling back to
    // the SVG symbol (which lives at the qrc root, not under /icons).
    QIcon icon(":/icons/astra.ico");
    if (icon.isNull())
        icon = QIcon(":/astra-symbol.svg");
    app.setWindowIcon(icon);

    // Font — try Inter, fallback to system
    QFont font = QApplication::font();
    font.setFamily("Inter");
    font.setPointSize(14);
    QApplication::setFont(font);

    // Apply the persisted theme (dark by default) BEFORE any widget exists, so
    // the application palette AND stylesheet are correct for the first paint.
    // ThemeManager::setTheme also sets the per-theme QPalette that the
    // custom-painted widgets read via palette(); a bare setStyleSheet() here
    // would leave the palette on the default light palette until the user
    // toggled the theme.
    {
        QSettings settings("ASTRA", "Desktop");
        const bool darkTheme = settings.value("themeDark", true).toBool();
        astra::ThemeManager themeManager;
        themeManager.setTheme(darkTheme ? astra::ThemeManager::Theme::Dark
                                        : astra::ThemeManager::Theme::Light);
        qApp->setProperty("astraDark", darkTheme);
    }

#if defined(Q_OS_WIN)
    startBackendHost();
#endif

    // Create API client
    astra::ApiClient apiClient;

    // Create main window
    astra::MainWindow mainWindow;
    mainWindow.setApiClient(&apiClient);

    // Show
    mainWindow.show();

    return app.exec();
}
