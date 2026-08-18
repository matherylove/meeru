#include "media_window.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMovie>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "avatar.h"
#include "crop_dialog.h"
#include "meeru_style.h"
#include "meeru_window.h"

namespace {
const int kWindowWidth = 460;
const int kStageHeight = 300;
const int kDockGap = 6;
}

MediaWindow::MediaWindow(const LocalProfile &profile,
                         const MeeruPaths &paths,
                         const Chat::Message &message,
                         QWidget *anchor,
                         QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), paths_(paths), message_(message), anchor_(anchor),
      pinned_(true), titleBar_(0), stage_(0), caption_(0), movie_(0)
{
    setWindowTitle(message_.attachment.fileName);
    setStyleSheet(MeeruStyle::sheet());
    setFixedWidth(kWindowWidth);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("meeruRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(root);

    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    titleBar_ = new MeeruTitleBar(message_.attachment.fileName, true, false, root);
    titleBar_->addPinButton(true);
    connect(titleBar_, SIGNAL(pinToggled(bool)), this, SLOT(onPinToggled(bool)));
    layout->addWidget(titleBar_);

    stage_ = new QLabel(root);
    stage_->setObjectName(QString::fromLatin1("mediaStage"));
    stage_->setAlignment(Qt::AlignCenter);
    stage_->setFixedHeight(kStageHeight);
    stage_->setAttribute(Qt::WA_StyledBackground, true);
    layout->addWidget(stage_);

    caption_ = new QLabel(root);
    caption_->setObjectName(QString::fromLatin1("dialogLabel"));
    caption_->setWordWrap(true);
    caption_->setContentsMargins(12, 8, 12, 4);
    layout->addWidget(caption_);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->setContentsMargins(12, 4, 12, 10);
    actions->setSpacing(6);

    QPushButton *save = new QPushButton(QString::fromLatin1("Save a copy"), root);
    QPushButton *avatar = new QPushButton(QString::fromLatin1("Set as picture"), root);
    QPushButton *banner = new QPushButton(QString::fromLatin1("Set as banner"), root);
    QPushButton *open = new QPushButton(QString::fromLatin1("Open"), root);
    open->setObjectName(QString::fromLatin1("primaryButton"));

    const bool picture = message_.attachment.media == Chat::MediaImage
                      || message_.attachment.media == Chat::MediaAnimation;
    avatar->setEnabled(picture);
    banner->setEnabled(picture);

    actions->addWidget(save);
    actions->addWidget(avatar);
    actions->addWidget(banner);
    actions->addStretch();
    actions->addWidget(open);
    layout->addLayout(actions);

    connect(save, SIGNAL(clicked()), this, SLOT(onSaveCopy()));
    connect(avatar, SIGNAL(clicked()), this, SLOT(onSetAvatar()));
    connect(banner, SIGNAL(clicked()), this, SLOT(onSetBanner()));
    connect(open, SIGNAL(clicked()), this, SLOT(onOpenExternally()));

    showContent();
    adjustSize();
    followAnchor();
}

MediaWindow::~MediaWindow()
{
    if (movie_)
        movie_->stop();
}

void MediaWindow::showContent()
{
    const QString path = message_.attachment.localPath;
    const QString size = message_.attachment.fileSize > 1024 * 1024
        ? QString::fromLatin1("%1 MB").arg(message_.attachment.fileSize / (1024.0 * 1024.0), 0, 'f', 1)
        : QString::fromLatin1("%1 KB").arg(qMax(qint64(1), message_.attachment.fileSize / 1024));

    caption_->setText(QString::fromLatin1("%1  -  %2  -  sent %3")
                          .arg(message_.attachment.fileName)
                          .arg(size)
                          .arg(message_.sentAtUtc.toLocalTime()
                                   .toString(QString::fromLatin1("d MMM yyyy, h:mm AP"))));

    if (message_.attachment.media == Chat::MediaAnimation) {
        movie_ = MeeruImage::bufferedMovie(path, this);
        if (movie_) {
            movie_->setScaledSize(QSize(kWindowWidth - 20, kStageHeight - 10));
            stage_->setMovie(movie_);
            movie_->start();
            return;
        }
    }

    if (message_.attachment.media == Chat::MediaImage) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            stage_->setPixmap(QPixmap::fromImage(image).scaled(
                kWindowWidth - 20, kStageHeight - 10, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }
    }

    // Video and sound are handed to whatever the system already plays them
    // with: Meeru has no decoder of its own, and pretending otherwise would
    // only produce a black rectangle.
    stage_->setText(message_.attachment.media == Chat::MediaVideo
        ? QString::fromLatin1("Video\n\nPress Open to play it in your usual player.")
        : (message_.attachment.media == Chat::MediaAudio
               ? QString::fromLatin1("Sound\n\nPress Open to play it.")
               : QString::fromLatin1("No preview for this kind of file.")));
}

void MediaWindow::onSaveCopy()
{
    const QString target = QFileDialog::getSaveFileName(
        this, QString::fromLatin1("Save a copy"),
        QDir::homePath() + QLatin1Char('/') + message_.attachment.fileName);
    if (target.isEmpty())
        return;

    QFile::remove(target);
    QFile::copy(message_.attachment.localPath, target);
}

bool MediaWindow::applyAsPicture(const QString &kind)
{
    ImageStore store(paths_, profile_.identityId, kind);

    CropDialog crop(message_.attachment.localPath, store.aspect(),
                    kind == QLatin1String("banner") ? QString::fromLatin1("Crop your banner")
                                                    : QString::fromLatin1("Crop your picture"),
                    this);
    if (!crop.isReady() || crop.exec() != QDialog::Accepted)
        return false;

    QString error;
    const bool ok = crop.isAnimated()
        ? store.saveAnimated(message_.attachment.localPath, crop.cropRect(), &error)
        : store.saveStill(crop.croppedStill(), &error);

    if (ok)
        emit pictureChanged();
    return ok;
}

void MediaWindow::onSetAvatar()
{
    applyAsPicture(QString::fromLatin1("avatar"));
}

void MediaWindow::onSetBanner()
{
    applyAsPicture(QString::fromLatin1("banner"));
}

void MediaWindow::onOpenExternally()
{
    if (!message_.attachment.localPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(message_.attachment.localPath));
}

void MediaWindow::onPinToggled(bool pinned)
{
    pinned_ = pinned;
    if (pinned_)
        followAnchor();
}

void MediaWindow::followAnchor()
{
    if (!pinned_ || !anchor_ || !anchor_->isVisible())
        return;
    const QRect frame = anchor_->frameGeometry();
    move(frame.right() + kDockGap, frame.top());
}

void MediaWindow::closeEvent(QCloseEvent *event)
{
    if (movie_)
        movie_->stop();
    emit closed();
    event->accept();
}
