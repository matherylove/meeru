#include "avatar.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSaveFile>
#include <QStringList>

#include "meeru_paint.h"
#include "presence.h"

namespace {

const int kAvatarStoredSide = 256;
const int kBannerStoredWidth = 600;
const int kBannerStoredHeight = 200;

const int kMaxAnimationBytes = 24 * 1024 * 1024;

bool isBanner(const QString &kind)
{
    return kind == QLatin1String("banner");
}

QPixmap cropFrame(const QPixmap &frame, const QRect &crop)
{
    if (frame.isNull())
        return QPixmap();
    QRect area = crop.isValid() ? crop : QRect(QPoint(0, 0), frame.size());
    area = area.intersected(QRect(QPoint(0, 0), frame.size()));
    if (!area.isValid())
        return QPixmap();
    return frame.copy(area);
}

}

int MeeruImage::maximumAnimationBytes()
{
    return kMaxAnimationBytes;
}

QMovie *MeeruImage::bufferedMovie(const QString &path, QObject *parent)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const qint64 size = file.size();
    if (size <= 0 || size > kMaxAnimationBytes)
        return 0;
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
        return 0;

    QBuffer *buffer = new QBuffer();
    buffer->setData(bytes);
    if (!buffer->open(QIODevice::ReadOnly)) {
        delete buffer;
        return 0;
    }

    QByteArray format;
    if (path.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive))
        format = "gif";

    QMovie *movie = new QMovie(buffer, format, parent);
    buffer->setParent(movie);
    if (!movie->isValid()) {
        delete movie;
        return 0;
    }
    return movie;
}

ImageStore::ImageStore(const MeeruPaths &paths, const QString &identityId, const QString &kind)
    : paths_(paths), identityId_(identityId), kind_(kind), animated_(false)
{
    reload();
}

ImageStore::ImageStore(const QString &directory, const QString &kind)
    : explicitDirectory_(directory), kind_(kind), animated_(false)
{
    reload();
}

QString ImageStore::peerDirectory(const MeeruPaths &paths, const QString &ownerId, const QString &peerId)
{
    return paths.identityDirectory(ownerId) + QLatin1String("/peers/") + peerId;
}

QSize ImageStore::storedSize() const
{
    if (isBanner(kind_))
        return QSize(kBannerStoredWidth, kBannerStoredHeight);
    return QSize(kAvatarStoredSide, kAvatarStoredSide);
}

qreal ImageStore::aspect() const
{
    const QSize size = storedSize();
    return qreal(size.width()) / qreal(size.height());
}

QString ImageStore::directory() const
{
    if (!explicitDirectory_.isEmpty())
        return explicitDirectory_;
    return paths_.identityDirectory(identityId_);
}

QString ImageStore::metadataPath() const
{
    return directory() + QLatin1Char('/') + kind_ + QLatin1String(".json");
}

void ImageStore::reload()
{
    file_.clear();
    crop_ = QRect();
    animated_ = false;

    QFile metadata(metadataPath());
    if (metadata.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(metadata.readAll()).object();
        const QString name = object.value("file").toString();
        const QString candidate = directory() + QLatin1Char('/') + name;
        if (!name.isEmpty() && QFile::exists(candidate)) {
            file_ = candidate;
            animated_ = object.value("animated").toBool(false);
            const QJsonObject rect = object.value("crop").toObject();
            crop_ = QRect(rect.value("x").toInt(), rect.value("y").toInt(),
                          rect.value("width").toInt(), rect.value("height").toInt());
        }
        return;
    }

    const QString still = directory() + QLatin1Char('/') + kind_ + QLatin1String(".png");
    if (QFile::exists(still))
        file_ = still;
}

bool ImageStore::hasImage() const { return !file_.isEmpty(); }
bool ImageStore::isAnimated() const { return animated_; }
QString ImageStore::filePath() const { return file_; }
QRect ImageStore::cropRect() const { return crop_; }

// Removes one stored file, stepping aside if something outside Meeru still
// holds it: renaming an unlocked-but-busy file at least frees the name so the
// replacement can be written, and the stale copy is cleaned up next time.
bool ImageStore::discard(const QString &path)
{
    if (!QFile::exists(path))
        return true;
    if (QFile::remove(path))
        return true;

    const QString parked = path + QLatin1String(".old-")
                         + QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
    if (QFile::rename(path, parked)) {
        QFile::remove(parked);
        return true;
    }
    return false;
}

bool ImageStore::clearFiles(QString *error)
{
    // Sweep any copy left parked by an earlier replacement.
    QDir folder(directory());
    const QStringList stale = folder.entryList(QStringList(kind_ + QLatin1String(".*.old-*")), QDir::Files);
    for (int i = 0; i < stale.size(); ++i)
        QFile::remove(folder.filePath(stale.at(i)));

    bool ok = true;
    if (!discard(directory() + QLatin1Char('/') + kind_ + QLatin1String(".png")))
        ok = false;
    if (!discard(directory() + QLatin1Char('/') + kind_ + QLatin1String(".gif")))
        ok = false;

    if (!ok && error) {
        *error = QString::fromLatin1(
            "Windows is holding the current picture file open, so Meeru could not replace it. "
            "Close any program previewing your Meeru folder and try again.");
    }
    return ok;
}

bool ImageStore::saveStill(const QImage &cropped, QString *error)
{
    if (cropped.isNull()) {
        if (error)
            *error = QString::fromLatin1("The selected picture could not be read");
        return false;
    }
    if (!QDir().mkpath(directory())) {
        if (error)
            *error = QString::fromLatin1("Cannot create the identity folder");
        return false;
    }
    if (!clearFiles(error))
        return false;

    const QSize target = storedSize();
    QImage scaled = cropped;
    if (scaled.size() != target)
        scaled = scaled.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QString file = directory() + QLatin1Char('/') + kind_ + QLatin1String(".png");
    if (!scaled.save(file, "PNG")) {
        if (error)
            *error = QString::fromLatin1("Cannot write the picture");
        return false;
    }

    QJsonObject object;
    object.insert("formatVersion", 1);
    object.insert("file", kind_ + QLatin1String(".png"));
    object.insert("animated", false);

    QSaveFile metadata(metadataPath());
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(payload) != payload.size() || !metadata.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot write the picture settings");
        return false;
    }

    reload();
    return true;
}

bool ImageStore::saveAnimated(const QString &sourceGifPath, const QRect &crop, QString *error)
{
    if (crop.width() <= 0 || crop.height() <= 0) {
        if (error)
            *error = QString::fromLatin1("The crop area is empty");
        return false;
    }
    if (!QDir().mkpath(directory())) {
        if (error)
            *error = QString::fromLatin1("Cannot create the identity folder");
        return false;
    }
    if (!clearFiles(error))
        return false;

    const QString file = directory() + QLatin1Char('/') + kind_ + QLatin1String(".gif");
    if (!QFile::copy(sourceGifPath, file)) {
        if (error)
            *error = QString::fromLatin1("Cannot copy the animation into your Meeru folder");
        return false;
    }

    QJsonObject rect;
    rect.insert("x", crop.x());
    rect.insert("y", crop.y());
    rect.insert("width", crop.width());
    rect.insert("height", crop.height());

    QJsonObject object;
    object.insert("formatVersion", 1);
    object.insert("file", kind_ + QLatin1String(".gif"));
    object.insert("animated", true);
    object.insert("crop", rect);

    QSaveFile metadata(metadataPath());
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(payload) != payload.size() || !metadata.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot write the picture settings");
        return false;
    }

    reload();
    return true;
}

bool ImageStore::saveReceived(const QByteArray &bytes, bool animated, const QRect &crop, QString *error)
{
    if (bytes.isEmpty()) {
        if (error)
            *error = QString::fromLatin1("The picture that arrived was empty");
        return false;
    }
    if (!QDir().mkpath(directory())) {
        if (error)
            *error = QString::fromLatin1("Cannot create the folder for that contact");
        return false;
    }
    if (!clearFiles(error))
        return false;

    const QString suffix = animated ? QLatin1String(".gif") : QLatin1String(".png");
    const QString file = directory() + QLatin1Char('/') + kind_ + suffix;

    QSaveFile picture(file);
    if (!picture.open(QIODevice::WriteOnly) || picture.write(bytes) != bytes.size() || !picture.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot store that picture");
        return false;
    }

    // Anything that does not load is discarded rather than kept around: a
    // contact should not be able to leave arbitrary files in our folder.
    QImageReader reader(file);
    if (!reader.canRead()) {
        QFile::remove(file);
        if (error)
            *error = QString::fromLatin1("That picture could not be read");
        return false;
    }

    QJsonObject object;
    object.insert("formatVersion", 1);
    object.insert("file", kind_ + suffix);
    object.insert("animated", animated);
    if (animated) {
        QJsonObject rect;
        rect.insert("x", crop.x());
        rect.insert("y", crop.y());
        rect.insert("width", crop.width());
        rect.insert("height", crop.height());
        object.insert("crop", rect);
    }

    QSaveFile metadata(metadataPath());
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(payload) != payload.size() || !metadata.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot store that picture");
        return false;
    }

    reload();
    return true;
}

bool ImageStore::removeImage(QString *error)
{
    if (!clearFiles(error))
        return false;
    QFile::remove(metadataPath());
    reload();
    return true;
}

// ---------------------------------------------------------------- AvatarFrame

AvatarFrame::AvatarFrame(QWidget *parent)
    : QWidget(parent),
      tileSize_(55),
      margin_(3.0),
      radius_(10.0),
      stroke_(3.0),
      color_(Presence::color(Presence::Available)),
      movie_(0),
      pressed_(false),
      interactive_(false),
      animation_(0)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    setTileSize(tileSize_);
    animation_ = new QPropertyAnimation(this, "presenceColor", this);
    animation_->setDuration(400);
    animation_->setEasingCurve(QEasingCurve::InOutQuad);
}

AvatarFrame::~AvatarFrame()
{
    if (movie_)
        movie_->stop();
}

void AvatarFrame::setTileSize(int size)
{
    tileSize_ = qMax(16, size);
    margin_ = qMax(qreal(2.0), tileSize_ * 0.055);
    stroke_ = qMax(qreal(2.0), tileSize_ * 0.055);
    radius_ = qMax(qreal(4.0), tileSize_ * 0.20);

    const int outer = tileSize_ + static_cast<int>(margin_ * 2.0) + 8;
    setFixedSize(outer, outer);
    rebuildStill();
    update();
}

int AvatarFrame::pictureInset() const
{
    const QRectF inner = innerRect();
    return qRound(inner.left());
}

void AvatarFrame::setInteractive(bool interactive)
{
    interactive_ = interactive;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void AvatarFrame::setInitials(const QString &initials)
{
    if (initials_ == initials)
        return;
    initials_ = initials;
    rebuildStill();
    update();
}

void AvatarFrame::setPresenceColor(const QColor &color, bool animated)
{
    if (!animated || !animation_) {
        setPresenceColorImmediate(color);
        return;
    }
    animation_->stop();
    animation_->setStartValue(color_);
    animation_->setEndValue(color);
    animation_->start();
}

void AvatarFrame::setPresenceColorImmediate(const QColor &color)
{
    color_ = color;
    update();
}

void AvatarFrame::clearImage()
{
    if (movie_) {
        movie_->stop();
        delete movie_;
        movie_ = 0;
    }
    source_ = QImage();
    crop_ = QRect();
    rebuildStill();
    update();
}

void AvatarFrame::setImage(const ImageStore &store)
{
    clearImage();
    if (!store.hasImage())
        return;

    if (store.isAnimated()) {
        QMovie *movie = MeeruImage::bufferedMovie(store.filePath(), this);
        if (movie) {
            crop_ = store.cropRect();
            movie_ = movie;
            movie_->setCacheMode(QMovie::CacheNone);
            connect(movie_, SIGNAL(frameChanged(int)), this, SLOT(onMovieFrame()));
            movie_->start();
            return;
        }
    }

    QImageReader reader(store.filePath());
    reader.setAutoTransform(true);
    source_ = reader.read();
    rebuildStill();
    update();
}

void AvatarFrame::onMovieFrame()
{
    update();
}

void AvatarFrame::rebuildStill()
{
    QSize tile = innerRect().size().toSize();
    if (tile.isEmpty())
        tile = QSize(tileSize_, tileSize_);

    if (!source_.isNull())
        still_ = MeeruPaint::roundedFromPixmap(QPixmap::fromImage(source_), tile, innerRadius());
    else
        still_ = MeeruPaint::initialsTile(initials_.isEmpty() ? QString::fromLatin1("?") : initials_,
                                          tile, innerRadius());
}

QRectF AvatarFrame::frameRect() const
{
    const qreal side = tileSize_ + stroke_ + margin_;
    const qreal left = (width() - side) / 2.0;
    const qreal top = (height() - side) / 2.0;
    return QRectF(left, top, side, side);
}

QRectF AvatarFrame::innerRect() const
{
    const QRectF outer = frameRect();
    const qreal halfStroke = stroke_ * 0.5;
    return outer.adjusted(halfStroke, halfStroke, -halfStroke, -halfStroke);
}

qreal AvatarFrame::innerRadius() const
{
    return qMax(qreal(0.0), radius_ - stroke_ * 0.5);
}

void AvatarFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Glow first, then the ring, then the picture on top: the same order the
    // login window gets for free by keeping its picture in a child widget.
    MeeruPaint::drawPresenceGlow(painter, QRectF(0, 0, width(), height()), color_);
    MeeruPaint::drawPresenceRing(painter, frameRect(), color_, radius_, stroke_);

    const QRectF inner = innerRect();
    const QSize innerSize = inner.size().toSize();

    QPixmap picture;
    if (movie_) {
        const QPixmap cropped = cropFrame(movie_->currentPixmap(), crop_);
        if (!cropped.isNull())
            picture = MeeruPaint::roundedFromPixmap(cropped, innerSize, innerRadius());
    }
    if (picture.isNull()) {
        if (still_.size() != innerSize)
            picture = MeeruPaint::roundedFromPixmap(still_, innerSize, innerRadius());
        else
            picture = still_;
    }

    painter.drawPixmap(inner.topLeft(), picture);
}

void AvatarFrame::mousePressEvent(QMouseEvent *event)
{
    if (interactive_ && event->button() == Qt::LeftButton) {
        pressed_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AvatarFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (interactive_ && pressed_ && event->button() == Qt::LeftButton) {
        pressed_ = false;
        if (rect().contains(event->pos()))
            emit clicked();
        event->accept();
        return;
    }
    pressed_ = false;
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------- BannerFrame

BannerFrame::BannerFrame(QWidget *parent)
    : QWidget(parent), movie_(0), pressed_(false)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(QString::fromLatin1("Click to choose a banner"));
}

BannerFrame::~BannerFrame()
{
    if (movie_)
        movie_->stop();
}

void BannerFrame::clearImage()
{
    if (movie_) {
        movie_->stop();
        delete movie_;
        movie_ = 0;
    }
    source_ = QImage();
    crop_ = QRect();
    update();
}

void BannerFrame::setImage(const ImageStore &store)
{
    clearImage();
    if (!store.hasImage())
        return;

    if (store.isAnimated()) {
        QMovie *movie = MeeruImage::bufferedMovie(store.filePath(), this);
        if (movie) {
            crop_ = store.cropRect();
            movie_ = movie;
            movie_->setCacheMode(QMovie::CacheNone);
            connect(movie_, SIGNAL(frameChanged(int)), this, SLOT(onMovieFrame()));
            movie_->start();
            return;
        }
    }

    QImageReader reader(store.filePath());
    reader.setAutoTransform(true);
    source_ = reader.read();
    update();
}

void BannerFrame::onMovieFrame()
{
    update();
}

void BannerFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPixmap picture;
    if (movie_)
        picture = cropFrame(movie_->currentPixmap(), crop_);
    else if (!source_.isNull())
        picture = QPixmap::fromImage(source_);

    if (!picture.isNull()) {
        QPixmap scaled = picture.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect area((scaled.width() - width()) / 2, (scaled.height() - height()) / 2, width(), height());
        painter.drawPixmap(rect(), scaled, area);
        // Keep the text readable whatever the user picked.
        QLinearGradient shade(0, 0, 0, height());
        shade.setColorAt(0.0, QColor(18, 11, 24, 90));
        shade.setColorAt(1.0, QColor(18, 11, 24, 175));
        painter.fillRect(rect(), shade);
        return;
    }

    QLinearGradient gradient(0, 0, width(), height());
    gradient.setColorAt(0.0, QColor(0x4d, 0x34, 0x5c));
    gradient.setColorAt(0.52, QColor(0x70, 0x50, 0x83));
    gradient.setColorAt(1.0, QColor(0x8e, 0x56, 0x6c));
    painter.fillRect(rect(), gradient);
}

void BannerFrame::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        pressed_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void BannerFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (pressed_ && event->button() == Qt::LeftButton) {
        pressed_ = false;
        if (rect().contains(event->pos()))
            emit clicked();
        event->accept();
        return;
    }
    pressed_ = false;
    QWidget::mouseReleaseEvent(event);
}
