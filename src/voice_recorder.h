#ifndef MEERU_VOICE_RECORDER_H
#define MEERU_VOICE_RECORDER_H

#include <QObject>
#include <QString>

#ifdef MEERU_HAS_AUDIO
#include <QAudioFormat>
class QAudioInput;
#endif

class QFile;

// Records a voice note straight to an uncompressed WAV file.
//
// Uncompressed on purpose: a compressed format would mean pulling in a codec,
// and on the machines Meeru targets the audio backends that can encode are the
// ones least likely to be present. Mono at 16 kHz costs about two megabytes a
// minute, which is nothing next to the pictures people already send, and it
// plays on every version of Windows without installing anything.
//
// The whole class is behind MEERU_HAS_AUDIO. If the Qt build has no multimedia
// module the program still compiles and simply reports that recording is not
// available, rather than failing to link.
class VoiceRecorder : public QObject
{
    Q_OBJECT

public:
    explicit VoiceRecorder(QObject *parent = 0);
    ~VoiceRecorder();

    static bool isSupported();
    static int maximumSeconds();

    bool start(const QString &targetPath, QString *error = 0);
    QString stop();            // returns the finished file, or empty on failure
    void cancel();
    bool isRecording() const;
    int elapsedSeconds() const;

signals:
    void levelChanged(int seconds);

private slots:
    void onTick();

private:
    void writeWavHeader(qint64 dataBytes);

    QFile *file_;
    QString path_;
    bool recording_;
    int seconds_;
    class QTimer *timer_;

#ifdef MEERU_HAS_AUDIO
    QAudioInput *input_;
    QAudioFormat format_;
#endif
};

#endif
