#include "voice_recorder.h"

#include <QDataStream>
#include <QFile>
#include <QTimer>

#ifdef MEERU_HAS_AUDIO
#include <QAudioDeviceInfo>
#include <QAudioInput>
#endif

namespace {
const int kSampleRate = 16000;
const int kChannels = 1;
const int kBitsPerSample = 16;
const int kMaximumSeconds = 300;
}

bool VoiceRecorder::isSupported()
{
#ifdef MEERU_HAS_AUDIO
    return !QAudioDeviceInfo::availableDevices(QAudio::AudioInput).isEmpty();
#else
    return false;
#endif
}

int VoiceRecorder::maximumSeconds()
{
    return kMaximumSeconds;
}

VoiceRecorder::VoiceRecorder(QObject *parent)
    : QObject(parent), file_(0), recording_(false), seconds_(0), timer_(0)
#ifdef MEERU_HAS_AUDIO
    , input_(0)
#endif
{
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, SIGNAL(timeout()), this, SLOT(onTick()));

#ifdef MEERU_HAS_AUDIO
    format_.setSampleRate(kSampleRate);
    format_.setChannelCount(kChannels);
    format_.setSampleSize(kBitsPerSample);
    format_.setCodec(QString::fromLatin1("audio/pcm"));
    format_.setByteOrder(QAudioFormat::LittleEndian);
    format_.setSampleType(QAudioFormat::SignedInt);
#endif
}

VoiceRecorder::~VoiceRecorder()
{
    cancel();
}

bool VoiceRecorder::isRecording() const
{
    return recording_;
}

int VoiceRecorder::elapsedSeconds() const
{
    return seconds_;
}

bool VoiceRecorder::start(const QString &targetPath, QString *error)
{
#ifdef MEERU_HAS_AUDIO
    if (recording_)
        return false;

    const QAudioDeviceInfo device = QAudioDeviceInfo::defaultInputDevice();
    if (device.isNull()) {
        if (error)
            *error = QString::fromLatin1("No microphone was found on this computer.");
        return false;
    }

    QAudioFormat format = format_;
    if (!device.isFormatSupported(format)) {
        // Rather than give up, take the closest thing the device will accept.
        format = device.nearestFormat(format);
    }
    format_ = format;

    file_ = new QFile(targetPath, this);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file_;
        file_ = 0;
        if (error)
            *error = QString::fromLatin1("Cannot write the recording.");
        return false;
    }

    // Space for the header; the sizes are only known once recording stops.
    file_->write(QByteArray(44, '\0'));

    input_ = new QAudioInput(device, format_, this);
    input_->start(file_);

    path_ = targetPath;
    seconds_ = 0;
    recording_ = true;
    timer_->start();
    return true;
#else
    Q_UNUSED(targetPath);
    if (error) {
        *error = QString::fromLatin1("This build of Meeru has no audio support, so voice notes "
                                     "cannot be recorded here.");
    }
    return false;
#endif
}

void VoiceRecorder::onTick()
{
    if (!recording_)
        return;
    ++seconds_;
    emit levelChanged(seconds_);
    if (seconds_ >= kMaximumSeconds)
        stop();
}

void VoiceRecorder::writeWavHeader(qint64 dataBytes)
{
    if (!file_)
        return;

    int sampleRate = kSampleRate;
    int channels = kChannels;
    int bits = kBitsPerSample;
#ifdef MEERU_HAS_AUDIO
    sampleRate = format_.sampleRate();
    channels = format_.channelCount();
    bits = format_.sampleSize();
#endif

    const int byteRate = sampleRate * channels * bits / 8;
    const int blockAlign = channels * bits / 8;

    file_->seek(0);
    QDataStream out(file_);
    out.setByteOrder(QDataStream::LittleEndian);

    file_->write("RIFF", 4);
    out << static_cast<quint32>(36 + dataBytes);
    file_->write("WAVEfmt ", 8);
    out << static_cast<quint32>(16);
    out << static_cast<quint16>(1);                     // PCM
    out << static_cast<quint16>(channels);
    out << static_cast<quint32>(sampleRate);
    out << static_cast<quint32>(byteRate);
    out << static_cast<quint16>(blockAlign);
    out << static_cast<quint16>(bits);
    file_->write("data", 4);
    out << static_cast<quint32>(dataBytes);
}

QString VoiceRecorder::stop()
{
    if (!recording_)
        return QString();

    recording_ = false;
    timer_->stop();

#ifdef MEERU_HAS_AUDIO
    if (input_) {
        input_->stop();
        delete input_;
        input_ = 0;
    }
#endif

    QString finished;
    if (file_) {
        const qint64 dataBytes = qMax(Q_INT64_C(0), file_->size() - 44);
        writeWavHeader(dataBytes);
        file_->close();
        finished = dataBytes > 0 ? path_ : QString();
        if (dataBytes <= 0)
            file_->remove();
        delete file_;
        file_ = 0;
    }

    path_.clear();
    return finished;
}

void VoiceRecorder::cancel()
{
    if (!recording_ && !file_)
        return;

    recording_ = false;
    if (timer_)
        timer_->stop();

#ifdef MEERU_HAS_AUDIO
    if (input_) {
        input_->stop();
        delete input_;
        input_ = 0;
    }
#endif

    if (file_) {
        file_->close();
        file_->remove();
        delete file_;
        file_ = 0;
    }
    path_.clear();
}
