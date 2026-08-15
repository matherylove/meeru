#ifndef MEERU_PEER_NODE_H
#define MEERU_PEER_NODE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "identity_crypto.h"
#include "identity_store.h"
#include "meeru_paths.h"
#include "roster.h"

class PeerSession;
class PortMapper;
class RendezvousClient;
class QTcpServer;
class QTcpSocket;
class QTimer;
class QUdpSocket;

// What Meeru knows about a peer it has seen announce itself on the network.
struct PeerEndpoint
{
    PeerEndpoint() : port(0) {}
    QString host;
    quint16 port;
    QString name;

    bool isValid() const { return !host.isEmpty() && port != 0; }
    QString toString() const;
};

// The peer to peer engine.
//
// Reachability is deliberately smaller than Jami's: Jami finds peers through
// OpenDHT and punches through NAT with ICE, which is a project of its own.
// Meeru currently finds peers by announcing itself on the local network, and
// otherwise connects to an address the user was given. Everything above that
// layer follows Jami: an identity is a key pair, adding somebody sends a trust
// request that stays pending until they accept, and profiles travel over the
// encrypted link between the two devices rather than through a server.
class PeerNode : public QObject
{
    Q_OBJECT

public:
    explicit PeerNode(const MeeruPaths &paths, QObject *parent = 0);
    ~PeerNode();

    bool start(const LocalProfile &profile, const IdentityMaterial &material, QString *error = 0);

    // Rendezvous nodes let contacts outside this network find each other. With
    // none configured Meeru still works, but only on the local network or with
    // an address typed by hand.
    void setRendezvousHosts(const QStringList &hosts);

    // Set before start(). A fixed port plus an address forwarded by hand is the
    // way through for people whose router does not answer UPnP.
    void setNetworkPreferences(int listenPort, const QString &publicAddress, bool useUpnp);
    QString reachability() const;
    void stop();
    bool isRunning() const;
    quint16 listenPort() const { return listenPort_; }

    // The roster drives policy: accepted contacts get the full profile, pending
    // ones only get what a request needs, strangers get nothing.
    void setContacts(const QList<Roster::Contact> &contacts);

    void setLocalProfile(const QString &displayName, const QString &presence, const QString &statusText);
    void setLocalPictures(const QString &avatarFile, const QString &bannerFile);

    // Sends a trust request, connecting first if needed.
    bool requestContact(const QString &peerId, const QString &endpointHint, const QString &message, QString *error = 0);
    void acceptContact(const QString &peerId);
    void forgetPeer(const QString &peerId);

    bool isOnline(const QString &peerId) const;

    // Every address this device believes it answers on, best route first.
    QStringList localEndpoints() const;
    PeerEndpoint knownEndpoint(const QString &peerId) const;

    static quint16 discoveryPort();
    static QString parseEndpointHint(const QString &value, QString *host, quint16 *port);

signals:
    void statusChanged(const QString &summary);
    void peerConnected(const QString &peerId);
    void peerDisconnected(const QString &peerId);
    void trustRequestReceived(const QString &peerId, const QString &displayName, const QString &message);
    void trustAccepted(const QString &peerId);
    void profileReceived(const QString &peerId, const QString &displayName,
                         const QString &presence, const QString &statusText);
    void pictureReceived(const QString &peerId, const QString &kind);

private slots:
    void onIncomingConnection();
    void onSessionEstablished(const QString &peerId);
    void onSessionFailed(const QString &peerId, const QString &reason);
    void onSessionMessage(const QString &peerId, const QJsonObject &object, const QByteArray &blob);
    void onDiscoveryDatagram();
    void onAnnounceTick();
    void onReconnectTick();
    void onOutgoingConnected();
    void onOutgoingError();
    void onPortMapped(const QString &externalAddress);
    void onPortMappingFailed(const QString &reason);
    void onRelayedSocket(const QString &peerId, QTcpSocket *socket, bool initiator);
    void onDirectCandidates(const QString &peerId, const QStringList &endpoints);
    void onRendezvousStatus(const QString &summary);
    void onPeerUnreachable(const QString &peerId, const QString &reason);

private:
    void announce(bool query);
    void sendDiscovery(const QByteArray &datagram);
    void adopt(QTcpSocket *socket, bool initiator, const QString &expectedPeerId);
    void dropSession(const QString &peerId);
    int contactState(const QString &peerId) const;
    bool isAccepted(const QString &peerId) const;
    void sendProfile(PeerSession *session, bool withPictures);
    void sendPicture(PeerSession *session, const QString &kind);
    void storePicture(const QString &peerId, const QString &kind, const QJsonObject &header, const QByteArray &blob);
    void connectTo(const QString &peerId, const PeerEndpoint &endpoint);
    void reach(const QString &peerId);
    void publishEndpoints();
    void emitStatus();

    MeeruPaths paths_;
    LocalProfile profile_;
    IdentityMaterial material_;
    QString statusText_;
    QString avatarFile_;
    QString bannerFile_;

    QTcpServer *server_;
    QUdpSocket *discovery_;
    QTimer *announceTimer_;
    QTimer *reconnectTimer_;
    quint16 listenPort_;
    bool running_;

    QHash<QString, PeerSession *> sessions_;
    QHash<QString, PeerEndpoint> endpoints_;
    QHash<QString, QString> pendingHints_;      // peer id -> address the user typed
    QHash<QString, QString> pendingRequests_;   // peer id -> message to deliver once connected
    QHash<QString, int> contactStates_;
    QHash<QTcpSocket *, QString> connecting_;

    PortMapper *mapper_;
    RendezvousClient *rendezvous_;
    QStringList rendezvousHosts_;
    QString externalAddress_;      // learnt from the router
    QString manualAddress_;        // given by the user
    QString rendezvousStatus_;
    int preferredPort_;
    bool useUpnp_;
};

#endif
