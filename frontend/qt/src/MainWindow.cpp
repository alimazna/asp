#include "MainWindow.h"
#include "pages/DashboardPage.h"
#include "pages/ChartPage.h"
#include "pages/HistoryPage.h"
#include "pages/HealthPage.h"
#include "pages/SettingsPage.h"
#include "dialogs/ConfirmExitDialog.h"
#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMessageBox>
#include <QFile>
#include <QTimer>
#include <QToolTip>

namespace astra {

static constexpr int SIDEBAR_WIDTH = 200;
static constexpr int TOP_BAR_HEIGHT = 52;
static constexpr int BOTTOM_BAR_HEIGHT = 36;
static constexpr int MIN_WIDTH = 1366;
static constexpr int MIN_HEIGHT = 768;
static constexpr int DEFAULT_WIDTH = 1440;
static constexpr int DEFAULT_HEIGHT = 900;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mSettings("ASTRA", "Desktop")
    , mApiClient(nullptr)
{
    setWindowTitle("ASTRA Desktop");
    setMinimumSize(MIN_WIDTH, MIN_HEIGHT);
    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);

    // Restore geometry
    restoreGeometry(mSettings.value("windowGeometry").toByteArray());
    restoreState(mSettings.value("windowState").toByteArray());

    // Theme
    mThemeManager = new ThemeManager(this);
    bool darkTheme = mSettings.value("themeDark", true).toBool();
    mThemeManager->setTheme(darkTheme ? ThemeManager::Theme::Dark : ThemeManager::Theme::Light);

    // Layout
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    setCentralWidget(central);

    setupSidebar();
    setupTopBar();
    setupBottomBar();
    setupContentArea();

    // Fullscreen hint
    connect(&mFullscreenHintTimer, &QTimer::timeout, this, &MainWindow::hideFullscreenHint);

    // Polling timers
    mAnalysisPollTimer.setInterval(5000);  // 5s per spec
    mHealthPollTimer.setInterval(10000);    // 10s per spec

    connect(&mAnalysisPollTimer, &QTimer::timeout, this, [this]() {
        if (mApiClient) mApiClient->fetchAnalysisLatest();
    });
    connect(&mHealthPollTimer, &QTimer::timeout, this, [this]() {
        if (mApiClient) mApiClient->fetchHealth();
    });

    // Keyboard shortcuts
    // (handled in keyPressEvent)

    // Start health polling immediately
    mHealthPollTimer.start();
}

void MainWindow::setApiClient(ApiClient* client) {
    mApiClient = client;
    connect(client, &ApiClient::analysisReceived, this, &MainWindow::onAnalysisUpdated);
    connect(client, &ApiClient::healthReceived, this, &MainWindow::onHealthUpdated);
    connect(client, &ApiClient::offline, this, &MainWindow::onOffline);
    connect(client, &ApiClient::online, this, &MainWindow::onOnline);

    // Initial fetches
    client->fetchAnalysisLatest();
    client->fetchHealth();
    mAnalysisPollTimer.start();
}

void MainWindow::setupSidebar() {
    mSidebar = new QWidget(this);
    mSidebar->setFixedWidth(SIDEBAR_WIDTH);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(mSidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // Logo area — 64px height
    QWidget* logoArea = new QWidget(mSidebar);
    logoArea->setFixedHeight(64);
    QHBoxLayout* logoLayout = new QHBoxLayout(logoArea);
    logoLayout->setContentsMargins(16, 0, 16, 0);
    logoLayout->setSpacing(0);

    // ASTRA lockup — load SVG, height 28px
    mLogoLabel = new QLabel(logoArea);
    mLogoLabel->setFixedHeight(28);
    mLogoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mLogoLabel->setStyleSheet("QLabel { color: transparent; }");
    // Load lockup SVG
    QFile lockupFile(":/astra-lockup.svg");
    if (lockupFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString svg = QString::fromUtf8(lockupFile.readAll());
        // Simple SVG rendering via QSvgWidget would require QtSvg module.
        // For now, use text label "ASTRA" with styling as fallback.
        // The spec says to use SVGs; QtSvg is optional. We'll use a QLabel with
        // styled text as the initial implementation.
        mLogoLabel->setText("ASTRA");
        mLogoLabel->setStyleSheet(
            "QLabel { "
            "color: #E8EEF5; "
            "font-family: 'Inter', 'Segoe UI', system-ui, sans-serif; "
            "font-size: 16px; "
            "font-weight: 600; "
            "letter-spacing: 3px; "
            "}"
        );
    }

    logoLayout->addWidget(mLogoLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // Vertical divider below logo — 1px, full width
    QFrame* divider = new QFrame(mSidebar);
    divider->setFixedHeight(1);
    divider->setStyleSheet("QFrame { background: #162A44; }");
    divider->setFixedWidth(SIDEBAR_WIDTH);

    // Navigation
    QWidget* navArea = new QWidget(mSidebar);
    QVBoxLayout* navLayout = new QVBoxLayout(navArea);
    navLayout->setContentsMargins(0, 8, 0, 0);
    navLayout->setSpacing(4);  // 4px between sidebar items

    const char* navLabels[] = { "Dashboard", "Chart", "History", "Health", "Settings" };
    for (int i = 0; i < 5; ++i) {
        QPushButton* btn = new QPushButton(navArea);
        btn->setText(navLabels[i]);
        btn->setFixedHeight(40);
        btn->setStyleSheet(
            "QPushButton { "
            "background: transparent; "
            "border: none; "
            "border-left: 3px solid transparent; "
            "color: #8FA3BF; "
            "padding: 10px 16px; "
            "border-radius: 0; "
            "font-size: 14px; "
            "font-weight: 500; "
            "}"
            "QPushButton:hover { "
            "background: #162A44; "
            "color: #E8EEF5; "
            "}"
            "QPushButton:pressed { "
            "background: #162A44; "
            "color: #E8EEF5; "
            "}"
        );
        btn->setProperty("pageIndex", i);
        navLayout->addWidget(btn);
        mNavButtons.append(btn);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onNavClicked(i);
        });
    }

    // Spacer to push exit to bottom
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding);
    navLayout->addSpacerItem(spacer);

    // Exit button — pinned to bottom
    mExitButton = new QPushButton(navArea);
    mExitButton->setText("Exit");
    mExitButton->setFixedHeight(40);
    mExitButton->setStyleSheet(
        "QPushButton { "
        "background: transparent; "
        "border: none; "
        "color: #8FA3BF; "
        "padding: 10px 16px; "
        "font-size: 14px; "
        "font-weight: 500; "
        "}"
        "QPushButton:hover { "
        "background: #162A44; "
        "color: #E8EEF5; "
        "}"
    );
    connect(mExitButton, &QPushButton::clicked, this, &MainWindow::onExitClicked);
    navLayout->addWidget(mExitButton);

    sidebarLayout->addWidget(logoArea);
    sidebarLayout->addWidget(divider);
    sidebarLayout->addWidget(navArea);

    // Hack: fix the divider width to match sidebar
    // (done after layout)
}

void MainWindow::setupTopBar() {
    mTopBar = new QWidget(this);
    mTopBar->setFixedHeight(TOP_BAR_HEIGHT);
    QHBoxLayout* topLayout = new QHBoxLayout(mTopBar);
    topLayout->setContentsMargins(16, 0, 16, 0);
    topLayout->setSpacing(12);

    // Left: page title
    mPageTitle = new QLabel(mTopBar);
    mPageTitle->setText("Dashboard");
    mPageTitle->setStyleSheet(
        "QLabel { "
        "color: #E8EEF5; "
        "font-size: 16px; "
        "font-weight: 500; "
        "}"
    );

    topLayout->addWidget(mPageTitle);

    // Spacer
    QSpacerItem* titleSpacer = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    topLayout->addSpacerItem(titleSpacer);

    // Right: 32x32 buttons + live indicator
    // LIVE indicator: pulsing green dot + "LIVE" text
    QHBoxLayout* liveLayout = new QHBoxLayout();
    liveLayout->setSpacing(6);

    mLiveDot = new QLabel(mTopBar);
    mLiveDot->setFixedSize(8, 8);
    mLiveDot->setStyleSheet(
        "QLabel { "
        "background: #4CAF7A; "
        "border-radius: 4px; "
        "}"
    );

    mLiveLabel = new QLabel(mTopBar);
    mLiveLabel->setText("LIVE");
    mLiveLabel->setStyleSheet(
        "QLabel { "
        "color: #4CAF7A; "
        "font-size: 12px; "
        "font-weight: 500; "
        "}"
    );

    liveLayout->addWidget(mLiveDot, 0, Qt::AlignTop);
    liveLayout->addWidget(mLiveLabel);
    topLayout->addLayout(liveLayout);

    QSpacerItem* rightSpacer = new QSpacerItem(8, 8, QSizePolicy::Expanding, QSizePolicy::Minimum);
    topLayout->addSpacerItem(rightSpacer);

    // Refresh button
    mRefreshBtn = new QPushButton(mTopBar);
    mRefreshBtn->setFixedSize(32, 32);
    mRefreshBtn->setStyleSheet(
        "QPushButton { "
        "background: transparent; "
        "border: none; "
        "padding: 4px; "
        "}"
        "QPushButton:hover { "
        "background: #162A44; "
        "}"
    );
    // Would load refresh.svg icon here
    mRefreshBtn->setText("↻");
    mRefreshBtn->setShortcut(Qt::CTRL | Qt::Key_R);
    connect(mRefreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);

    // Fullscreen button
    mFullscreenBtn = new QPushButton(mTopBar);
    mFullscreenBtn->setFixedSize(32, 32);
    mFullscreenBtn->setStyleSheet(
        "QPushButton { "
        "background: transparent; "
        "border: none; "
        "padding: 4px; "
        "}"
        "QPushButton:hover { "
        "background: #162A44; "
        "}"
    );
    mFullscreenBtn->setText("\u26F6");  // fullscreen icon
    connect(mFullscreenBtn, &QPushButton::clicked, this, &MainWindow::onFullscreenToggled);

    // Theme toggle
    mThemeBtn = new QPushButton(mTopBar);
    mThemeBtn->setFixedSize(32, 32);
    mThemeBtn->setStyleSheet(
        "QPushButton { "
        "background: transparent; "
        "border: none; "
        "padding: 4px; "
        "}"
        "QPushButton:hover { "
        "background: #162A44; "
        "}"
    );
    mThemeBtn->setText("\u263D");  // moon icon initially
    connect(mThemeBtn, &QPushButton::clicked, this, &MainWindow::onThemeToggled);

    // Close button
    mCloseBtn = new QPushButton(mTopBar);
    mCloseBtn->setFixedSize(32, 32);
    mCloseBtn->setStyleSheet(
        "QPushButton { "
        "background: transparent; "
        "border: none; "
        "padding: 4px; "
        "}"
        "QPushButton:hover { "
        "background: #D95A5A; "
        "color: white; "
        "}"
    );
    mCloseBtn->setText("X");
    connect(mCloseBtn, &QPushButton::clicked, this, &MainWindow::onCloseClicked);

    topLayout->addWidget(mRefreshBtn, 0, Qt::AlignRight);
    topLayout->addWidget(mFullscreenBtn, 0, Qt::AlignRight);
    topLayout->addWidget(mThemeBtn, 0, Qt::AlignRight);
    topLayout->addWidget(mCloseBtn, 0, Qt::AlignRight);

    // Bottom border line
    QFrame* bottomLine = new QFrame(mTopBar);
    bottomLine->setFixedHeight(1);
    bottomLine->setStyleSheet("QFrame { background: #162A44; }");
    bottomLine->setFixedWidth(this->width() - SIDEBAR_WIDTH - 32);

    QVBoxLayout* topBarInner = new QVBoxLayout(mTopBar);
    topBarInner->setContentsMargins(0, 0, 0, 0);
    topBarInner->setSpacing(0);
    topBarInner->addLayout(topLayout);
    topBarInner->addWidget(bottomLine);
}

void MainWindow::setupBottomBar() {
    mBottomBar = new QWidget(this);
    mBottomBar->setFixedHeight(BOTTOM_BAR_HEIGHT);
    QHBoxLayout* bottomLayout = new QHBoxLayout(mBottomBar);
    bottomLayout->setContentsMargins(16, 0, 16, 0);
    bottomLayout->setSpacing(24);

    // Backend status
    mBackendStatus = new QLabel(mBottomBar);
    mBackendStatus->setText("\u25CF Backend ONLINE");
    mBackendStatus->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "}"
        "QLabel { "
        "color: #4CAF7A; "
        "}"
    );

    // Bridge status
    mBridgeStatus = new QLabel(mBottomBar);
    mBridgeStatus->setText("\u25CF Bridge OK");
    mBridgeStatus->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "}"
    );

    // Freshness
    mFreshnessStatus = new QLabel(mBottomBar);
    mFreshnessStatus->setText("\u25CF Fresh: \u2014");
    mFreshnessStatus->setStyleSheet(
        "QLabel { "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "}"
    );

    bottomLayout->addWidget(mBackendStatus);
    bottomLayout->addWidget(mBridgeStatus);
    bottomLayout->addWidget(mFreshnessStatus);

    // Spacer
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    bottomLayout->addSpacerItem(spacer);

    // Disclaimer — right-aligned
    mDisclaimer = new QLabel(mBottomBar);
    mDisclaimer->setText("Not financial advice");
    mDisclaimer->setStyleSheet(
        "QLabel { "
        "color: #5A6B80; "
        "font-size: 12px; "
        "}"
    );
    bottomLayout->addWidget(mDisclaimer);

    // Divider above bottom bar
    QFrame* topLine = new QFrame(mBottomBar);
    topLine->setFixedHeight(1);
    topLine->setStyleSheet("QFrame { background: #162A44; }");
    QVBoxLayout* bbInner = new QVBoxLayout(mBottomBar);
    bbInner->setContentsMargins(0, 0, 0, 0);
    bbInner->setSpacing(0);
    bbInner->addWidget(topLine);
    bbInner->addLayout(bottomLayout);
}

void MainWindow::setupContentArea() {
    mContentStack = new QStackedWidget(this);

    // Create pages
    mPages.resize(5);
    mPages[0] = new DashboardPage(this);
    mPages[1] = new ChartPage(this);       // stub — no /candles endpoint
    mPages[2] = new HistoryPage(this);
    mPages[3] = new HealthPage(this);
    mPages[4] = new SettingsPage(this);

    for (int i = 0; i < 5; ++i) {
        mContentStack->addWidget(mPages[i]);
    }

    // Set initial active nav button
    if (!mNavButtons.isEmpty()) {
        mNavButtons[0]->setStyleSheet(
            "QPushButton { "
            "background: #162A44; "
            "border-left: 3px solid #4A90D9; "
            "color: #E8EEF5; "
            "padding: 10px 16px; "
            "font-size: 14px; "
            "font-weight: 500; "
            "}"
            "QPushButton:hover { "
            "background: #162A44; "
            "color: #E8EEF5; "
            "}"
        );
    }

    // Layout: sidebar on left, top bar, content, bottom bar
    QVBoxLayout* centralLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(mSidebar);
    QVBoxLayout* rightColumn = new QVBoxLayout();
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);
    rightColumn->addWidget(mTopBar);
    rightColumn->addWidget(mContentStack);
    rightColumn->addWidget(mBottomBar);
    rightColumn->setStretch(1, 1);  // content expands
    rowLayout->addLayout(rightColumn);
    centralLayout->addLayout(rowLayout);
}

void MainWindow::onNavClicked(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= PageCount) return;
    if (pageIndex == mCurrentPage) return;

    // Update nav button styles
    for (int i = 0; i < mNavButtons.size(); ++i) {
        QPushButton* btn = mNavButtons[i];
        if (i == pageIndex) {
            btn->setStyleSheet(
                "QPushButton { "
                "background: #162A44; "
                "border-left: 3px solid #4A90D9; "
                "color: #E8EEF5; "
                "padding: 10px 16px; "
                "font-size: 14px; "
                "font-weight: 500; "
                "}"
                "QPushButton:hover { "
                "background: #162A44; "
                "color: #E8EEF5; "
                "}"
            );
        } else {
            btn->setStyleSheet(
                "QPushButton { "
                "background: transparent; "
                "border: none; "
                "border-left: 3px solid transparent; "
                "color: #8FA3BF; "
                "padding: 10px 16px; "
                "border-radius: 0; "
                "font-size: 14px; "
                "font-weight: 500; "
                "}"
                "QPushButton:hover { "
                "background: #162A44; "
                "color: #E8EEF5; "
                "}"
            );
        }
    }

    mCurrentPage = pageIndex;
    mContentStack->setCurrentIndex(pageIndex);

    // Update page title
    const char* titles[] = { "Dashboard", "Chart", "History", "Health", "Settings" };
    mPageTitle->setText(titles[pageIndex]);

    // Start/stops polling as needed
    if (pageIndex == 0) {  // Dashboard
        if (mApiClient) mApiClient->fetchAnalysisLatest();
        mAnalysisPollTimer.start();
    } else {
        mAnalysisPollTimer.stop();
    }

    if (pageIndex == 1) {  // Chart
        // Chart polling handled by ChartPage
    }
}

void MainWindow::onRefreshClicked() {
    if (mApiClient) {
        mApiClient->fetchAnalysisLatest();
        mApiClient->fetchHealth();
    }
}

void MainWindow::onThemeToggled() {
    ThemeManager::Theme newTheme =
        mThemeManager->currentTheme() == ThemeManager::Theme::Dark
            ? ThemeManager::Theme::Light
            : ThemeManager::Theme::Dark;
    mThemeManager->setTheme(newTheme);
    mSettings.setValue("themeDark", mThemeManager->isDark());
    // Update icon
    mThemeBtn->setText(mThemeManager->isDark() ? "\u263D" : "\u2600");  // moon / sun
    applyTheme();
}

void MainWindow::onFullscreenToggled() {
    toggleFullscreen(!mIsFullscreen);
}

void MainWindow::onCloseClicked() {
    showExitConfirmation();
}

void MainWindow::onExitClicked() {
    showExitConfirmation();
}

void MainWindow::onExitConfirmed(bool confirmed) {
    mExitConfirmed = confirmed;
    if (confirmed) {
        // Save settings
        mSettings.setValue("windowGeometry", saveGeometry());
        mSettings.setValue("windowState", saveState());
        mSettings.setValue("themeDark", mThemeManager->isDark());
        qApp->quit();
    }
}

void MainWindow::onAnalysisUpdated(const AnalysisResponse& /*resp*/) {
    // Update dashboard page
    if (mCurrentPage == Dashboard) {
        auto* dash = qobject_cast<DashboardPage*>(mPages[Dashboard]);
        if (dash) dash->updateFromAnalysis(mApiClient->currentAnalysis());
    }
    updateLivenessIndicator();
    updateBottomBarStatus();
}

void MainWindow::onHealthUpdated(const HealthResponse& resp) {
    updateLivenessIndicator();
    updateBottomBarStatus();

    // Update health page if visible
    if (mCurrentPage == Health) {
        auto* hp = qobject_cast<HealthPage*>(mPages[Health]);
        if (hp) hp->updateFromHealth(resp);
    }
}

void MainWindow::onOffline() {
    updateLivenessIndicator();
    updateBottomBarStatus();
}

void MainWindow::onOnline() {
    updateLivenessIndicator();
    updateBottomBarStatus();
}

void MainWindow::updateLivenessIndicator() {
    if (!mApiClient) {
        mLiveDot->setStyleSheet("QLabel { background: #D95A5A; border-radius: 4px; }");
        mLiveLabel->setText("OFFLINE");
        mLiveLabel->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; font-weight: 500; }");
        return;
    }

    if (!mApiClient->isOnline()) {
        mLiveDot->setStyleSheet("QLabel { background: #D95A5A; border-radius: 4px; }");
        mLiveLabel->setText("OFFLINE");
        mLiveLabel->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; font-weight: 500; }");
        return;
    }

    auto health = mApiClient->currentHealth();
    if (health.data.status == "degraded") {
        mLiveDot->setStyleSheet("QLabel { background: #D9A14A; border-radius: 4px; }");
        mLiveLabel->setText("DEGRADED");
        mLiveLabel->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; font-weight: 500; }");
        return;
    }

    // Online and not degraded
    mLiveDot->setStyleSheet(
        "QLabel { "
        "background: #4CAF7A; "
        "border-radius: 4px; "
        "}"
    );
    mLiveLabel->setText("LIVE");
    mLiveLabel->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; font-weight: 500; }");

    // Pulse animation via QPropertyAnimation would go here.
    // For simplicity, we use a timer-based opacity toggle.
}

void MainWindow::updateBottomBarStatus() {
    if (!mApiClient) {
        mBackendStatus->setText("\u25CF Backend OFFLINE");
        mBackendStatus->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; }");
        mBridgeStatus->setText("\u25CF Bridge OFFLINE");
        mBridgeStatus->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; }");
        mFreshnessStatus->setText("\u25CF Fresh: \u2014");
        return;
    }

    auto health = mApiClient->currentHealth();
    if (health.data.status == "offline") {
        mBackendStatus->setText("\u25CF Backend OFFLINE");
        mBackendStatus->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; }");
    } else if (health.data.status == "degraded") {
        mBackendStatus->setText("\u25CF Backend DEGRADED");
        mBackendStatus->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
    } else {
        mBackendStatus->setText("\u25CF Backend ONLINE");
        mBackendStatus->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
    }

    if (health.data.bridge == "offline") {
        mBridgeStatus->setText("\u25CF Bridge OFFLINE");
        mBridgeStatus->setStyleSheet("QLabel { color: #D95A5A; font-size: 12px; }");
    } else if (health.data.bridge == "stale") {
        mBridgeStatus->setText("\u25CF Bridge STALE");
        mBridgeStatus->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
    } else {
        mBridgeStatus->setText("\u25CF Bridge OK");
        mBridgeStatus->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
    }

    // Freshness
    auto analysis = mApiClient->currentAnalysis();
    if (analysis.data.meta.dataFreshnessSec.has_value()) {
        int secs = analysis.data.meta.dataFreshnessSec.value();
        mFreshnessStatus->setText(QString("\u25CF Fresh %1s").arg(secs));
        if (secs > 60) {
            mFreshnessStatus->setStyleSheet("QLabel { color: #D9A14A; font-size: 12px; }");
        } else {
            mFreshnessStatus->setStyleSheet("QLabel { color: #4CAF7A; font-size: 12px; }");
        }
    } else {
        mFreshnessStatus->setText("\u25CF Fresh: \u2014");
        mFreshnessStatus->setStyleSheet("QLabel { color: #8FA3BF; font-size: 12px; }");
    }
}

void MainWindow::applyTheme() {
    // Theme is applied via ThemeManager; this just triggers a repolish
    qApp->setStyleSheet(mThemeManager->qssContent());
    // Re-apply widget-specific styling that might have been overridden
    // The QSS handles most styling; individual widget styles supplement
}

void MainWindow::toggleFullscreen(bool enter) {
    if (enter) {
        showFullScreen();
        mIsFullscreen = true;
        // Hide sidebar
        mSidebar->setVisible(false);
        // Top bar: keep tiny hint
        mTopBar->setVisible(true);
        // Show F11 hint
        showFullscreenHint();
    } else {
        showNormal();
        mIsFullscreen = false;
        mSidebar->setVisible(true);
        mTopBar->setVisible(true);
        mFullscreenHintTimer.stop();
        mFullscreenHintVisible = false;
    }
}

void MainWindow::showFullscreenHint() {
    // Create a temporary label at top-right showing "Press F11 to exit fullscreen"
    // Fades after 3 seconds
    if (mFullscreenHintVisible) return;
    mFullscreenHintVisible = true;

    QLabel* hint = new QLabel(this);
    hint->setText("Press F11 to exit fullscreen");
    hint->setStyleSheet(
        "QLabel { "
        "background: rgba(15, 31, 53, 0.9); "
        "color: #8FA3BF; "
        "font-size: 12px; "
        "padding: 6px 12px; "
        "border-radius: 6px; "
        "}"
    );
    hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    hint->move(width() - 280, 12);
    hint->setVisible(true);

    mFullscreenHintTimer.start(3000);
    connect(&mFullscreenHintTimer, &QTimer::timeout, hint, [hint]() {
        hint->setVisible(false);
        hint->deleteLater();
    });
}

void MainWindow::hideFullscreenHint() {
    mFullscreenHintTimer.stop();
    mFullscreenHintVisible = false;
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Global shortcuts
    if (event->matches(QKeySequence::Close)) {
        showExitConfirmation();
        return;
    }

    if ((event->key() == Qt::Key_Q && event->modifiers().testFlag(Qt::ControlModifier))) {
        showExitConfirmation();
        return;
    }

    if ((event->key() == Qt::Key_R && event->modifiers().testFlag(Qt::ControlModifier))) {
        onRefreshClicked();
        return;
    }

    if ((event->key() == Qt::Key_T && event->modifiers().testFlag(Qt::ControlModifier))) {
        onThemeToggled();
        return;
    }

    if ((event->key() == Qt::Key_Comma && event->modifiers().testFlag(Qt::ControlModifier))) {
        onNavClicked(Settings);
        return;
    }

    if ((event->key() == Qt::Key_D && event->modifiers().testFlag(Qt::ControlModifier))) {
        onNavClicked(Dashboard);
        return;
    }

    if ((event->key() == Qt::Key_H && event->modifiers().testFlag(Qt::ControlModifier))) {
        onNavClicked(Chart);
        return;
    }

    if ((event->key() == Qt::Key_L && event->modifiers().testFlag(Qt::ControlModifier))) {
        onNavClicked(History);
        return;
    }

    if ((event->key() == Qt::Key_K && event->modifiers().testFlag(Qt::ControlModifier))) {
        onNavClicked(Health);
        return;
    }

    // F11 — fullscreen toggle
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen(!mIsFullscreen);
        return;
    }

    // ESC — exit fullscreen or cancel dialog
    if (event->key() == Qt::Key_Escape) {
        if (mIsFullscreen) {
            toggleFullscreen(false);
            return;
        }
        // Check if exit dialog is open
        // (handled by the dialog itself)
    }

    // Chart page: 1-9 for timeframe
    if (mCurrentPage == Chart && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        auto* chart = qobject_cast<ChartPage*>(mPages[Chart]);
        if (chart) chart->onTimeframeKey(event->key() - Qt::Key_1);
    }

    // Chart page: +/- zoom
    if (mCurrentPage == Chart) {
        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
            auto* chart = qobject_cast<ChartPage*>(mPages[Chart]);
            if (chart) chart->zoomIn();
        } else if (event->key() == Qt::Key_Minus) {
            auto* chart = qobject_cast<ChartPage*>(mPages[Chart]);
            if (chart) chart->zoomOut();
        } else if ((event->key() == Qt::Key_0 && event->modifiers().testFlag(Qt::ControlModifier))) {
            auto* chart = qobject_cast<ChartPage*>(mPages[Chart]);
            if (chart) chart->resetZoom();
        }
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    mExitConfirmed = false;
    showExitConfirmation();
    if (mExitConfirmed) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::showExitConfirmation() {
    ConfirmExitDialog* dialog = new ConfirmExitDialog(this);
    connect(dialog, &ConfirmExitDialog::confirmed, this, &MainWindow::onExitConfirmed);
    connect(dialog, &ConfirmExitDialog::rejected, this, [this, dialog]() {
        mExitConfirmed = false;
        dialog->deleteLater();
    });
    dialog->open();
}


}  // namespace astra
