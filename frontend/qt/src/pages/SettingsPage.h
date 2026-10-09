#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QEvent>
#include <QVector>
#include "api/ApiClient.h"

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// SettingsPage — General, Appearance, Keyboard Shortcuts (read-only), About
// Save button persists to QSettings
// ──────────────────────────────────────────────────────────────────────────────

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    void setApiClient(ApiClient* client);

protected:
    void changeEvent(QEvent* event) override;

private:
    void setupLayout();
    void restyle();
    void loadSettings();
    void saveSettings();
    void showToast(const QString& message);

    QLineEdit* mApiUrlEdit = nullptr;
    QComboBox* mRefreshCombo = nullptr;
    QCheckBox* mFullscreenCheck = nullptr;
    QCheckBox* mCrosshairCheck = nullptr;
    QCheckBox* mSlTpCheck = nullptr;

    QGroupBox* mGeneralGroup = nullptr;
    QGroupBox* mAppearanceGroup = nullptr;
    QGroupBox* mShortcutsGroup = nullptr;
    QGroupBox* mAboutGroup = nullptr;

    QLabel* mThemeDarkRadio = nullptr;
    QLabel* mThemeLightRadio = nullptr;
    QComboBox* mFontSizeCombo = nullptr;

    QVector<QLabel*> mKeyLabels;
    QVector<QLabel*> mDescLabels;
    QLabel* mAbout1 = nullptr;
    QLabel* mAbout2 = nullptr;
    QLabel* mAbout3 = nullptr;

    int mThemeIdx = 0;  // 0 = dark, 1 = light

    QPushButton* mSaveBtn = nullptr;
    QFrame* mToastFrame = nullptr;
    QLabel* mToastLabel = nullptr;

    ApiClient* mApiClient = nullptr;
    QSettings mSettings;
};

}  // namespace astra
