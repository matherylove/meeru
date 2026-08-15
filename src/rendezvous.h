#ifndef MEERU_RENDEZVOUS_H
#define MEERU_RENDEZVOUS_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "identity_crypto.h"

class QTcpServer;
class QTcpSocket;
class QTimer;

// Meeting point for people who are not on the same network.
//
// Two machines behind ordinary home routers cannot dial each other: neither has
// an address the other can reach, and no amount of client side cleverness
// changes that. Jami solves it with bootstrap nodes and TURN relays that the
// project runs; Meeru does the same with this, which is built into the same
// executable and started with --rendezvous.
//
// The relay only ever moves bytes. Peers run the ordinary Meeru handshake
// through it end to end, so the operator of a rendezvous node cannot read a
// message, cannot impersonate anybody, and cannot join a conversation. What it
// does see is who talks to whom and when.
namespace Rendezvous {

quint16 defaultPort();
QByteArray frame(const QJsonObject &object);
QString registrationChallenge(const QByteArray &nonce);

}

// Runs inside every Meeru: keeps one connection to each configured rendezvous
// node so contacts can find this device, and opens relayed connections out.
class RendezvousClient : public QObject
{
    Q_OBJECT

public:
    explicit RendezvousClient(QObject *parent = 0);
    ~RendezvousClient();

    void start(const QString &identityId, const IdentityMaterial &material,
               const QStringList &hosts, quint16 listenPort);
    void stop();
    bool isConnected() const;
    QString observedAddress() const { return observedAddress_; }

    void setLocalEndpoints(const QStringList &endpoints);

    // Asks a rendezvous node to put us through to that identity.
    void requestConnection(const QString &peerId);

signals:
    // A socket that is already piped to the peer; the caller runs the normal
    // handshake over it.
    void relayedSocket(const QString &peerId, QTcpSocket *socket, bool initiator);
    void directCandidates(const QString &peerId, const QStringList &endpoints);
    void statusChanged(const QString &summary);
    void peerUnreachable(const QString &peerId, const QString &reason);

private slots:
    void onControlConnected();
    void onControlReadyRead();
    void onControlDisconnected();
    void onRelayConnected();
    void onRelayError();
    void onRetryTick();
    void onKeepAliveTick();

private:
    struct Host {
        QString host;
        quint16 port;
    };

    void connectToHost();
    void sendControl(const QJsonObject &object);
    void handleControl(const QJsonObject &object);
    void openRelay(const QString &channel, const QString &peerId, bool initiator);

    QString identityId_;
    IdentityMaterial material_;
    QList<Host> hosts_;
    int hostIndex_;
    quint16 listenPort_;
    QStringList endpoints_;
    QString observedAddress_;
    bool running_;
    bool registered_;

    QTcpSocket *control_;
    QByteArray inbox_;
    QTimer *retry_;
    QTimer *keepAlive_;
    QHash<QTcpSocket *, QString> pendingRelays_;   // socket -> peer id
};

// The node an operator runs on a machine with a public address.
class RendezvousServer : public QObject
{
    Q_OBJECT

public:
    explicit RendezvousServer(QObject *parent = 0);

    bool listen(quint16 port, QString *error = 0);
    quint16 port() const;
    int registeredCount() const { return registrations_.size(); }

signals:
    void logMessage(const QString &text);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onSweep();

private:
    struct Pending {
        QTcpSocket *caller;
        QTcpSocket *callee;
        QByteArray callerBuffer;
        QByteArray calleeBuffer;
        qint64 createdAt;
    };

    void handleFrame(QTcpSocket *socket, const QJsonObject &object);
    void pipe(QTcpSocket *from, QTcpSocket *to, const QByteArray &data);
    void dropSocket(QTcpSocket *socket);

    QTcpServer *server_;
    QTimer *sweep_;
    QHash<QTcpSocket *, QByteArray> inboxes_;
    QHash<QTcpSocket *, QByteArray> challenges_;
    QHash<QTcpSocket *, QString> identities_;      // control sockets
    QHash<QString, QTcpSocket *> registrations_;   // identity -> control socket
    QHash<QString, QStringList> endpoints_;        // identity -> advertised addresses
    QHash<QTcpSocket *, QTcpSocket *> pipes_;      // relay socket -> its partner
    QHash<QString, Pending> channels_;
    QHash<QTcpSocket *, QString> channelOf_;
};

#endif
