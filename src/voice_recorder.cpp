#include "voice_recorder.h"

#include <QDataStream>
#include <QFile>
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


// ------------------------------------------------------------- VoicePlayer

VoicePlayer *VoicePlayer::instance()
{
    // One at a time: starting a second note stops the first, which is what
    // anybody clicking two of them in a row expects.
    static VoicePlayer *shared = new VoicePlayer();
    return shared;
}

VoicePlayer::VoicePlayer(QObject *parent)
    : QObject(parent), file_(0)
#ifdef MEERU_HAS_AUDIO
    , output_(0)
#endif
{
}

bool VoicePlayer::isPlaying() const
{
#ifdef MEERU_HAS_AUDIO
    return output_ != 0;
#else
    return false;
#endif
}

void VoicePlayer::stop()
{
#ifdef MEERU_HAS_AUDIO
    if (output_) {
        output_->stop();
        delete output_;
        output_ = 0;
    }
#endif
    if (file_) {
        file_->close();
        delete file_;
        file_ = 0;
    }
}

bool VoicePlayer::play(const QString &path, QString *error)
{
#ifdef MEERU_HAS_AUDIO
    stop();

    if (!path.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive)) {
        if (error) {
            *error = QString::fromLatin1("Meeru plays its own voice notes; anything else opens in "
                                         "your usual player.");
        }
        return false;
    }

    file_ = new QFile(path, this);
    if (!file_->open(QIODevice::ReadOnly)) {
        delete file_;
        file_ = 0;
        if (error)
            *error = QString::fromLatin1("That recording could not be opened.");
        return false;
    }

    // The 44 byte header tells us how it was recorded; the rest is the sound.
    const QByteArray header = file_->read(44);
    if (header.size() != 44 || !header.startsWith("RIFF")) {
        stop();
        if (error)
            *error = QString::fromLatin1("That file is not a recording Meeru made.");
        return false;
    }

    QDataStream reader(header.mid(22, 16));
    reader.setByteOrder(QDataStream::LittleEndian);
    quint16 channels = 1;
    quint32 sampleRate = 16000;
    quint32 byteRate = 0;
    quint16 blockAlign = 0;
    quint16 bits = 16;
    reader >> channels >> sampleRate >> byteRate >> blockAlign >> bits;

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(sampleRate));
    format.setChannelCount(channels);
    format.setSampleSize(bits);
    format.setCodec(QString::fromLatin1("audio/pcm"));
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    const QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    if (device.isNull() || !device.isFormatSupported(format)) {
        if (device.isNull()) {
            stop();
            if (error)
                *error = QString::fromLatin1("This computer has nothing to play sound through.");
            return false;
        }
        format = device.nearestFormat(format);
    }

    // Deliberately not connected to anything that deletes this: the player is
    // shared, and the next note to be played stops the previous one.
    output_ = new QAudioOutput(device, format, this);
    output_->start(file_);
    return true;
#else
    Q_UNUSED(path);
    if (error)
        *error = QString::fromLatin1("This build of Meeru has no audio support.");
    return false;
#endif
}
