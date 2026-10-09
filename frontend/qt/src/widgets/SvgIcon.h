#pragma once
#include <QWidget>
#include <QString>
#include <QColor>
#include <QSize>
#ifdef ASTRA_HAVE_QTSVG
class QSvgRenderer;
#endif

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// SvgIcon — paints a thin-stroke outline icon from the existing resource set
// (: /icons/*.svg), tinted to a caller-supplied color.
// QPainter only (QSvgRenderer parses the SVG; no QGraphicsView, no QML).
// If QtSvg is unavailable at build time the icon paints nothing and the
// label text carries the meaning.
// ──────────────────────────────────────────────────────────────────────────────

class SvgIcon : public QWidget {
public:
    explicit SvgIcon(const QString& resourcePath, int size = 16, QWidget* parent = nullptr);

    void setResource(const QString& resourcePath);
    void setColor(const QColor& color);

    QSize sizeHint() const override { return QSize(mSize, mSize); }
    QSize minimumSizeHint() const override { return QSize(mSize, mSize); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString mPath;
    int mSize = 16;
    // Invalid by default: paintEvent falls back to the theme's window-text
    // color so icons track light/dark without an explicit setColor().
    QColor mColor;
#ifdef ASTRA_HAVE_QTSVG
    QSvgRenderer* mRenderer = nullptr;
#endif
};

}  // namespace astra
