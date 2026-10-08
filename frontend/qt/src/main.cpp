#include "MainWindow.h"
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QStyleFactory>

// Force meta-object compilation for QObject-derived classes
// (MOC handles this automatically via qt_standard_project_setup)

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // High DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // Font — try Inter, fallback to system
    QFont font = QApplication::font();
    font.setFamily("Inter");
    font.setPointSize(14);
    QApplication::setFont(font);

    // Load QSS theme from resources
    // After qt_add_resources, files are available at :/path
    QFile qssFile(":/theme/theme-dark.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());
        qApp->setStyleSheet(qss);
        qssFile.close();
        qDebug() << "Loaded dark theme QSS";
    } else {
        qWarning() << "Failed to load theme QSS from resources, trying fallback";
        // Fallback: try to read from file system (for development)
        QFile fsFile("frontend/qt/src/resources/theme/theme-dark.qss");
        if (fsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString qss = QString::fromUtf8(fsFile.readAll());
            qApp->setStyleSheet(qss);
            fsFile.close();
            qDebug() << "Loaded dark theme QSS from filesystem";
        } else {
            qWarning() << "Failed to load theme QSS entirely";
        }
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
