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
#include <QElapsedTimer>
#include <QVector>

class QPushButton;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QVariantAnimation;
class QSequentialAnimationGroup;

// Total time the frontend waits for a backend it spawned to bind 8790 before
// declaring the auto-start failed. The backend binds only after the bridge
// handshake, which cold-starts MT5 on first launch and genuinely needs 25-40s on
// slower hardware; 20s expired before the backend answered and the poll then gave
// up for good. 60s covers the cold start, and the poll never stops permanently
// even after this expiry (see onHealthUpdated's recovery path). Declared before
// the class because the MainWindow member's default references it.
static constexpr int kAutostartWindowMs = 60000;

class DashboardPage;
class ChartPage;
class HistoryPage;
class HealthPage;
class ResearchPage;
class KnowledgePage;
class ApprovalPage;
class GovernancePage;
class IncidentsPage;
class SettingsPage;
class RecoveryPage;
class ComingSoonPage;

namespace astra {

class NavButton;
class SvgIcon;

// Pure label for the auto-start "backend reachable" log line. Extracted from
// MainWindow so the timing semantics are unit-testable on any platform (the log
// itself is written only on Windows). Two windows can share the log file: a
// window that adopted a backend it did not spawn must not claim a long-start
// recovery, and must say the backend was already running, so the log cannot show
// two contradictory "after Ns" lines for one backend.
inline QString autostartReachabilityLabel(qint64 secs, bool preexisting, int windowMs) {
    if (!preexisting && secs > windowMs / 1000) {
        return QStringLiteral(
            "ui recovered: backend reachable after %1s (long-start)").arg(secs);
    }
    const QString suffix = preexisting
                               ? QStringLiteral(" (backend already running)")
                               : QString();
    return QStringLiteral("backend reachable after %1s%2").arg(secs).arg(suffix);
}

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
    // Surfaces a failed auto-start of the backend (missing executable or a
    // refused spawn) as a visible banner with the exact path + reason, instead
    // of the undiagnosable bare "OFFLINE". Windows-only in practice.
    void setBackendStartFailure(const QString& reason);
    // True when the auto-start should retry: the frontend spawned a backend this
    // session and is polling for it to come up. Gates the red failure banner so
    // it only appears when the full window expired without any response.
    void setBackendAutostartInProgress(bool inProgress) {
        mAutostartInProgress = inProgress;
        if (inProgress) mAutostartSession = true;
    }
    // Wall-clock time (QDateTime::currentMSecsSinceEpoch) at which the frontend
    // actually spawned the backend. Set by main() immediately after the spawn so
    // the bake-in window and the "reachable after Ns" log measure from the spawn,
    // not from the (possibly much later) point where the health poll starts. A
    // synchronous startup step used to delay that start by tens of seconds, which
    // both stalled the first probe and made "after Ns" contradict the log clock.
    void setBackendAutostartSpawnedAt(qint64 msecsSinceEpoch) {
        if (msecsSinceEpoch > 0) mAutostartSpawnEpochMs = msecsSinceEpoch;
    }
    // True when this window adopted a backend it did not spawn. The reachability
    // log then measures from when this window began probing, and says so, so two
    // windows sharing the log do not emit contradictory "after Ns" lines.
    void setBackendPreexisting(bool preexisting) { mBackendPreexisting = preexisting; }
    // Test-only: shrink the auto-start window so the failure/recovery path can be
    // exercised without a 60s wall-clock wait.
    void setAutostartWindowMsForTest(int ms) { mAutostartWindowMs = ms; }
    // Test-only: drive one health poll without waiting for the 1s/5s/10s timer, so
    // recovery from a mid-run outage can be asserted deterministically.
    void triggerHealthPollForTest() { if (mApiClient) mApiClient->fetchHealth(); }
    // True when the red auto-start failure banner is currently shown. Exposed for
    // the offscreen retry test (the banner is otherwise a private child widget).
    bool startupBannerShown() const { return mStartupBanner && !mStartupBanner->isHidden(); }
    void showExitConfirmation();
    // Applies the shell's inline styles from the current palette. Call once the
    // window is shown (after setStyleSheet propagation has settled).
    void initStyleChrome();

    // Sidebar collapse state (200px expanded <-> 64px icons-only). Exposed for
    // tests and the offscreen snapshot tool. animate=false applies instantly.
    [[nodiscard]] bool isSidebarCollapsed() const { return mSidebarCollapsed; }
    void setSidebarCollapsed(bool collapsed, bool animate);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onNavClicked(int navIndex);
    void onRefreshClicked();
    void onThemeToggled();
    void onFullscreenToggled();
    void onSidebarToggled();
    void onCloseClicked();
    void onExitClicked();
    void onAnalysisUpdated(const AnalysisResponse& resp);
    void onHealthUpdated(const HealthResponse& resp);
    // Called on each failed health poll during the auto-start bake-in.
    void onAutostartHealthMiss();
    // Fires when the wall-clock bake-in window elapses with no successful
    // /health, showing the failure banner.
    void onAutostartWindowExpired();
    void onHistoryUpdated(const QVector<AnalysisData>& items);
    void onTimeframesUpdated(const QVector<TimeframeData>& items);
    void onOffline();
    void onOnline();
    // One-shot candle-fetch evidence for the installed smoke test: the CI check
    // has no backend request log, so the frontend records that GET /candles ran
    // and whether bars came back.
    void onCandlesUpdated(const CandlesResponse& resp);
    void onCandlesFailed(const QString& message, const QString& code);

private:
    void setupSidebar();
    void setupTopBar();
    void setupBottomBar();
    void setupContentArea();
    void updateLivenessIndicator();
    void updateBottomBarStatus();
    void updateClock();
    void applyTheme();
    // Re-applies the shell's inline styles from the current application
    // palette, so the sidebar/top bar/bottom bar follow a theme switch.
    void restyleChrome();
    void toggleFullscreen(bool enter);
    void showFullscreenHint();
    void hideFullscreenHint();
    void animateSidebar(bool collapsed, bool animate);
    void saveSettings();
    void stopAllTimers();
    [[nodiscard]] int navIndexOfPage(int pageIndex) const;

    // Navigation. ComingSoon is retained as the 404 fallback for any sidebar
    // entry that is not wired to a real page.
    enum Page {
        Dashboard, Chart, History, Health,
        Research, Knowledge, Approval, Governance, Incidents,
        Settings, Recovery, ComingSoon, PageCount
    };
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
    QLabel* mSubtitleLabel = nullptr;

    // Collapsible sidebar
    static constexpr int SIDEBAR_EXPANDED = 200;
    static constexpr int SIDEBAR_COLLAPSED = 64;
    QPushButton* mSidebarToggleBtn = nullptr;
    SvgIcon* mSidebarToggleIcon = nullptr;
    bool mSidebarCollapsed = false;
    QSequentialAnimationGroup* mSidebarGroup = nullptr;
    // Every faded text label (nav labels, group headers, lockup). One shared
    // QGraphicsOpacityEffect per label — Qt allows only one effect per widget.
    QVector<QGraphicsOpacityEffect*> mTextEffects;
    // Subset hidden entirely when collapsed (group headers + lockup), so they
    // cannot intercept clicks in the 64px rail.
    QVector<QGraphicsOpacityEffect*> mCollapseHiddenEffects;

    // Top bar
    QWidget* mTopBar = nullptr;
    QLabel* mPageTitle = nullptr;
    QLabel* mHealthDot = nullptr;
    QLabel* mHealthLabel = nullptr;
    QLabel* mClockLabel = nullptr;
    QTimer mClockTimer;
    QPushButton* mFullscreenBtn = nullptr;
    SvgIcon* mFullscreenIcon = nullptr;
    QPushButton* mThemeBtn = nullptr;
    QPushButton* mCloseBtn = nullptr;

    // Bottom bar
    QWidget* mBottomBar = nullptr;
    QLabel* mBackendStatus = nullptr;   // ● Runtime
    QLabel* mBridgeStatus = nullptr;    // ● Streams
    QLabel* mFreshnessStatus = nullptr; // ● Persistence
    QLabel* mDisclaimer = nullptr;      // System Health (right)

    // Startup binder banner: shown when the frontend could not spawn the backend
    // (missing exe / refused spawn). Hidden until setBackendStartFailure() sets it.
    QLabel* mStartupBanner = nullptr;

    // Theme
    ThemeManager* mThemeManager = nullptr;
    // True once setupSidebar/TopBar/BottomBar have built the shell widgets;
    // restyleChrome() must not run before then (PaletteChange fires during
    // construction when the application palette is first applied).
    bool mChromeReady = false;
    bool mIsFullscreen = false;
    QTimer mFullscreenHintTimer;
    bool mFullscreenHintVisible = false;
    QLabel* mFullscreenHintLabel = nullptr;

    // Polling
    QTimer mAnalysisPollTimer;
    QTimer mHealthPollTimer;
    // One-shot wall-clock timer for the auto-start bake-in window. Driving the
    // window from a timer (not from probe-failure counts) keeps the banner
    // timing independent of how long a failed probe takes to time out.
    QTimer mAutostartWindowTimer;
    ApiClient* mApiClient = nullptr;

    // Auto-start bake-in: while the backend is still coming up (binds 8790 only
    // after the bridge handshake, 2-5s), the health poll retries every 1s and
    // must not declare the backend dead on the first miss.
    bool mAutostartInProgress = false;
    bool mBackendReachable = false;
    // True once an auto-start has ever been declared this session (spawn attempt).
    // Keeps the "reachable after Ns" / long-start recovery logging scoped to the
    // auto-start path: a plain session (backend started elsewhere) must not log
    // recovery for every healthy poll.
    bool mAutostartSession = false;
    int mBackendAttempts = 0;
    int mAutostartWindowMs = kAutostartWindowMs;
    QElapsedTimer mAutostartClock;
    // Wall-clock spawn time set by main(); used for the window budget and the
    // "reachable after Ns" log so they measure from the spawn, not from whenever
    // the retry poll happened to start.
    qint64 mAutostartSpawnEpochMs = 0;

    // Last health payload delivered on the wire. The chip and bottom bar render
    // from this, so they reflect the exact response received (and are testable
    // without a live network round-trip).
    HealthResponse mLastHealth;
    bool mHasHealth = false;

    // Chart-data evidence. The chart's first fetch fires during the backend's
    // bake-in and can legitimately fail at the transport layer before the
    // backend binds; logging only the first outcome hid the eventual success.
    // Log every failure (so transient ones are visible) but log the *result*
    // only on change, so the recount converges on the terminal state without
    // flooding while the backend is still coming up.
    bool mLoggedCandleFailure = false;
    bool mLoggedCandleResult = false;
    bool mLastCandleHadData = false;
    QString mLastCandleFailureLine;
    // Set once the "market closed — showing last data" notice is logged, so the
    // evidence line is written exactly once per session.
    bool mLoggedStaleNotice = false;
    // True when this window adopted a backend it did not spawn (see
    // setBackendPreexisting); the reachability log measures from probe start.
    bool mBackendPreexisting = false;

    // Settings
    QSettings mSettings;

    // Current page index
    int mCurrentPage = 0;

    // Exit confirmation state
    bool mExitConfirmed = false;
};

}  // namespace astra
