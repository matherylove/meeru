#include "call_engine.h"

#include <QApplication>
#include <QBuffer>
#include <QDesktopWidget>
#include <QPixmap>
#include <QTimer>

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
const int kVideoIntervalMs = 125;      // eight frames a second
const int kScreenWidth = 640;
const int kScreenIntervalMs = 500;     // a screen changes far less than a face
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
    // Deliberately not claimed. Qt 5.6 can list cameras only with the
    // multimedia widgets module present, and on the machines this targets a
    // webcam is the exception. Screen sharing covers the common case.
    return false;
}

QString CallEngine::hardwareNote()
{
    return QString::fromLatin1(
        "Calls use uncompressed sound and JPEG frames. Hardware video encoding is not available on "
        "this kind of machine: it needs Windows 7 or newer, and the software encoders that would "
        "replace it need instructions a Pentium III does not have. On a local network this is "
        "comfortable; over anything slower it will not be.");
}

CallEngine::CallEngine(QObject *parent)
    : QObject(parent), state_(Idle), videoOn_(false), screenOn_(false), muted_(false),
      offeredVideo_(false), videoTimer_(0), screenTimer_(0)
#ifdef MEERU_HAS_AUDIO
    , input_(0), output_(0), inputDevice_(0), outputDevice_(0)
#endif
{
    videoTimer_ = new QTimer(this);
    videoTimer_->setInterval(kVideoIntervalMs);
    connect(videoTimer_, SIGNAL(timeout()), this, SLOT(onVideoTick()));

    screenTimer_ = new QTimer(this);
    screenTimer_->setInterval(kScreenIntervalMs);
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
    // Reserved for a camera. Nothing is sent while there is nothing to send,
    // and the interface says as much rather than showing a black rectangle.
}

void CallEngine::onScreenTick()
{
    if (state_ != Active || !screenOn_)
        return;

    // GDI grab: the one capture that works everywhere from Windows XP up.
    const QPixmap shot = QPixmap::grabWindow(QApplication::desktop()->winId());
    if (shot.isNull())
        return;

    QImage frame = shot.toImage().scaledToWidth(kScreenWidth, Qt::SmoothTransformation);
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
    if (videoOn_)
        videoTimer_->start();
    else
        videoTimer_->stop();
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
