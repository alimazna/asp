#include "MainWindow.h"
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"
#include <QApplication>
#include <QSettings>
#include <QIcon>
#include <QDebug>
#include <QStyleFactory>

// Force meta-object compilation for QObject-derived classes
// (MOC handles this automatically via qt_standard_project_setup)

int main(int argc, char* argv[]) {
    // High-DPI rounding policy must be set BEFORE the QApplication exists.
    // (Qt::AA_EnableHighDpiScaling is a no-op on Qt 6 — high-DPI is always on.)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Window icon (also the taskbar/alt-tab icon). The .exe icon itself is set
    // from resources/app.rc on Windows; this covers the running window. The
    // symbol lives at the qrc root, not under /icons.
    app.setWindowIcon(QIcon(":/astra-symbol.svg"));

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

    // Create API client
    astra::ApiClient apiClient;

    // Create main window
    astra::MainWindow mainWindow;
    mainWindow.setApiClient(&apiClient);

    // Show
    mainWindow.show();

    return app.exec();
}
