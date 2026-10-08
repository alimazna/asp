#pragma once
#include "api/ApiClient.h"
#include "theme/ThemeManager.h"
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QSettings>

class DashboardPage;
class ChartPage;
class HistoryPage;
class HealthPage;
class SettingsPage;
class ConfirmExitDialog;

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// MainWindow — ASTRA shell
// Layout: sidebar (200px) + main area (top bar 52px + content + bottom bar 36px)
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

private slots:
    void onNavClicked(int pageIndex);
    void onRefreshClicked();
    void onThemeToggled();
    void onFullscreenToggled();
    void onCloseClicked();
    void onExitClicked();
    void onExitConfirmed(bool confirmed);
    void onAnalysisUpdated(const AnalysisResponse& resp);
    void onHealthUpdated(const HealthResponse& resp);
    void onOffline();
    void onOnline();

private:
    void setupSidebar();
    void setupTopBar();
    void setupBottomBar();
    void setupContentArea();
    void updateLivenessIndicator();
    void updateBottomBarStatus();
    void applyTheme();
    void toggleFullscreen(bool enter);
    void showFullscreenHint();
    void hideFullscreenHint();

    // Navigation
    enum Page { Dashboard, Chart, History, Health, Settings, PageCount };
    QStackedWidget* mContentStack = nullptr;
    QVector<QWidget*> mPages;

    // Sidebar
    QWidget* mSidebar = nullptr;
    QLabel* mLogoLabel = nullptr;
    QVector<QPushButton*> mNavButtons;
    QPushButton* mExitButton = nullptr;

    // Top bar
    QWidget* mTopBar = nullptr;
    QLabel* mPageTitle = nullptr;
    QLabel* mLiveLabel = nullptr;
    QLabel* mLiveDot = nullptr;
    QPushButton* mRefreshBtn = nullptr;
    QPushButton* mFullscreenBtn = nullptr;
    QPushButton* mThemeBtn = nullptr;
    QPushButton* mCloseBtn = nullptr;

    // Bottom bar
    QWidget* mBottomBar = nullptr;
    QLabel* mBackendStatus = nullptr;
    QLabel* mBridgeStatus = nullptr;
    QLabel* mFreshnessStatus = nullptr;
    QLabel* mDisclaimer = nullptr;

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
