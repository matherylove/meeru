#ifndef MEERU_PEER_SESSION_H
#define MEERU_PEER_SESSION_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "identity_crypto.h"

class QTcpSocket;
class QTimer;

// One authenticated, encrypted link to another Meeru user.
//
// Jami proves who you are with a self-signed certificate whose fingerprint is
// the account ID. Meeru does the same thing with the keys it already has: the
// identity ID is BLAKE2b over the Ed25519 public key, so proving ownership of
// that key proves ownership of the ID, and an ID cannot be claimed by anyone
// else.
//
// The handshake is a signed Diffie-Hellman:
//   1. both sides send a hello with their long term keys and a fresh
//      ephemeral X25519 key,
//   2. both sign the transcript of the two hellos with Ed25519,
//   3. the session keys come from the ephemeral exchange, so recording the
//      traffic and stealing the identity key later still does not reveal it.
// After that every frame is XChaCha20-Poly1305 with a per-direction key and a
// counter nonce.
class PeerSession : public QObject
{
    Q_OBJECT

public:
    PeerSession(QTcpSocket *socket,
                const IdentityMaterial &material,
                const QString &ownIdentityId,
                bool initiator,
                const QString &expectedPeerId,
                QObject *parent = 0);
    ~PeerSession();

    QString peerId() const { return peerId_; }
    QString peerAddress() const;
    bool isEstablished() const { return stage_ == Established; }
    bool isInitiator() const { return initiator_; }

    void sendControl(const QJsonObject &object);
    void sendPayload(const QJsonObject &header, const QByteArray &blob);
    void closeSession();

    static int maximumFrameSize();

signals:
    void established(const QString &peerId);
    void controlReceived(const QString &peerId, const QJsonObject &object, const QByteArray &blob);
    void failed(const QString &peerId, const QString &reason);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onTimeout();

private:
    enum Stage {
        WaitingHello = 0,
        WaitingAuth = 1,
        Established = 2,
        Dead = 3
    };

    void sendHello();
    bool handleHello(const QByteArray &frame);
    bool handleAuth(const QByteArray &frame);
    bool handleMessage(const QByteArray &frame);
    void deriveKeys();
    bool writeFrame(const QByteArray &data);
    void fail(const QString &reason);

    QTcpSocket *socket_;
    IdentityMaterial material_;
    QString ownId_;
    QString peerId_;
    QString expectedPeerId_;
    bool initiator_;
    Stage stage_;

    QByteArray inbox_;
    QByteArray ownHello_;
    QByteArray peerHello_;
    QByteArray ephemeralSecret_;
    QByteArray peerEdPublic_;
    QByteArray transcript_;
    QByteArray sendKey_;
    QByteArray receiveKey_;
    quint64 sendCounter_;
    quint64 receiveCounter_;

    QTimer *timeout_;
};

#endif
