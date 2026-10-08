#pragma once
#include <QObject>
#include <QString>
#include <QTimer>

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// Theme manager — loads and applies QSS for dark/light modes.
// Caches compiled QSS. Fade transition via 250ms timer (opacity not QSS).
// ──────────────────────────────────────────────────────────────────────────────

class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    enum class Theme { Dark, Light };
    Q_ENUM(Theme)

    void setTheme(Theme theme);
    Theme currentTheme() const { return mCurrentTheme; }
    bool isDark() const { return mCurrentTheme == Theme::Dark; }

    void applyTheme();
    void loadQssFromResource(const QString& resourcePath);
    QString qssContent() const { return mCurrentQss; }

    // For fade transition: caller toggles widget opacity
    void startFadeTransition();
    bool fadeActive() const { return mFadeTimer.isActive(); }

signals:
    void themeChanged(Theme theme);
    void qssApplied(const QString& qss);

private:
    QString defaultDarkQss() const;
    QString defaultLightQss() const;

    Theme mCurrentTheme = Theme::Dark;
    QString mCurrentQss;
    QTimer mFadeTimer;
};

}  // namespace astra
