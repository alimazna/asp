#include "MainWindow.h"
#include "api/ApiClient.h"
#include "autostart/AutoStart.h"
#include "platform/windows/SingleInstance.h"
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
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTextStream>

namespace {

constexpr quint16 kBackendPort = 8790;

// Held for the process lifetime. The cross-process auto-start lock is released
// when this dies (an exclusive HANDLE), so a crashed frontend never blocks a
// relaunch. Declared before main().
aura::InstanceLock g_autostartLock;

// Outcome of the auto-start attempt, surfaced to the UI. An installed ASTRA
// lives in C:\Program Files\ASTRA, which a normally-launched (non-elevated)
// process cannot write to; a bare "OFFLINE" there is undiagnosable. main()
// passes this to MainWindow so a failed spawn shows the real path + reason.
QString g_backendStartFailure;

// Wall-clock time the backend was actually spawned (QDateTime::currentMSecsSinceEpoch).
// Passed to MainWindow so the bake-in window and the "reachable after Ns" log
// measure from the spawn rather than from whenever the health poll first runs.
qint64 g_backendSpawnEpochMs = 0;

// True when this window adopted a backend it did not spawn (one already
// listening, or another ASTRA instance was starting it). The "reachable after
// Ns" number must then be measured from when this window began probing, not
// from a spawn instant that never existed — otherwise two windows logging to
// the same file produced contradictory "after 2s"/"after 49s" lines.
bool g_backendPreexisting = false;

// User-writable log directory. MUST NOT be under the install dir: the installer
// places ASTRA in Program Files, so a non-elevated launch cannot create or write
// <appDir>\logs. QStandardPaths::GenericDataLocation resolves to
// %LOCALAPPDATA% on Windows, so appending ASTRA/logs yields exactly
// %LOCALAPPDATA%\ASTRA\logs, which every interactive user can write. This
// mirrors AppPaths.logDir (= userRootDir/logs) on the backend side.
QString autostartLogDir() {
    QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + "/.astra";
    return QDir(base).filePath("ASTRA/logs");
}

// Append a line to <userData>/logs/frontend-autostart.log. The desktop app is
// built WIN32 (no console), so qWarning() output goes nowhere a user can see;
// a file is the only place a failed auto-start can be diagnosed from.
void appendAutostartLog(const QString& line) {
    const QString dir = autostartLogDir();
    if (!QDir().mkpath(dir))
        return;
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

// Cross-process auto-start lock, in the user-writable state dir. An exclusive
// HANDLE is held for the process lifetime, so a second ASTRA launched while the
// first is still baking in (port not yet answering) cannot spawn its own
// backend. The previous guard was a single bool in one process: it could not see
// a second frontend process, which is why the log showed two spawns 33s apart.
QString autostartLockPath() {
    return QDir(autostartLogDir()).filePath("frontend-autostart.lock");
}

// The auto-start must run at most once per process lifetime.
bool g_spawnAttempted = false;
// True when a backend is expected to come up (we spawned it, found one already
// listening, or another frontend is starting it). Gates the bake-in retry poll.
bool g_backendExpected = false;

void startBackendHost() {
    astra::AutoStartDeps deps;
    deps.backendListening = backendListening;
    deps.tryLock = [](const QString& path) {
        std::string err;
        // Already held by this process (e.g. a re-entrant call): treat as locked.
        return g_autostartLock.acquire(path.toStdString(), err);
    };
    deps.exeExists = [](const QString& p) { return QFileInfo::exists(p); };
    deps.log = appendAutostartLog;
    deps.backendExePath = []() {
        return QDir(QCoreApplication::applicationDirPath())
            .filePath("aura_backend_host.exe");
    };
    deps.lockPath = autostartLockPath;
    deps.spawn = [](const QString& exe, int port) {
        // Record the spawn instant so the bake-in window and the "reachable
        // after Ns" log measure from here even if a later startup step delays
        // the health poll.
        g_backendSpawnEpochMs = QDateTime::currentMSecsSinceEpoch();
        // Detached: the backend owns its own supervised bridge child and must
        // outlive the frontend window. Do not block the GUI thread waiting for
        // the port: the backend binds it only after the bridge handshake.
        if (!QProcess::startDetached(exe, {"--api-port", QString::number(port)})) {
            g_backendSpawnEpochMs = 0;
            return false;
        }
        return true;
    };

    const astra::AutoStartOutcome outcome = astra::attemptAutoStart(
        qEnvironmentVariableIsSet("AURA_NO_BACKEND"), kBackendPort,
        g_spawnAttempted, deps);

    switch (outcome) {
        case astra::AutoStartOutcome::Spawned:
            g_backendExpected = true;
            break;
        case astra::AutoStartOutcome::AlreadyListening:
        // Another ASTRA frontend is starting the backend; this window still needs
        // to poll it in, so it is "expected" too.
        case astra::AutoStartOutcome::LockedByAnotherInstance:
            // We did not spawn this backend, so there is no spawn instant to
            // measure from: mark it pre-existing so the reachability log uses
            // this window's own probe start.
            g_backendPreexisting = true;
            g_backendExpected = true;
            break;
        case astra::AutoStartOutcome::ExeMissing:
            g_backendStartFailure = QStringLiteral("aura_backend_host.exe not found at ")
                                    + deps.backendExePath();
            break;
        case astra::AutoStartOutcome::SpawnFailed:
            g_backendStartFailure = QStringLiteral("QProcess::startDetached failed for ")
                                    + deps.backendExePath();
            break;
        case astra::AutoStartOutcome::AlreadyAttempted:
        case astra::AutoStartOutcome::Suppressed:
            break;
    }
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

    // Application identity. The auto-start log uses QStandardPaths::
    // GenericDataLocation (= %LOCALAPPDATA% on Windows) plus an explicit "ASTRA"
    // component, so the log lands in %LOCALAPPDATA%\ASTRA\logs even when ASTRA is
    // installed to Program Files (which a non-elevated process cannot write).
    QCoreApplication::setOrganizationName("ASTRA");
    QCoreApplication::setApplicationName("ASTRA");

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
#if defined(Q_OS_WIN)
    if (!g_backendStartFailure.isEmpty())
        mainWindow.setBackendStartFailure(g_backendStartFailure);
    // We spawned a host (or found one already listening): the backend binds 8790
    // only after the bridge handshake, so poll it in for the next 20s before
    // declaring failure. Left false when the executable was missing, where the
    // banner is already showing the reason. MUST be set before setApiClient(),
    // which starts the fast retry poll when this is true.
    if (g_backendExpected && g_backendStartFailure.isEmpty()) {
        mainWindow.setBackendAutostartSpawnedAt(g_backendSpawnEpochMs);
        mainWindow.setBackendPreexisting(g_backendPreexisting);
        mainWindow.setBackendAutostartInProgress(true);
    }
#endif
    mainWindow.setApiClient(&apiClient);

    // Show
    mainWindow.show();

    return app.exec();
}
