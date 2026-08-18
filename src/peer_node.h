#ifndef MEERU_PEER_NODE_H
#define MEERU_PEER_NODE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "identity_crypto.h"
#include "identity_store.h"
#include "meeru_paths.h"
#include "message_store.h"
#include "transfer_manager.h"
#include "roster.h"

class PeerSession;
class PortMapper;
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

// A Meeru user seen announcing itself on the local network.
struct NearbyPeer
{
    NearbyPeer() : connected(false) {}
    QString identityId;
    QString name;
    QString address;
    bool connected;
};

// The peer to peer engine.
//
// Meeru connects people directly, and only directly. Contacts on the same
// network are found by announcing on it; contacts elsewhere are reached at an
// address their invite code carries, which the router can open on its own
// through UPnP or which the user forwards by hand.
//
// There is no directory service and no relay of any kind, which is a real
// limitation and worth naming: two people who are both behind a router that
// will not open a port cannot reach each other at all. What is here works
// without asking anyone for permission or trusting any third party with who
// talks to whom.
//
// Above that layer the model follows Jami: an identity is a key pair, adding
// somebody sends a trust request that stays pending until they accept, and
// profiles travel over the encrypted link between the two devices.
class PeerNode : public QObject
{
    Q_OBJECT

public:
    explicit PeerNode(const MeeruPaths &paths, QObject *parent = 0);
    ~PeerNode();

    bool start(const LocalProfile &profile, const IdentityMaterial &material, QString *error = 0);

    // Set before start(). A fixed port plus an address forwarded by hand is the
    // way through for people whose router does not answer UPnP.
    void setNetworkPreferences(int listenPort, const QString &publicAddress, bool useUpnp);

    // How this device can currently be reached, in words fit for the screen.
    QString reachability() const;

    // A plain account of what the engine is actually doing, so a connection
    // that silently does nothing can be told apart from one that is blocked.
    QString diagnostics() const;
    void stop();
    bool isRunning() const;
    quint16 listenPort() const { return listenPort_; }

    // The roster drives policy: accepted contacts get the full profile, pending
    // ones only get what a request needs, strangers get nothing.
    void setContacts(const QList<Roster::Contact> &contacts);

    // Group conversations, so that connecting to one member delivers whatever
    // was written to that group while they were away.
    void setConversations(const QList<Roster::Conversation> &conversations);

    void setLocalProfile(const QString &displayName, const QString &presence, const QString &statusText);
    void setLocalPictures(const QString &avatarFile, const QString &bannerFile);

    // Sends a trust request, connecting first if needed.
    bool requestContact(const QString &peerId, const QString &endpointHint, const QString &message, QString *error = 0);
    void acceptContact(const QString &peerId);

    // Conversations. A message to somebody who is not connected is kept by the
    // store and handed over the moment they appear, so writing to an offline
    // contact behaves the way people expect from a phone.
    void setMessageStore(MessageStore *store);
    void sendMessage(const QString &peerId, const QString &conversationId, const Chat::Message &message);
    void requestHistory(const QString &peerId, const QString &conversationId);

    // Attachments travel only when the receiver asks; this starts that ask.
    bool receiveAttachment(const QString &peerId, const QString &conversationId, const QString &messageId);
    TransferManager *transfers() const { return transfers_; }
    void forgetPeer(const QString &peerId);

    bool isOnline(const QString &peerId) const;

    // Every address this device believes it answers on, best route first.
    QStringList localEndpoints() const;

    // Everyone announcing themselves on this network, so a contact can be
    // added by picking them from a list instead of copying an ID.
    QList<NearbyPeer> nearbyPeers() const;

    static QString parseEndpointHint(const QString &value, QString *host, quint16 *port);

    // The UDP port used to find people on the local network, needed by the
    // firewall helper so the rule and the socket cannot drift apart.
    static quint16 discoveryUdpPort();

    // Both sides derive the same conversation name from the two Meeru IDs.
    static QString directConversationId(const QString &a, const QString &b);

signals:
    void statusChanged(const QString &summary);
    void peerConnected(const QString &peerId);
    void peerDisconnected(const QString &peerId);
    void trustRequestReceived(const QString &peerId, const QString &displayName, const QString &message);
    void trustAccepted(const QString &peerId);
    void profileReceived(const QString &peerId, const QString &displayName,
                         const QString &presence, const QString &statusText);
    void pictureReceived(const QString &peerId, const QString &kind);
    void messageReceived(const QString &peerId, const QString &conversationId, const Chat::Message &message);
    void messageDelivered(const QString &conversationId, const QString &messageId);
    void typingChanged(const QString &peerId, const QString &conversationId, bool typing);

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
    void onConnectTimeout();
    void onPortMapped(const QString &externalAddress);
    void onPortMappingFailed(const QString &reason);

private:
    void announce(bool query);
    void sendDiscovery(const QByteArray &datagram);
    void adopt(QTcpSocket *socket, bool initiator, const QString &expectedPeerId);
    void dropSession(const QString &peerId);
    int contactState(const QString &peerId) const;
    bool isAccepted(const QString &peerId) const;
    void sendProfile(PeerSession *session, bool withPictures);
    void deliverWaiting(const QString &peerId, PeerSession *session);
    void sendChat(PeerSession *session, const QString &conversationId, const Chat::Message &message);
    void sendPicture(PeerSession *session, const QString &kind);
    void storePicture(const QString &peerId, const QString &kind, const QJsonObject &header, const QByteArray &blob);
    void connectTo(const QString &peerId, const PeerEndpoint &endpoint);
    void reach(const QString &peerId);
    void emitStatus();

    MeeruPaths paths_;
    MessageStore *messages_;
    TransferManager *transfers_;
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
    QHash<QString, QStringList> groupsOf_;   // peer id -> conversation ids
    QHash<QTcpSocket *, QString> connecting_;

    PortMapper *mapper_;
    QString externalAddress_;      // learnt from the router
    QString manualAddress_;        // given by the user
    QString lastError_;
    QDateTime lastErrorAt_;
    int connectionAttempts_;
    int handshakeFailures_;
    int connectionFailures_;
    int inboundConnections_;
    int preferredPort_;
    bool useUpnp_;
};

#endif
