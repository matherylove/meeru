#ifndef MEERU_CALL_ENGINE_H
#define MEERU_CALL_ENGINE_H

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#ifdef MEERU_HAS_AUDIO
#include <QAudioFormat>
class QAudioInput;
class QAudioOutput;
class QIODevice;
#endif

class QTimer;

// Calls, video and screen sharing.
//
// About hardware acceleration, since it was asked for: there is no route to it
// on the machines this program targets. NVENC, Quick Sync and AMF all need
// Windows 7 or newer with modern drivers, and DXVA on Windows XP decodes but
// never encodes. The software encoders that could replace them, x264 and
// libvpx, need SSE2 to run in real time, which a Pentium III does not have.
//
// So this uses what genuinely fits:
//
//   Sound travels as uncompressed PCM, mono at 16 kHz, about 256 kbit/s. On a
//   local network that is nothing, and it needs no codec at all.
//
//   Pictures travel as a stream of JPEG frames, which is the approach video
//   conferencing used on hardware of this era. Qt already carries the JPEG
//   encoder, so there is nothing to bundle and nothing to license. At 320x240
//   and eight frames a second it costs under a megabit.
//
//   Screens are captured through GDI, which has worked since Windows XP,
//   scaled down and sent the same way at a lower rate, since a screen changes
//   far less often than a face does.
//
// The cost of that honesty is quality: this is a usable call on a local
// network, not something that competes with a modern conferencing program.
class CallEngine : public QObject
{
    Q_OBJECT

public:
    enum State {
        Idle = 0,
        Ringing,      // we are being called
        Calling,      // we called somebody
        Active,
        Ended
    };

    enum Source {
        SourceCamera = 0,
        SourceScreen = 1
    };

    explicit CallEngine(QObject *parent = 0);
    ~CallEngine();

    static bool audioAvailable();
    static bool cameraAvailable();
    static QString hardwareNote();

    State state() const { return state_; }
    QString conversationId() const { return conversationId_; }
    QStringList participants() const { return participants_; }
    bool isVideoOn() const { return videoOn_; }
    bool isScreenSharing() const { return screenOn_; }
    bool isMuted() const { return muted_; }

    // Starting and answering.
    void startCall(const QString &conversationId, const QStringList &participants, bool withVideo);
    void incomingCall(const QString &conversationId, const QString &fromPeer, bool withVideo);
    void answer(bool withVideo);
    void hangUp();

    void setMuted(bool muted);
    void setVideoOn(bool on);
    void setScreenSharing(bool on);

    // Wire handlers, driven by PeerNode.
    void handleAudio(const QString &peerId, const QByteArray &pcm);
    void handleVideo(const QString &peerId, const QByteArray &jpeg, int source);
    void peerLeft(const QString &peerId);

signals:
    // Outgoing media, to be put on the wire by whoever owns the sessions.
    void audioReady(const QStringList &participants, const QByteArray &pcm);
    void videoReady(const QStringList &participants, const QByteArray &jpeg, int source);
    void signalReady(const QStringList &participants, const QString &kind, bool withVideo);

    void stateChanged(int state);
    void frameReceived(const QString &peerId, const QImage &frame, int source);
    void ringing(const QString &conversationId, const QString &fromPeer, bool withVideo);

private slots:
    void onAudioReadyRead();
    void onVideoTick();
    void onScreenTick();

private:
    void openAudio();
    void closeAudio();
    void setState(State state);

    State state_;
    QString conversationId_;
    QStringList participants_;
    QString caller_;
    bool videoOn_;
    bool screenOn_;
    bool muted_;
    bool offeredVideo_;

    QTimer *videoTimer_;
    QTimer *screenTimer_;

#ifdef MEERU_HAS_AUDIO
    QAudioInput *input_;
    QAudioOutput *output_;
    QIODevice *inputDevice_;
    QIODevice *outputDevice_;
    QAudioFormat format_;
#endif
};

#endif
