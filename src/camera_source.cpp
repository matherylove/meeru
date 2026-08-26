#include "camera_source.h"

#ifdef MEERU_HAS_AUDIO
#include <QCamera>
#include <QCameraInfo>
#include <QVideoFrame>
#include <QVideoSurfaceFormat>
#endif

#ifdef MEERU_HAS_AUDIO

CameraSurface::CameraSurface(QObject *parent)
    : QAbstractVideoSurface(parent)
{
}

QList<QVideoFrame::PixelFormat> CameraSurface::supportedPixelFormats(
    QAbstractVideoBuffer::HandleType type) const
{
    QList<QVideoFrame::PixelFormat> formats;
    if (type != QAbstractVideoBuffer::NoHandle)
        return formats;

    // Everything Qt knows how to turn into a QImage on its own. Accepting the
    // widest set here is what lets one piece of code work with both the old
    // DirectShow backend and the modern one.
    formats << QVideoFrame::Format_RGB32
            << QVideoFrame::Format_ARGB32
            << QVideoFrame::Format_ARGB32_Premultiplied
            << QVideoFrame::Format_RGB24
            << QVideoFrame::Format_RGB565
            << QVideoFrame::Format_RGB555;
    return formats;
}

bool CameraSurface::present(const QVideoFrame &frame)
{
    QVideoFrame copy(frame);
    if (!copy.map(QAbstractVideoBuffer::ReadOnly))
        return false;

    const QImage::Format format = QVideoFrame::imageFormatFromPixelFormat(copy.pixelFormat());
    if (format == QImage::Format_Invalid) {
        copy.unmap();
        return false;
    }

    // Copied deliberately: the frame's memory belongs to the camera and is
    // reused the moment this returns.
    const QImage image(copy.bits(), copy.width(), copy.height(), copy.bytesPerLine(), format);
    emit frameReady(image.copy());

    copy.unmap();
    return true;
}

#endif

CameraSource::CameraSource(QObject *parent)
    : QObject(parent), running_(false)
#ifdef MEERU_HAS_AUDIO
    , camera_(0), surface_(0)
#endif
{
}

CameraSource::~CameraSource()
{
    stop();
}

bool CameraSource::isAvailable()
{
#ifdef MEERU_HAS_AUDIO
    return !QCameraInfo::availableCameras().isEmpty();
#else
    return false;
#endif
}

QStringList CameraSource::deviceNames()
{
    QStringList names;
#ifdef MEERU_HAS_AUDIO
    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    for (int i = 0; i < cameras.size(); ++i)
        names.append(cameras.at(i).description());
#endif
    return names;
}

bool CameraSource::start(QString *error)
{
#ifdef MEERU_HAS_AUDIO
    if (running_)
        return true;

    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    if (cameras.isEmpty()) {
        if (error) {
            *error = QString::fromLatin1("No camera was found. If one is plugged in and Windows can "
                                         "see it, this build of Qt may be missing its camera plugin.");
        }
        return false;
    }

    surface_ = new CameraSurface(this);
    connect(surface_, SIGNAL(frameReady(QImage)), this, SIGNAL(frameReady(QImage)));

    camera_ = new QCamera(cameras.first(), this);
    camera_->setViewfinder(surface_);
    camera_->setCaptureMode(QCamera::CaptureViewfinder);
    camera_->start();

    if (camera_->error() != QCamera::NoError) {
        if (error)
            *error = camera_->errorString();
        stop();
        return false;
    }

    running_ = true;
    return true;
#else
    if (error)
        *error = QString::fromLatin1("This build of Meeru was made without multimedia support.");
    return false;
#endif
}

void CameraSource::stop()
{
#ifdef MEERU_HAS_AUDIO
    if (camera_) {
        camera_->stop();
        delete camera_;
        camera_ = 0;
    }
    if (surface_) {
        delete surface_;
        surface_ = 0;
    }
#endif
    running_ = false;
}
