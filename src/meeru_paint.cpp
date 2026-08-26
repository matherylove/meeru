#include "meeru_paint.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStringList>

#include "meeru_style.h"

void MeeruPaint::drawPresenceGlow(QPainter &painter, const QRectF &widgetRect, const QColor &color)
{
    QRadialGradient glow(widgetRect.center(), widgetRect.width() / 2.0);
    QColor glowColor = color;
    glowColor.setAlpha(90);
    glow.setColorAt(0.75, Qt::transparent);
    glow.setColorAt(0.92, glowColor);
    glow.setColorAt(1.0, Qt::transparent);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawRect(widgetRect);
}

void MeeruPaint::drawPresenceRing(QPainter &painter,
                                  const QRectF &frameRect,
                                  const QColor &color,
                                  qreal radius,
                                  qreal strokeWidth)
{
    QPen basePen(color.darker(115), strokeWidth);
    painter.setPen(basePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frameRect, radius, radius);

    QLinearGradient glassGradient(frameRect.topLeft(), frameRect.bottomLeft());
    QColor highlight = color.lighter(160);
    highlight.setAlpha(210);
    QColor midTone = color;
    midTone.setAlpha(160);
    QColor shadowTone = color.darker(130);
    shadowTone.setAlpha(190);
    glassGradient.setColorAt(0.0, highlight);
    glassGradient.setColorAt(0.45, midTone);
    glassGradient.setColorAt(1.0, shadowTone);

    QPen glassPen(QBrush(glassGradient), strokeWidth * 0.64);
    painter.setPen(glassPen);
    const qreal glassInset = strokeWidth * 0.18;
    painter.drawRoundedRect(frameRect.adjusted(glassInset, glassInset, -glassInset, -glassInset),
                            radius - glassInset, radius - glassInset);

    QPainterPath specularPath;
    QRectF specularRect(frameRect.left() + radius * 0.27,
                        frameRect.top() + strokeWidth * 0.6,
                        frameRect.width() - radius * 0.54,
                        frameRect.height() * 0.42);
    if (specularRect.width() <= 0 || specularRect.height() <= 0)
        return;

    const qreal specularRadius = qMax(qreal(0.0), radius - strokeWidth * 0.8);
    specularPath.addRoundedRect(specularRect, specularRadius, specularRadius);
    QLinearGradient specularGradient(specularRect.topLeft(), specularRect.bottomLeft());
    specularGradient.setColorAt(0.0, QColor(255, 255, 255, 70));
    specularGradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(specularGradient);
    painter.setClipPath(specularPath);
    painter.drawRect(specularRect);
    painter.setClipping(false);
}

void MeeruPaint::drawPresenceHalo(QPainter &painter,
                                  const QRectF &widgetRect,
                                  const QRectF &frameRect,
                                  const QColor &color,
                                  qreal radius,
                                  qreal strokeWidth)
{
    drawPresenceGlow(painter, widgetRect, color);
    drawPresenceRing(painter, frameRect, color, radius, strokeWidth);
}

QPixmap MeeruPaint::presenceBadge(const QColor &color, int boxSize, int dotSize)
{
    if (boxSize < 2)
        boxSize = 2;
    dotSize = qBound(2, dotSize, boxSize);

    QPixmap pixmap(boxSize, boxSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal offset = (boxSize - dotSize) / 2.0;
    const QRectF circle(offset + 0.5, offset + 0.5, dotSize - 1.0, dotSize - 1.0);

    QColor ring = color.darker(190);
    ring.setAlpha(200);
    painter.setPen(QPen(ring, 1.0));
    painter.setBrush(color);
    painter.drawEllipse(circle);

    if (dotSize >= 7) {
        QColor gloss = color.lighter(170);
        gloss.setAlpha(190);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gloss);
        painter.drawEllipse(QRectF(circle.left() + dotSize * 0.24,
                                   circle.top() + dotSize * 0.17,
                                   dotSize * 0.36, dotSize * 0.28));
    }
    return pixmap;
}

QPixmap MeeruPaint::presenceDot(const QColor &color, int diameter)
{
    return presenceBadge(color, diameter, diameter);
}

QString MeeruPaint::initialsFor(const QString &displayName)
{
    const QString trimmed = displayName.trimmed();
    if (trimmed.isEmpty())
        return QString::fromLatin1("?");

    const QStringList words = trimmed.split(QLatin1Char(' '), QString::SkipEmptyParts);
    QString initials;
    for (int i = 0; i < words.size() && initials.size() < 2; ++i) {
        const QString word = words.at(i);
        if (!word.isEmpty())
            initials.append(word.at(0));
    }
    if (initials.isEmpty())
        initials = trimmed.left(1);
    return initials.toUpper();
}

QPixmap MeeruPaint::initialsTile(const QString &initials, const QSize &size, qreal radius)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    if (size.isEmpty())
        return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0.0, MeeruStyle::lavender());
    gradient.setColorAt(1.0, MeeruStyle::pink());
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);

    QFont font(QString::fromLatin1("Segoe UI"));
    font.setBold(true);
    font.setPixelSize(qMax(8, static_cast<int>(size.height() * 0.42)));
    painter.setFont(font);
    painter.setPen(MeeruStyle::avatarInk());
    painter.drawText(QRectF(0, 0, size.width(), size.height()), Qt::AlignCenter, initials);
    return pixmap;
}

QPixmap MeeruPaint::roundedFromPixmap(const QPixmap &source, const QSize &size, qreal radius)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    if (source.isNull() || size.isEmpty())
        return pixmap;

    QPixmap scaled = source;
    if (scaled.size() != size)
        scaled = scaled.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
    painter.setClipPath(path);
    const QRect sourceRect((scaled.width() - size.width()) / 2,
                           (scaled.height() - size.height()) / 2,
                           size.width(), size.height());
    painter.drawPixmap(QRect(0, 0, size.width(), size.height()), scaled, sourceRect);
    return pixmap;
}
