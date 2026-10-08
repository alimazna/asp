#include "ThemeManager.h"
#include <QFile>
#include <QApplication>
#include <QStyle>
#include <QTimer>

namespace astra {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    mFadeTimer.setSingleShot(true);
    mFadeTimer.setInterval(250);  // 250ms fade per spec
}

void ThemeManager::setTheme(Theme theme) {
    if (mCurrentTheme == theme) return;
    mCurrentTheme = theme;
    emit themeChanged(theme);
    applyTheme();
}

void ThemeManager::applyTheme() {
    QString resourceName = isDark() ? ":/theme/theme-dark.qss" : ":/theme/theme-light.qss";
    loadQssFromResource(resourceName);
    emit qssApplied(mCurrentQss);
}

void ThemeManager::loadQssFromResource(const QString& resourcePath) {
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Fallback: try to read as plain file path
        QFile f(resourcePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mCurrentQss = QString::fromUtf8(f.readAll());
        } else {
            mCurrentQss = isDark() ? defaultDarkQss() : defaultLightQss();
        }
    } else {
        mCurrentQss = QString::fromUtf8(file.readAll());
        file.close();
    }
    qApp->setStyleSheet(mCurrentQss);
}

QString ThemeManager::defaultDarkQss() const {
    return R"(
        /* ASTRA Dark Theme — fallback */
        QMainWindow { background-color: #0A1628; color: #E8EEF5; }
        QWidget { background-color: #0A1628; color: #E8EEF5; }
        QPushButton { background: transparent; border: 1px solid #162A44; color: #8FA3BF; border-radius: 8px; padding: 8px 16px; }
        QPushButton:hover { background: #162A44; border-color: #4A90D9; color: #E8EEF5; }
        QPushButton:pressed { background: #4A90D9; color: white; }
        QPushButton:disabled { color: #5A6B80; }
        QComboBox { background: #0F1F35; border: 1px solid #162A44; color: #E8EEF5; border-radius: 8px; padding: 6px 10px; }
        QComboBox:hover { border-color: #4A90D9; }
        QSpinBox { background: #0F1F35; border: 1px solid #162A44; color: #E8EEF5; border-radius: 8px; padding: 4px; }
        QCheckBox { color: #E8EEF5; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #162A44; border-radius: 3px; background: #0F1F35; }
        QCheckBox::indicator:checked { background: #4A90D9; border-color: #4A90D9; }
        QTabWidget::pane { background: #0F1F35; border: 1px solid #162A44; border-radius: 12px; }
        QTabBar::tab { background: transparent; color: #8FA3BF; padding: 10px 16px; border-radius: 8px; }
        QTabBar::tab:selected { background: #162A44; color: #E8EEF5; }
        QTabBar::tab:hover { background: #162A44; }
    )";
}

QString ThemeManager::defaultLightQss() const {
    return R"(
        /* ASTRA Light Theme — fallback */
        QMainWindow { background-color: #F5F7FA; color: #0A1628; }
        QWidget { background-color: #F5F7FA; color: #0A1628; }
        QPushButton { background: transparent; border: 1px solid #E1E6ED; color: #5A6B80; border-radius: 8px; padding: 8px 16px; }
        QPushButton:hover { background: #EDF1F6; border-color: #2E6BB8; color: #0A1628; }
        QPushButton:pressed { background: #2E6BB8; color: white; }
        QPushButton:disabled { color: #8FA3BF; }
        QComboBox { background: #FFFFFF; border: 1px solid #E1E6ED; color: #0A1628; border-radius: 8px; padding: 6px 10px; }
        QComboBox:hover { border-color: #2E6BB8; }
        QSpinBox { background: #FFFFFF; border: 1px solid #E1E6ED; color: #0A1628; border-radius: 8px; padding: 4px; }
        QCheckBox { color: #0A1628; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #E1E6ED; border-radius: 3px; background: #FFFFFF; }
        QCheckBox::indicator:checked { background: #2E6BB8; border-color: #2E6BB8; }
        QTabWidget::pane { background: #FFFFFF; border: 1px solid #E1E6ED; border-radius: 12px; }
        QTabBar::tab { background: transparent; color: #5A6B80; padding: 10px 16px; border-radius: 8px; }
        QTabBar::tab:selected { background: #EDF1F6; color: #0A1628; }
        QTabBar::tab:hover { background: #EDF1F6; }
    )";
}

void ThemeManager::startFadeTransition() {
    if (!mFadeTimer.isActive()) {
        mFadeTimer.start();
    }
}

}  // namespace astra
