#ifndef MEERU_PAINT_H
#define MEERU_PAINT_H

#include <QColor>
#include <QPixmap>
#include <QRectF>
#include <QString>

class QPainter;

namespace MeeruPaint {

// The presence "glass" halo used around the logo on the login screen and
// around the user avatar on the main window. Kept in one place so both
// windows animate and shade identically.
//
// Drawn in two steps so callers can slot the picture in between: the outer
// glow must stay behind the picture, never tinted over it.
void drawPresenceGlow(QPainter &painter,
                      const QRectF &widgetRect,
                      const QColor &color);

void drawPresenceRing(QPainter &painter,
                      const QRectF &frameRect,
                      const QColor &color,
                      qreal radius,
                      qreal strokeWidth);

// Convenience for callers whose picture is a child widget painted on top.
void drawPresenceHalo(QPainter &painter,
                      const QRectF &widgetRect,
                      const QRectF &frameRect,
                      const QColor &color,
                      qreal radius,
                      qreal strokeWidth);

// A glossy status dot with a contrast ring, so it reads on dark chrome and on
// top of a photo banner alike. presenceBadge centres the dot inside a larger
// transparent box, which is what menu and combo icon slots expect.
QPixmap presenceBadge(const QColor &color, int boxSize, int dotSize);
QPixmap presenceDot(const QColor &color, int diameter = 12);

// "Mathery" -> "M", "Mathery Automation" -> "MA"
QString initialsFor(const QString &displayName);

// Rounded avatar tile with the lavender/pink gradient and centred initials.
QPixmap initialsTile(const QString &initials, const QSize &size, qreal radius);

// Crops the largest centred square from a pixmap and scales it to size.
QPixmap roundedFromPixmap(const QPixmap &source, const QSize &size, qreal radius);

}

#endif
