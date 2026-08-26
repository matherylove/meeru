#ifndef MEERU_AVATAR_H
#define MEERU_AVATAR_H

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include "meeru_paths.h"

class QMovie;
class QPropertyAnimation;

namespace MeeruImage {

// Builds a QMovie that reads from a copy of the file held in memory.
//
// A QMovie created straight from a path keeps the file open for as long as it
// animates, and Windows refuses to delete or replace an open file. Since Meeru
// shows the stored avatar and banner while also letting the user replace or
// erase them, every animation is loaded through a buffer instead. The buffer
// is owned by the movie, so both die together.
QMovie *bufferedMovie(const QString &path, QObject *parent);

int maximumAnimationBytes();

}

// Persists a picture belonging to one identity inside its folder in %APPDATA%.
// The same class backs both the profile picture ("avatar", square) and the
// header banner ("banner", 3:1).
//
// Still images are cropped, scaled and written as <kind>.png. Animated GIFs are
// kept in their original form (Qt cannot re-encode GIF) together with the crop
// rectangle in <kind>.json, and every frame is cropped when it is displayed.
class ImageStore
{
public:
    ImageStore(const MeeruPaths &paths, const QString &identityId, const QString &kind);

    // For pictures that belong to somebody else: the folder is given directly,
    // since a contact's files live under peers/<their id>/ inside our identity.
    ImageStore(const QString &directory, const QString &kind);

    static QString peerDirectory(const MeeruPaths &paths, const QString &ownerId, const QString &peerId);

    bool hasImage() const;
    bool isAnimated() const;
    QString filePath() const;
    QRect cropRect() const;
    QString kind() const { return kind_; }

    bool saveStill(const QImage &cropped, QString *error = 0);
    bool saveAnimated(const QString &sourceGifPath, const QRect &crop, QString *error = 0);
    bool removeImage(QString *error = 0);

    // Writes a picture that arrived from a contact, keeping the same layout on
    // disk as one the user chose, so the widgets read both the same way.
    bool saveReceived(const QByteArray &bytes, bool animated, const QRect &crop, QString *error = 0);

    void refresh() { reload(); }

    // Stored resolution for still images of this kind.
    QSize storedSize() const;
    qreal aspect() const;   // width / height

private:
    QString directory() const;
    QString metadataPath() const;
    void reload();
    bool clearFiles(QString *error);
    static bool discard(const QString &path);

    MeeruPaths paths_;
    QString identityId_;
    QString explicitDirectory_;
    QString kind_;
    QString file_;
    QRect crop_;
    bool animated_;
};

// Avatar tile with the animated presence halo. Falls back to the user's
// initials when no picture has been chosen.
class AvatarFrame : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor presenceColor READ presenceColor WRITE setPresenceColorImmediate)

public:
    explicit AvatarFrame(QWidget *parent = 0);
    ~AvatarFrame();

    void setTileSize(int size);          // inner picture size, halo is added around it
    void setInitials(const QString &initials);
    void setPresenceColor(const QColor &color, bool animated = true);
    QColor presenceColor() const { return color_; }
    void setPresenceColorImmediate(const QColor &color);

    void setImage(const ImageStore &store);
    void clearImage();
    void setInteractive(bool interactive);

    // Distance from the widget edge to the picture edge, so surrounding
    // layouts can line text up with the picture instead of with the halo.
    int pictureInset() const;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

private slots:
    void onMovieFrame();

private:
    QRectF frameRect() const;
    QRectF innerRect() const;
    qreal innerRadius() const;
    void rebuildStill();

    int tileSize_;
    qreal margin_;
    qreal radius_;
    qreal stroke_;
    QColor color_;
    QString initials_;
    QPixmap still_;
    QImage source_;
    QMovie *movie_;
    QRect crop_;
    bool pressed_;
    bool interactive_;
    QPropertyAnimation *animation_;
};

// The banner behind the profile header. Paints the mockup gradient when the
// user has not chosen a picture of their own.
class BannerFrame : public QWidget
{
    Q_OBJECT

public:
    explicit BannerFrame(QWidget *parent = 0);
    ~BannerFrame();

    void setImage(const ImageStore &store);
    void clearImage();

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

private slots:
    void onMovieFrame();

private:
    QImage source_;
    QMovie *movie_;
    QRect crop_;
    bool pressed_;
};

#endif
