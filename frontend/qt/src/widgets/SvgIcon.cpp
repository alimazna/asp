#include "SvgIcon.h"
#include <QPainter>
#include <QPaintEvent>
#ifdef ASTRA_HAVE_QTSVG
#include <QSvgRenderer>
#endif

namespace astra {

SvgIcon::SvgIcon(const QString& resourcePath, int size, QWidget* parent)
    : QWidget(parent)
    , mPath(resourcePath)
    , mSize(size)
{
    setFixedSize(mSize, mSize);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setResource(resourcePath);
}

void SvgIcon::setResource(const QString& resourcePath) {
    mPath = resourcePath;
#ifdef ASTRA_HAVE_QTSVG
    delete mRenderer;
    mRenderer = new QSvgRenderer(mPath, this);
#endif
    update();
}

void SvgIcon::setColor(const QColor& color) {
    if (mColor == color) return;
    mColor = color;
    update();
}

void SvgIcon::paintEvent(QPaintEvent* /*event*/) {
#ifdef ASTRA_HAVE_QTSVG
    if (!mRenderer || !mRenderer->isValid()) return;

    // Render once, then tint with CompositionMode_SourceIn so every icon
    // adopts the nav state color (thin-stroke outline stays intact).
    QImage img(size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter pi(&img);
        mRenderer->render(&pi, QRectF(QPointF(0, 0), QSizeF(size())));
        pi.setCompositionMode(QPainter::CompositionMode_SourceIn);
        pi.fillRect(img.rect(), mColor);
    }
    QPainter p(this);
    p.drawImage(0, 0, img);
#else
    Q_UNUSED(mPath);
#endif
}

}  // namespace astra
