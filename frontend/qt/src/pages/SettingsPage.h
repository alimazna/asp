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

private:
    void setupLayout();
    void loadSettings();
    void saveSettings();
    void showToast(const QString& message);

    QLineEdit* mApiUrlEdit = nullptr;
    QComboBox* mRefreshCombo = nullptr;
    QCheckBox* mFullscreenCheck = nullptr;
    QCheckBox* mCrosshairCheck = nullptr;
    QCheckBox* mSlTpCheck = nullptr;

    QLabel* mThemeDarkRadio = nullptr;
    QLabel* mThemeLightRadio = nullptr;
    QComboBox* mFontSizeCombo = nullptr;

    QPushButton* mSaveBtn = nullptr;
    QFrame* mToastFrame = nullptr;
    QLabel* mToastLabel = nullptr;

    ApiClient* mApiClient = nullptr;
    QSettings mSettings;
};

}  // namespace astra
