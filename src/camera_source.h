#ifndef MEERU_CAMERA_SOURCE_H
#define MEERU_CAMERA_SOURCE_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#ifdef MEERU_HAS_AUDIO
#include <QAbstractVideoSurface>
class QCamera;
#endif

// Frames from a webcam, on every Windows Meeru runs on.
//
// The usual way to show a camera in Qt is QCameraViewfinder, which lives in the
// multimedia widgets module and only hands you a picture on screen, not the
// frames themselves. Neither is any use here: the frames have to be encoded and
// sent, and that module is often missing from a static build.
//
// So the camera is pointed at a surface of our own instead. Qt hands each frame
// straight to it, the widgets module is never needed, and the backend
// underneath is whatever the system has: DirectShow on Windows XP, Media
// Foundation from Vista on. The same code covers both.
#ifdef MEERU_HAS_AUDIO

class CameraSurface : public QAbstractVideoSurface
{
    Q_OBJECT

public:
    explicit CameraSurface(QObject *parent = 0);

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(
        QAbstractVideoBuffer::HandleType type = QAbstractVideoBuffer::NoHandle) const;
    bool present(const QVideoFrame &frame);

signals:
    void frameReady(const QImage &image);
};

#endif

class CameraSource : public QObject
{
    Q_OBJECT

public:
    explicit CameraSource(QObject *parent = 0);
    ~CameraSource();

    static bool isAvailable();
    static QStringList deviceNames();

    bool start(QString *error = 0);
    void stop();
    bool isRunning() const { return running_; }

signals:
    void frameReady(const QImage &image);

private:
    bool running_;
#ifdef MEERU_HAS_AUDIO
    QCamera *camera_;
    CameraSurface *surface_;
#endif
};

#endif
