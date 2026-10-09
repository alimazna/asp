#pragma once
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>
#include <QSettings>
#include <QVector>

class QPushButton;
class QCloseEvent;
class QEvent;
class QKeyEvent;

class DashboardPage;
class ChartPage;
class HistoryPage;
class HealthPage;
class SettingsPage;
class ComingSoonPage;

namespace astra {

class NavButton;

// ──────────────────────────────────────────────────────────────────────────────
// MainWindow — ASTRA shell
// Layout: sidebar (232px, grouped navigation) + main area
//         (top bar 56px + content + bottom bar 32px)
// ──────────────────────────────────────────────────────────────────────────────

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    void setApiClient(ApiClient* client);
    void showExitConfirmation();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onNavClicked(int navIndex);
    void onRefreshClicked();
    void onThemeToggled();
    void onFullscreenToggled();
    void onCloseClicked();
    void onExitClicked();
    void onAnalysisUpdated(const AnalysisResponse& resp);
    void onHealthUpdated(const HealthResponse& resp);
    void onHistoryUpdated(const QVector<AnalysisData>& items);
    void onOffline();
    void onOnline();

private:
    void setupSidebar();
    void setupTopBar();
    void setupBottomBar();
    void setupContentArea();
    void updateLivenessIndicator();
    void updateBottomBarStatus();
    void updateClock();
    void applyTheme();
    void toggleFullscreen(bool enter);
    void showFullscreenHint();
    void hideFullscreenHint();
    void saveSettings();
    void stopAllTimers();
    [[nodiscard]] int navIndexOfPage(int pageIndex) const;

    // Navigation
    enum Page { Dashboard, Chart, History, Health, Settings, ComingSoon, PageCount };
    QStackedWidget* mContentStack = nullptr;
    QVector<QWidget*> mPages;

    // Sidebar — grouped navigation (label, target page or -1 = coming soon,
    // icon resource)
    struct NavEntry {
        QString label;
        int pageIndex;
        QString iconPath;
    };
    struct NavGroup {
        QString title;
        QVector<NavEntry> entries;
    };
    QVector<NavGroup> mNavGroups;
    QVector<int> mNavEntryPage;      // nav index -> page index (-1 coming soon)
    QVector<QString> mNavEntryLabel; // nav index -> display label
    QVector<NavButton*> mNavButtons;
    NavButton* mExitButton = nullptr;
    QWidget* mSidebar = nullptr;
    QLabel* mLogoLabel = nullptr;

    // Top bar
    QWidget* mTopBar = nullptr;
    QLabel* mPageTitle = nullptr;
    QLabel* mHealthDot = nullptr;
    QLabel* mHealthLabel = nullptr;
    QLabel* mClockLabel = nullptr;
    QTimer mClockTimer;
    QPushButton* mFullscreenBtn = nullptr;
    QPushButton* mThemeBtn = nullptr;
    QPushButton* mCloseBtn = nullptr;

    // Bottom bar
    QWidget* mBottomBar = nullptr;
    QLabel* mBackendStatus = nullptr;   // ● Runtime
    QLabel* mBridgeStatus = nullptr;    // ● Streams
    QLabel* mFreshnessStatus = nullptr; // ● Persistence
    QLabel* mDisclaimer = nullptr;      // System Health (right)

    // Theme
    ThemeManager* mThemeManager = nullptr;
    bool mIsFullscreen = false;
    QTimer mFullscreenHintTimer;
    bool mFullscreenHintVisible = false;

    // Polling
    QTimer mAnalysisPollTimer;
    QTimer mHealthPollTimer;
    ApiClient* mApiClient = nullptr;

    // Settings
    QSettings mSettings;

    // Current page index
    int mCurrentPage = 0;

    // Exit confirmation state
    bool mExitConfirmed = false;
};

}  // namespace astra
