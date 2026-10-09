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
#include <QProcess>
#include <windows.h>

namespace {

// Start the backend host next to the frontend so a double-clicked ASTRA.exe
// brings up the whole chain (frontend -> backend :8790 -> bridge :8791).
// The backend supervises the frozen bridge itself. If a backend is already
// listening (another instance, or a dev run) the bind fails harmlessly and the
// existing one serves. Set AURA_NO_BACKEND=1 to suppress (dev/snapshot).
bool backendAlreadyRunning() {
    HANDLE mutex = OpenMutexA(SYNCHRONIZE, FALSE, "Global\\AURA_BACKEND_HOST_SINGLETON");
    if (mutex != nullptr) {
        CloseHandle(mutex);
        return true;
    }
    return false;
}

void startBackendHost() {
    if (qEnvironmentVariableIsSet("AURA_NO_BACKEND"))
        return;
    if (backendAlreadyRunning())
        return;
    const QString exe = QDir(QCoreApplication::applicationDirPath())
                            .filePath("aura_backend_host.exe");
    if (!QFileInfo::exists(exe)) {
        qWarning() << "backend host not found next to frontend:" << exe;
        return;
    }
    // Detached: the backend outlives no frontend state and owns its own
    // supervised bridge child.
    if (!QProcess::startDetached(exe, {"--api-port", "8790"}))
        qWarning() << "failed to start backend host:" << exe;
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
