#include "SettingsPage.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <QFont>
#include <QPalette>

namespace astra {

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
    , mSettings("ASTRA", "Desktop")
{
    setupLayout();
    loadSettings();
    restyle();
}

void SettingsPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        restyle();
    }
    QWidget::changeEvent(event);
}

void SettingsPage::setApiClient(ApiClient* client) {
    mApiClient = client;
}

void SettingsPage::restyle() {
    const QPalette pal = palette();
    const QString textPrimary = pal.color(QPalette::Text).name();
    const QString textSecondary = pal.color(QPalette::WindowText).name();
    const QString textTertiary = pal.color(QPalette::PlaceholderText).name();
    const QString surface = pal.color(QPalette::Base).name();
    const QString surfaceAlt = pal.color(QPalette::AlternateBase).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString accent = pal.color(QPalette::Highlight).name();

    const QString groupStyle =
        QString("QGroupBox { color: %1; font-size: 12px; font-weight: 500; "
                "letter-spacing: 0.05em; padding-top: 20px; margin-top: 0; }"
                "QGroupBox::title { subcontrol-origin: margin; left: 20px; }")
            .arg(textSecondary);
    for (QGroupBox* g : {mGeneralGroup, mAppearanceGroup, mShortcutsGroup, mAboutGroup}) {
        if (g) g->setStyleSheet(groupStyle);
    }

    const QString editStyle =
        QString("QLineEdit { background: %1; border: 1px solid %2; color: %3; "
                "border-radius: 8px; padding: 8px 12px; font-size: 14px; "
                "font-family: 'JetBrains Mono', 'Consolas', monospace; }"
                "QLineEdit:hover { border-color: %4; }"
                "QLineEdit:focus { border-color: %5; }")
            .arg(surface, border, textPrimary, textTertiary, accent);
    if (mApiUrlEdit) mApiUrlEdit->setStyleSheet(editStyle);

    const QString comboStyle =
        QString("QComboBox { background: %1; border: 1px solid %2; color: %3; "
                "border-radius: 8px; padding: 8px 12px; font-size: 14px; }"
                "QComboBox:hover { border-color: %4; }"
                "QComboBox:focus { border-color: %4; }")
            .arg(surface, border, textPrimary, accent);
    if (mRefreshCombo) mRefreshCombo->setStyleSheet(comboStyle);
    if (mFontSizeCombo) mFontSizeCombo->setStyleSheet(comboStyle);

    const QString checkStyle =
        QString("QCheckBox { color: %1; spacing: 8px; }"
                "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %2; "
                "border-radius: 3px; background: %3; }"
                "QCheckBox::indicator:checked { background: %4; border-color: %4; }")
            .arg(textPrimary, border, surface, accent);
    for (QCheckBox* c : {mFullscreenCheck, mCrosshairCheck, mSlTpCheck}) {
        if (c) c->setStyleSheet(checkStyle);
    }

    // Theme radio labels — active one uses the accent.
    if (mThemeDarkRadio) {
        mThemeDarkRadio->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; }")
                                           .arg(mThemeIdx == 0 ? accent : textPrimary));
    }
    if (mThemeLightRadio) {
        mThemeLightRadio->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; }")
                                            .arg(mThemeIdx == 1 ? accent : textPrimary));
    }

    for (QLabel* l : mKeyLabels) {
        if (l) {
            l->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; "
                                     "font-family: 'JetBrains Mono', 'Consolas', monospace; "
                                     "font-weight: 500; }").arg(accent));
        }
    }
    for (QLabel* l : mDescLabels) {
        if (l) l->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; }").arg(textSecondary));
    }
    if (mAbout1) {
        mAbout1->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; "
                                       "font-weight: 500; }").arg(textPrimary));
    }
    if (mAbout2) mAbout2->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; }").arg(textSecondary));
    if (mAbout3) mAbout3->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textTertiary));

    if (mSaveBtn) {
        mSaveBtn->setStyleSheet(
            QString("QPushButton { background: %1; color: white; border: 1px solid %1; "
                    "border-radius: 8px; padding: 12px 24px; font-size: 14px; "
                    "font-weight: 500; }").arg(accent));
    }
    if (mToastFrame) {
        mToastFrame->setStyleSheet(
            QString("QFrame { background: %1; border: 1px solid #4CAF7A; "
                    "border-radius: 8px; }").arg(surface));
    }
    if (mToastLabel) mToastLabel->setStyleSheet("QLabel { color: #4CAF7A; font-size: 14px; }");
}

void SettingsPage::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // ── GENERAL ──
    mGeneralGroup = new QGroupBox(this);
    mGeneralGroup->setTitle("GENERAL");
    QFormLayout* generalLayout = new QFormLayout(mGeneralGroup);
    generalLayout->setSpacing(12);
    generalLayout->setLabelAlignment(Qt::AlignLeft);

    mApiUrlEdit = new QLineEdit(mGeneralGroup);
    mApiUrlEdit->setText("http://127.0.0.1:8790/api/v1/");
    generalLayout->addRow("API Base URL", mApiUrlEdit);

    mRefreshCombo = new QComboBox(mGeneralGroup);
    mRefreshCombo->addItem("5s");
    mRefreshCombo->addItem("10s");
    mRefreshCombo->addItem("15s");
    mRefreshCombo->addItem("30s");
    generalLayout->addRow("Refresh Interval", mRefreshCombo);

    mFullscreenCheck = new QCheckBox(mGeneralGroup);
    mFullscreenCheck->setText("Start in Fullscreen");
    generalLayout->addRow(mFullscreenCheck);

    mCrosshairCheck = new QCheckBox(mGeneralGroup);
    mCrosshairCheck->setText("Show Crosshair");
    mCrosshairCheck->setChecked(true);
    generalLayout->addRow(mCrosshairCheck);

    mSlTpCheck = new QCheckBox(mGeneralGroup);
    mSlTpCheck->setText("Show SL/TP Overlay");
    mSlTpCheck->setChecked(true);
    generalLayout->addRow(mSlTpCheck);

    mainLayout->addWidget(mGeneralGroup);

    // ── APPEARANCE ──
    mAppearanceGroup = new QGroupBox(this);
    mAppearanceGroup->setTitle("APPEARANCE");
    QFormLayout* appLayout = new QFormLayout(mAppearanceGroup);
    appLayout->setSpacing(12);

    // Theme: Dark / Light radio buttons
    QHBoxLayout* themeRow = new QHBoxLayout();
    themeRow->setSpacing(24);

    mThemeDarkRadio = new QLabel(mAppearanceGroup);
    mThemeDarkRadio->setText("\u25A1 Dark");
    themeRow->addWidget(mThemeDarkRadio);

    mThemeLightRadio = new QLabel(mAppearanceGroup);
    mThemeLightRadio->setText("\u25A1 Light");
    themeRow->addWidget(mThemeLightRadio);

    appLayout->addRow("Theme", themeRow);

    // Font size
    mFontSizeCombo = new QComboBox(mAppearanceGroup);
    mFontSizeCombo->addItem("Small");
    mFontSizeCombo->addItem("Normal");
    mFontSizeCombo->addItem("Large");
    appLayout->addRow("Font Size", mFontSizeCombo);

    mainLayout->addWidget(mAppearanceGroup);

    // ── KEYBOARD SHORTCUTS (read-only) ──
    mShortcutsGroup = new QGroupBox(this);
    mShortcutsGroup->setTitle("KEYBOARD SHORTCUTS");
    QGridLayout* shortcutLayout = new QGridLayout(mShortcutsGroup);
    shortcutLayout->setSpacing(4);
    shortcutLayout->setContentsMargins(20, 16, 20, 16);

    struct Shortcut { const char* key; const char* desc; };
    Shortcut shortcuts[] = {
        {"F11", "Toggle fullscreen"},
        {"ESC", "Exit fullscreen / cancel"},
        {"Ctrl+Q", "Exit"},
        {"Ctrl+R", "Refresh"},
        {"Ctrl+D", "Dashboard"},
        {"Ctrl+H", "Chart"},
        {"Ctrl+L", "History"},
        {"Ctrl+K", "Health"},
        {"Ctrl+,", "Settings"},
        {"1..9", "Timeframe"},
        {"Ctrl+T", "Toggle theme"},
    };

    for (int i = 0; i < 11; ++i) {
        QLabel* keyLbl = new QLabel(mShortcutsGroup);
        keyLbl->setText(shortcuts[i].key);

        QLabel* descLbl = new QLabel(mShortcutsGroup);
        descLbl->setText(shortcuts[i].desc);

        shortcutLayout->addWidget(keyLbl, i, 0);
        shortcutLayout->addWidget(descLbl, i, 1);
        shortcutLayout->setColumnMinimumWidth(0, 80);
        mKeyLabels.append(keyLbl);
        mDescLabels.append(descLbl);
    }

    mainLayout->addWidget(mShortcutsGroup);

    // ── ABOUT ──
    mAboutGroup = new QGroupBox(this);
    mAboutGroup->setTitle("ABOUT");
    QVBoxLayout* aboutLayout = new QVBoxLayout(mAboutGroup);
    aboutLayout->setContentsMargins(20, 16, 20, 16);
    aboutLayout->setSpacing(4);

    mAbout1 = new QLabel(mAboutGroup);
    mAbout1->setText("ASTRA Desktop v1.0");
    aboutLayout->addWidget(mAbout1);

    mAbout2 = new QLabel(mAboutGroup);
    mAbout2->setText("Backend API v1");
    aboutLayout->addWidget(mAbout2);

    mAbout3 = new QLabel(mAboutGroup);
    mAbout3->setText("Decision support only. Not financial advice.");
    aboutLayout->addWidget(mAbout3);

    mainLayout->addWidget(mAboutGroup);

    // ── Save button ──
    mSaveBtn = new QPushButton(this);
    mSaveBtn->setText("Save");
    mSaveBtn->setProperty("primary", true);
    connect(mSaveBtn, &QPushButton::clicked, this, &SettingsPage::saveSettings);
    mainLayout->addWidget(mSaveBtn);
    mainLayout->addStretch();

    // Toast (hidden by default)
    mToastFrame = new QFrame(this);
    mToastFrame->setProperty("astraCard", true);
    mToastFrame->setFrameStyle(QFrame::NoFrame);
    mToastFrame->setFixedHeight(40);
    mToastLabel = new QLabel(mToastFrame);
    mToastLabel->setText("");
    mToastLabel->setAlignment(Qt::AlignCenter);
    QHBoxLayout* toastLayout = new QHBoxLayout(mToastFrame);
    toastLayout->setContentsMargins(16, 0, 16, 0);
    toastLayout->addWidget(mToastLabel);
    mToastFrame->setVisible(false);
    mainLayout->addWidget(mToastFrame);
}

void SettingsPage::loadSettings() {
    mApiUrlEdit->setText(mSettings.value("apiUrl", "http://127.0.0.1:8790/api/v1/").toString());
    int refreshIdx = mSettings.value("refreshInterval", 0).toInt();
    mRefreshCombo->setCurrentIndex(qBound(0, refreshIdx, mRefreshCombo->count() - 1));
    mFullscreenCheck->setChecked(mSettings.value("startFullscreen", false).toBool());
    mCrosshairCheck->setChecked(mSettings.value("showCrosshair", true).toBool());
    mSlTpCheck->setChecked(mSettings.value("showSlTp", true).toBool());
    int themeIdx = mSettings.value("theme", 0).toInt();
    // 0 = dark, 1 = light
    mThemeIdx = themeIdx;
    int fontIdx = mSettings.value("fontSize", 1).toInt();
    mFontSizeCombo->setCurrentIndex(qBound(0, fontIdx, mFontSizeCombo->count() - 1));
    restyle();
}

void SettingsPage::saveSettings() {
    mSettings.setValue("apiUrl", mApiUrlEdit->text());
    mSettings.setValue("refreshInterval", mRefreshCombo->currentIndex());
    mSettings.setValue("startFullscreen", mFullscreenCheck->isChecked());
    mSettings.setValue("showCrosshair", mCrosshairCheck->isChecked());
    mSettings.setValue("showSlTp", mSlTpCheck->isChecked());
    mSettings.setValue("fontSize", mFontSizeCombo->currentIndex());

    // Theme (0=dark, 1=light)
    mSettings.setValue("theme", mThemeIdx);

    showToast("Settings saved");
}

void SettingsPage::showToast(const QString& message) {
    mToastLabel->setText(message);
    mToastFrame->setVisible(true);
    QTimer::singleShot(2000, this, [this]() {
        mToastFrame->setVisible(false);
    });
}

}  // namespace astra
