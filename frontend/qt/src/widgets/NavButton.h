#pragma once
#include <QWidget>
#include <QString>
#include <QColor>
#include <QMouseEvent>

class QEnterEvent;
class QPaintEvent;
class QResizeEvent;

namespace astra {

class SvgIcon;

// ──────────────────────────────────────────────────────────────────────────────
// NavButton — one sidebar row: 36px tall, radius 8px.
// Active: bg surface-2 (#162A44) + 3px accent-blue (#4A90D9) left border.
// Coming-soon: text-tertiary, default cursor, still clickable (opens the
// shared ComingSoonPage).
// ──────────────────────────────────────────────────────────────────────────────

class NavButton : public QWidget {
    Q_OBJECT

public:
    NavButton(const QString& label, const QString& iconPath,
              bool comingSoon, QWidget* parent = nullptr);

    void setActive(bool active);
    [[nodiscard]] bool isActive() const { return mActive; }
    [[nodiscard]] bool isComingSoon() const { return mComingSoon; }
    [[nodiscard]] QString label() const { return mLabel; }

    QSize sizeHint() const override { return QSize(160, 36); }
    QSize minimumSizeHint() const override { return QSize(96, 36); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshIconColor();
    [[nodiscard]] QColor textColor() const;

    QString mLabel;
    bool mActive = false;
    bool mHover = false;
    bool mComingSoon = false;
    SvgIcon* mIcon = nullptr;
};

}  // namespace astra
