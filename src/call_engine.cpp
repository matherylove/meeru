#include "call_engine.h"

#include <QApplication>
#include <QBuffer>
#include <QDesktopWidget>
#include <QPixmap>
#include <QDateTime>
#include <QTimer>

#include "platform_support.h"

#ifdef MEERU_HAS_AUDIO
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#endif

namespace {

const int kSampleRate = 16000;
const int kChannels = 1;
const int kBitsPerSample = 16;

const int kVideoWidth = 320;
const int kVideoHeight = 240;
const int kVideoQuality = 60;
const int kScreenQuality = 55;
const int kMaxFrameBytes = 512 * 1024;

QByteArray encodeFrame(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", quality);
    return bytes;
}

}

bool CallEngine::audioAvailable()
{
#ifdef MEERU_HAS_AUDIO
    return !QAudioDeviceInfo::availableDevices(QAudio::AudioInput).isEmpty()
        && !QAudioDeviceInfo::availableDevices(QAudio::AudioOutput).isEmpty();
#else
    return false;
#endif
}

bool CallEngine::cameraAvailable()
{
    return CameraSource::isAvailable();
}

QString CallEngine::hardwareNote()
{
    return Platform::capabilitySummary();
}

CallEngine::CallEngine(QObject *parent)
    : QObject(parent), state_(Idle), videoOn_(false), screenOn_(false), muted_(false),
      offeredVideo_(false), camera_(0), lastCameraFrameMs_(0),
      videoTimer_(0), screenTimer_(0)
#ifdef MEERU_HAS_AUDIO
    , input_(0), output_(0), inputDevice_(0), outputDevice_(0)
#endif
{
    // How hard this machine is asked to work depends on what it is.
    videoTimer_ = new QTimer(this);
    videoTimer_->setInterval(Platform::suggestedFrameInterval());
    connect(videoTimer_, SIGNAL(timeout()), this, SLOT(onVideoTick()));

    screenTimer_ = new QTimer(this);
    screenTimer_->setInterval(Platform::suggestedFrameInterval() * 2);
    connect(screenTimer_, SIGNAL(timeout()), this, SLOT(onScreenTick()));

#ifdef MEERU_HAS_AUDIO
    format_.setSampleRate(kSampleRate);
    format_.setChannelCount(kChannels);
    format_.setSampleSize(kBitsPerSample);
    format_.setCodec(QString::fromLatin1("audio/pcm"));
    format_.setByteOrder(QAudioFormat::LittleEndian);
    format_.setSampleType(QAudioFormat::SignedInt);
#endif
}

CallEngine::~CallEngine()
{
    closeAudio();
}

void CallEngine::setState(State state)
{
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(static_cast<int>(state_));
}

void CallEngine::openAudio()
{
#ifdef MEERU_HAS_AUDIO
    if (input_ || !audioAvailable())
        return;

    const QAudioDeviceInfo inputDevice = QAudioDeviceInfo::defaultInputDevice();
    const QAudioDeviceInfo outputDevice = QAudioDeviceInfo::defaultOutputDevice();

    QAudioFormat format = format_;
    if (!inputDevice.isFormatSupported(format))
        format = inputDevice.nearestFormat(format);
    format_ = format;

    input_ = new QAudioInput(inputDevice, format_, this);
    inputDevice_ = input_->start();
    if (inputDevice_)
        connect(inputDevice_, SIGNAL(readyRead()), this, SLOT(onAudioReadyRead()));

    output_ = new QAudioOutput(outputDevice, format_, this);
    outputDevice_ = output_->start();
#endif
}

void CallEngine::closeAudio()
{
#ifdef MEERU_HAS_AUDIO
    if (input_) {
        input_->stop();
        delete input_;
        input_ = 0;
        inputDevice_ = 0;
    }
    if (output_) {
        output_->stop();
        delete output_;
        output_ = 0;
        outputDevice_ = 0;
    }
#endif
}

void CallEngine::onAudioReadyRead()
{
#ifdef MEERU_HAS_AUDIO
    if (!inputDevice_ || state_ != Active)
        return;

    const QByteArray pcm = inputDevice_->readAll();
    if (pcm.isEmpty())
        return;

    // Muting stops the sound leaving this machine rather than merely hiding
    // it at the far end, which is the only version of muting worth having.
    if (muted_)
        return;

    emit audioReady(participants_, pcm);
#endif
}

void CallEngine::handleAudio(const QString &peerId, const QByteArray &pcm)
{
    Q_UNUSED(peerId);
#ifdef MEERU_HAS_AUDIO
    if (state_ == Active && outputDevice_ && !pcm.isEmpty())
        outputDevice_->write(pcm);
#else
    Q_UNUSED(pcm);
#endif
}

void CallEngine::onVideoTick()
{
    // The camera pushes frames as it has them; this only keeps the rate in
    // check, which the timer already does by gating onCameraFrame.
}

void CallEngine::onCameraFrame(const QImage &image)
{
    if (state_ != Active || !videoOn_ || image.isNull())
        return;

    // A camera hands over far more frames than this can send, so most are
    // dropped rather than queued: a late frame is worth nothing in a call.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastCameraFrameMs_ < Platform::suggestedFrameInterval())
        return;
    lastCameraFrameMs_ = now;

    QImage frame = image.scaled(kVideoWidth, kVideoHeight, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
    if (frame.format() != QImage::Format_RGB888)
        frame = frame.convertToFormat(QImage::Format_RGB888);

    const QByteArray jpeg = encodeFrame(frame, kVideoQuality);
    if (jpeg.isEmpty() || jpeg.size() > kMaxFrameBytes)
        return;

    emit videoReady(participants_, jpeg, SourceCamera);
    emit frameReceived(QString(), frame, SourceCamera);
}

void CallEngine::onScreenTick()
{
    if (state_ != Active || !screenOn_)
        return;

    // GDI grab: the one capture that works everywhere from Windows XP up.
    const QPixmap shot = QPixmap::grabWindow(QApplication::desktop()->winId());
    if (shot.isNull())
        return;

    QImage frame = shot.toImage().scaledToWidth(Platform::suggestedScreenWidth(),
                                               Qt::SmoothTransformation);
    if (frame.format() != QImage::Format_RGB888)
        frame = frame.convertToFormat(QImage::Format_RGB888);

    const QByteArray jpeg = encodeFrame(frame, kScreenQuality);
    if (jpeg.isEmpty() || jpeg.size() > kMaxFrameBytes)
        return;

    emit videoReady(participants_, jpeg, SourceScreen);
    emit frameReceived(QString(), frame, SourceScreen);   // our own preview
}

void CallEngine::handleVideo(const QString &peerId, const QByteArray &jpeg, int source)
{
    if (state_ != Active || jpeg.isEmpty() || jpeg.size() > kMaxFrameBytes)
        return;

    QImage frame;
    if (!frame.loadFromData(jpeg, "JPEG"))
        return;
    emit frameReceived(peerId, frame, source);
}

void CallEngine::startCall(const QString &conversationId, const QStringList &participants, bool withVideo)
{
    if (state_ != Idle && state_ != Ended)
        return;

    conversationId_ = conversationId;
    participants_ = participants;
    offeredVideo_ = withVideo;
    caller_.clear();

    setState(Calling);
    emit signalReady(participants_, QString::fromLatin1("offer"), withVideo);
}

void CallEngine::incomingCall(const QString &conversationId, const QString &fromPeer, bool withVideo)
{
    if (state_ == Active) {
        // Already busy: turn it away rather than leaving the caller waiting.
        QStringList one;
        one.append(fromPeer);
        emit signalReady(one, QString::fromLatin1("busy"), false);
        return;
    }

    conversationId_ = conversationId;
    caller_ = fromPeer;
    participants_.clear();
    participants_.append(fromPeer);
    offeredVideo_ = withVideo;

    setState(Ringing);
    emit ringing(conversationId, fromPeer, withVideo);
}

void CallEngine::answer(bool withVideo)
{
    if (state_ != Ringing)
        return;

    videoOn_ = withVideo && offeredVideo_;
    openAudio();
    setState(Active);
    emit signalReady(participants_, QString::fromLatin1("answer"), videoOn_);
}

void CallEngine::hangUp()
{
    if (state_ == Idle)
        return;

    emit signalReady(participants_, QString::fromLatin1("end"), false);

    screenTimer_->stop();
    videoTimer_->stop();
    if (camera_) {
        camera_->stop();
        delete camera_;
        camera_ = 0;
    }
    closeAudio();

    screenOn_ = false;
    videoOn_ = false;
    muted_ = false;
    participants_.clear();
    conversationId_.clear();
    caller_.clear();
    setState(Ended);
    setState(Idle);
}

void CallEngine::setMuted(bool muted)
{
    muted_ = muted;
}

void CallEngine::setVideoOn(bool on)
{
    videoOn_ = on && cameraAvailable();

    if (!videoOn_) {
        if (camera_) {
            camera_->stop();
            delete camera_;
            camera_ = 0;
        }
        videoTimer_->stop();
        return;
    }

    if (!camera_) {
        camera_ = new CameraSource(this);
        connect(camera_, SIGNAL(frameReady(QImage)), this, SLOT(onCameraFrame(QImage)));
    }

    QString error;
    if (!camera_->start(&error)) {
        videoOn_ = false;
        delete camera_;
        camera_ = 0;
        return;
    }
    videoTimer_->start();
}

void CallEngine::setScreenSharing(bool on)
{
    screenOn_ = on;
    if (screenOn_ && state_ == Active)
        screenTimer_->start();
    else
        screenTimer_->stop();
}

void CallEngine::peerLeft(const QString &peerId)
{
    participants_.removeAll(peerId);
    if (participants_.isEmpty() && state_ != Idle)
        hangUp();
}
