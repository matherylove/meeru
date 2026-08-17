#include "peer_node.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include "avatar.h"
#include "peer_session.h"
#include "port_mapper.h"
#include "presence.h"

namespace {

// True for addresses that only mean something inside somebody else's house.
bool isPrivateAddress(const QHostAddress &address)
{
    if (address.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    const quint32 ip = address.toIPv4Address();
    if ((ip & 0xFF000000u) == 0x0A000000u) return true;   // 10.0.0.0/8
    if ((ip & 0xFFF00000u) == 0xAC100000u) return true;   // 172.16.0.0/12
    if ((ip & 0xFFFF0000u) == 0xC0A80000u) return true;   // 192.168.0.0/16
    if ((ip & 0xFFC00000u) == 0x64400000u) return true;   // 100.64.0.0/10, carrier NAT
    return false;
}

// The address Windows gives itself when no DHCP server answered. It reaches
// nothing, not even the machine next to it, and announcing one only sends
// contacts chasing an address that cannot work.
bool isSelfAssignedAddress(const QHostAddress &address)
{
    if (address.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    return (address.toIPv4Address() & 0xFFFF0000u) == 0xA9FE0000u;   // 169.254.0.0/16
}

// Meeru is a local network messenger, and a virtual adapter from Hamachi,
// ZeroTier or a VPN is a local network as far as it is concerned. Those are
// often exactly the interface a contact is reachable on, so they are ranked
// ahead of an ordinary card rather than buried behind it.
bool looksLikeVirtualNetwork(const QNetworkInterface &interface)
{
    const QString name = (interface.humanReadableName() + QLatin1Char(' ')
                          + interface.name()).toLower();
    static const char *hints[] = { "hamachi", "zerotier", "radmin", "tailscale",
                                   "tap-windows", "tap adapter", "wireguard",
                                   "openvpn", "vpn", "virtual" };
    for (int i = 0; i < 10; ++i) {
        if (name.contains(QString::fromLatin1(hints[i])))
            return true;
    }
    return false;
}

const quint16 kDiscoveryPort = 47440;
const quint16 kFirstListenPort = 47441;
const int kListenPortRange = 20;
const int kAnnounceIntervalMs = 8000;
const int kReconnectIntervalMs = 20000;
const int kMaxPictureBytes = 3 * 1024 * 1024;
const int kMaxPortNumber = 65535;
const int kConnectTimeoutMs = 8000;

const char kMsgProfile[] = "profile";
const char kMsgTrustRequest[] = "trust-request";
const char kMsgTrustAccepted[] = "trust-accepted";
const char kMsgPicture[] = "picture";
const char kMsgPictureRequest[] = "picture-request";
const char kMsgChat[] = "chat";
const char kMsgChatAck[] = "chat-ack";
const char kMsgHistoryRequest[] = "history-request";
const char kMsgTyping[] = "typing";

QString peerDirectory(const MeeruPaths &paths, const QString &ownerId, const QString &peerId)
{
    return paths.identityDirectory(ownerId) + QLatin1String("/peers/") + peerId;
}

}

// The conversation a pair of people share is named after both of them, so the
// two sides arrive at the same name without having to agree on one.
QString PeerNode::directConversationId(const QString &a, const QString &b)
{
    return (a < b) ? (a + QLatin1Char('|') + b) : (b + QLatin1Char('|') + a);
}

quint16 PeerNode::discoveryUdpPort()
{
    return kDiscoveryPort;
}

QString PeerEndpoint::toString() const
{
    if (!isValid())
        return QString();
    return host + QLatin1Char(':') + QString::number(port);
}

// Accepts "meeru:<id>@host:port", "<id>@host:port" and a bare "host:port".
QString PeerNode::parseEndpointHint(const QString &value, QString *host, quint16 *port)
{
    QString rest = value.trimmed();
    QString identity;

    const int at = rest.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        identity = Roster::normaliseIdentityId(rest.left(at));
        rest = rest.mid(at + 1).trimmed();
    } else {
        const QString candidate = Roster::normaliseIdentityId(rest);
        if (!candidate.isEmpty()) {
            if (host)
                host->clear();
            if (port)
                *port = 0;
            return candidate;
        }
    }

    const int colon = rest.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const int number = rest.mid(colon + 1).toInt(&ok);
        if (ok && number > 0 && number <= kMaxPortNumber) {
            if (host)
                *host = rest.left(colon).trimmed();
            if (port)
                *port = static_cast<quint16>(number);
            return identity;
        }
    }

    if (!rest.isEmpty()) {
        if (host)
            *host = rest;
        if (port)
            *port = kFirstListenPort;
    }
    return identity;
}

PeerNode::PeerNode(const MeeruPaths &paths, QObject *parent)
    : QObject(parent),
      paths_(paths),
      messages_(0),
      server_(0),
      discovery_(0),
      announceTimer_(0),
      reconnectTimer_(0),
      listenPort_(0),
      running_(false),
      mapper_(0),
      preferredPort_(0),
      useUpnp_(true),
      connectionAttempts_(0),
      handshakeFailures_(0),
      connectionFailures_(0),
      inboundConnections_(0)
{
}

void PeerNode::setNetworkPreferences(int listenPort, const QString &publicAddress, bool useUpnp)
{
    preferredPort_ = (listenPort > 0 && listenPort <= kMaxPortNumber) ? listenPort : 0;
    manualAddress_ = publicAddress.trimmed();
    useUpnp_ = useUpnp;
}

PeerNode::~PeerNode()
{
    stop();
    material_.clear();
}

bool PeerNode::isRunning() const
{
    return running_;
}

bool PeerNode::start(const LocalProfile &profile, const IdentityMaterial &material, QString *error)
{
    stop();

    if (!material.isValid()) {
        if (error)
            *error = QString::fromLatin1("The identity keys are not available");
        return false;
    }

    profile_ = profile;
    material_ = material;

    server_ = new QTcpServer(this);
    if (preferredPort_ > 0 && server_->listen(QHostAddress::Any, static_cast<quint16>(preferredPort_)))
        listenPort_ = server_->serverPort();
    for (int i = 0; i < kListenPortRange && listenPort_ == 0; ++i) {
        if (server_->listen(QHostAddress::Any, static_cast<quint16>(kFirstListenPort + i)))
            listenPort_ = server_->serverPort();
    }
    if (listenPort_ == 0 && server_->listen(QHostAddress::Any, 0))
        listenPort_ = server_->serverPort();

    if (listenPort_ == 0) {
        if (error)
            *error = QString::fromLatin1("Meeru could not open a port to receive connections");
        delete server_;
        server_ = 0;
        return false;
    }
    connect(server_, SIGNAL(newConnection()), this, SLOT(onIncomingConnection()));

    discovery_ = new QUdpSocket(this);
    if (!discovery_->bind(QHostAddress(QHostAddress::AnyIPv4), kDiscoveryPort,
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        // Losing discovery is not fatal: connecting by address still works.
        delete discovery_;
        discovery_ = 0;
    } else {
        connect(discovery_, SIGNAL(readyRead()), this, SLOT(onDiscoveryDatagram()));
    }

    announceTimer_ = new QTimer(this);
    announceTimer_->setInterval(kAnnounceIntervalMs);
    connect(announceTimer_, SIGNAL(timeout()), this, SLOT(onAnnounceTick()));
    announceTimer_->start();

    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(kReconnectIntervalMs);
    connect(reconnectTimer_, SIGNAL(timeout()), this, SLOT(onReconnectTick()));
    reconnectTimer_->start();

    running_ = true;

    // One way to be reachable from outside: ask the router to forward the port.
    // If it refuses, the user can forward one by hand in Settings instead.
    if (useUpnp_) {
        mapper_ = new PortMapper(this);
        connect(mapper_, SIGNAL(mapped(QString)), this, SLOT(onPortMapped(QString)));
        connect(mapper_, SIGNAL(failed(QString)), this, SLOT(onPortMappingFailed(QString)));
        mapper_->requestMapping(listenPort_);
    }


    announce(true);
    emitStatus();
    return true;
}

QString PeerNode::diagnostics() const
{
    QStringList lines;

    if (!running_) {
        lines.append(QString::fromLatin1("The engine is not running."));
        return lines.join(QString::fromLatin1("\n"));
    }

    lines.append(QString::fromLatin1("Listening for contacts on TCP port %1.").arg(listenPort_));
    lines.append(discovery_
        ? QString::fromLatin1("Local network discovery is on (UDP port %1).").arg(kDiscoveryPort)
        : QString::fromLatin1("Local network discovery could NOT open its UDP port."));

    lines.append(QString::fromLatin1("Meeru users seen on this network: %1.").arg(endpoints_.size()));
    QHash<QString, PeerEndpoint>::const_iterator it = endpoints_.constBegin();
    for (; it != endpoints_.constEnd(); ++it) {
        lines.append(QString::fromLatin1("    %1 at %2%3")
                         .arg(it.value().name.isEmpty() ? it.key().left(12) : it.value().name)
                         .arg(it.value().toString())
                         .arg(sessions_.contains(it.key()) ? QString::fromLatin1("  (connected)")
                                                           : QString()));
    }

    int established = 0;
    QHash<QString, PeerSession *>::const_iterator session = sessions_.constBegin();
    for (; session != sessions_.constEnd(); ++session) {
        if (session.value()->isEstablished())
            ++established;
    }
    lines.append(QString::fromLatin1("Connections established: %1.").arg(established));
    lines.append(QString::fromLatin1("Connection attempts made: %1.").arg(connectionAttempts_));
    lines.append(QString::fromLatin1("Incoming connections received: %1.").arg(inboundConnections_));
    lines.append(QString::fromLatin1("Requests waiting to be delivered: %1.").arg(pendingRequests_.size()));

    if (connectionFailures_ > 0)
        lines.append(QString::fromLatin1("Failed connections: %1.").arg(connectionFailures_));
    if (handshakeFailures_ > 0)
        lines.append(QString::fromLatin1("Refused handshakes: %1.").arg(handshakeFailures_));
    if (!lastError_.isEmpty())
        lines.append(QString::fromLatin1("\nLast problem: %1").arg(lastError_));

    if (connectionAttempts_ == 0 && pendingRequests_.isEmpty()) {
        lines.append(QString::fromLatin1(
            "\nNothing to report yet: no request has needed sending."));
    } else if (connectionAttempts_ == 0) {
        lines.append(QString::fromLatin1(
            "\nThere is a request waiting but no address to send it to. Nobody with that ID was "
            "found on this network. To reach somebody outside it, Meeru needs an address: paste "
            "their invite code, which carries one, or type it in the address field."));
    } else if (established == 0 && !endpoints_.isEmpty()) {
        lines.append(QString::fromLatin1(
            "\nThe other computer was found on this network but the connection did not complete. "
            "That points at something blocking incoming connections on the port above: a firewall, "
            "an antivirus, or client isolation on the router."));
    } else if (established == 0) {
        lines.append(QString::fromLatin1(
            "\nAn address was tried but nothing answered. Whatever is at that address is not letting "
            "the connection through: on the far side, either no port is open at the router, or the "
            "firewall of that computer is dropping it before Meeru ever sees it."));
    }

    // Being reachable on paper and never receiving anything is the single most
    // telling combination, and it points squarely at the receiving side.
    if (inboundConnections_ == 0 && (!externalAddress_.isEmpty() || !manualAddress_.isEmpty())) {
        lines.append(QString::fromLatin1(
            "\nThis computer believes it is reachable from the internet but has never received a "
            "single incoming connection. If a contact is trying to reach you and failing, the port "
            "forwarding is not really in place, or Windows Firewall is dropping the connection before "
            "Meeru sees it. Settings can add the firewall rules."));
    }

    return lines.join(QString::fromLatin1("\n"));
}

QString PeerNode::reachability() const
{
    if (!running_)
        return QString::fromLatin1("Offline");
    if (!manualAddress_.isEmpty())
        return QString::fromLatin1("Reachable at ") + manualAddress_
             + QString::fromLatin1(" (forwarded by hand)");
    if (!externalAddress_.isEmpty())
        return QString::fromLatin1("Reachable at ") + externalAddress_
             + QString::fromLatin1(" (opened by your router)");
    return QString::fromLatin1("Local network only");
}

QList<NearbyPeer> PeerNode::nearbyPeers() const
{
    QList<NearbyPeer> peers;
    QHash<QString, PeerEndpoint>::const_iterator it = endpoints_.constBegin();
    for (; it != endpoints_.constEnd(); ++it) {
        if (!it.value().isValid())
            continue;
        NearbyPeer peer;
        peer.identityId = it.key();
        peer.name = it.value().name;
        peer.address = it.value().toString();
        peer.connected = sessions_.contains(it.key());
        peers.append(peer);
    }
    return peers;
}

QStringList PeerNode::localEndpoints() const
{
    QStringList endpoints;

    // An address the user forwarded by hand is trusted ahead of anything the
    // router volunteered, since they know their own setup better than UPnP does.
    if (!manualAddress_.isEmpty()) {
        QString host;
        quint16 port = 0;
        parseEndpointHint(manualAddress_, &host, &port);
        if (!host.isEmpty())
            endpoints.append(host + QLatin1Char(':') + QString::number(port ? port : listenPort_));
    }
    if (!externalAddress_.isEmpty() && !endpoints.contains(externalAddress_))
        endpoints.append(externalAddress_);

    // Virtual adapters first, then ordinary ones. Nothing self-assigned, and
    // nothing from an interface that is not actually up.
    QStringList virtualEndpoints;
    QStringList ordinaryEndpoints;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (int i = 0; i < interfaces.size(); ++i) {
        const QNetworkInterface &interface = interfaces.at(i);
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const bool isVirtual = looksLikeVirtualNetwork(interface);
        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QHostAddress ip = entries.at(j).ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            if (isSelfAssignedAddress(ip))
                continue;

            const QString endpoint = ip.toString() + QLatin1Char(':') + QString::number(listenPort_);
            if (isVirtual)
                virtualEndpoints.append(endpoint);
            else
                ordinaryEndpoints.append(endpoint);
        }
    }

    endpoints += virtualEndpoints;
    endpoints += ordinaryEndpoints;
    return endpoints;
}

void PeerNode::onPortMapped(const QString &externalAddress)
{
    externalAddress_ = externalAddress;
    emitStatus();
}

void PeerNode::onPortMappingFailed(const QString &reason)
{
    Q_UNUSED(reason);
    // Expected on plenty of networks; the relay covers it.
    externalAddress_.clear();
    emitStatus();
}





// Tries the cheapest route first and falls back to the relay.
void PeerNode::reach(const QString &peerId)
{
    if (!running_ || sessions_.contains(peerId))
        return;

    const PeerEndpoint discovered = endpoints_.value(peerId);
    if (discovered.isValid()) {
        connectTo(peerId, discovered);
        return;
    }

    // An invite code can carry several addresses: a public one and one or more
    // on the sender's own network. Try each until something answers, but skip
    // the ones that cannot possibly work from here.
    if (pendingHints_.contains(peerId)) {
        const QStringList hints = pendingHints_.value(peerId)
                                      .split(QLatin1Char(','), QString::SkipEmptyParts);
        int usable = 0;
        int privateSkipped = 0;

        for (int i = 0; i < hints.size(); ++i) {
            QString host;
            quint16 port = 0;
            parseEndpointHint(hints.at(i).trimmed(), &host, &port);
            if (host.isEmpty() || port == 0)
                continue;

            // A private address belongs to the other person's own network. If
            // they are not on ours too, dialling it reaches nothing here, or
            // worse, reaches an unrelated machine that happens to have that
            // address on our side.
            const QHostAddress candidate(host);
            if (isSelfAssignedAddress(candidate)) {
                ++privateSkipped;
                continue;   // reaches nothing, anywhere, ever
            }
            if (isPrivateAddress(candidate) && !endpoints_.contains(peerId)) {
                ++privateSkipped;
                continue;
            }

            PeerEndpoint endpoint;
            endpoint.host = host;
            endpoint.port = port;
            connectTo(peerId, endpoint);
            ++usable;
        }

        if (usable > 0)
            return;

        if (privateSkipped > 0) {
            lastError_ = QString::fromLatin1(
                "The only addresses given for that contact are on their own home network, which "
                "cannot be reached from here. They need a public address: either their router opens "
                "one through UPnP, or they forward a port by hand in Settings.");
            lastErrorAt_ = QDateTime::currentDateTimeUtc();
        }
    }

    // Nothing known and nothing to ask: announce once more in case they only
    // just appeared on this network.
    announce(true);
}






void PeerNode::stop()
{
    running_ = false;

    QHash<QString, PeerSession *>::const_iterator it = sessions_.constBegin();
    for (; it != sessions_.constEnd(); ++it) {
        it.value()->closeSession();
        it.value()->deleteLater();
    }
    sessions_.clear();

    QHash<QTcpSocket *, QString>::const_iterator pending = connecting_.constBegin();
    for (; pending != connecting_.constEnd(); ++pending)
        pending.key()->deleteLater();
    connecting_.clear();

    if (mapper_) {
        mapper_->release();
        delete mapper_;
        mapper_ = 0;
    }

    delete server_;
    server_ = 0;
    delete discovery_;
    discovery_ = 0;
    delete announceTimer_;
    announceTimer_ = 0;
    delete reconnectTimer_;
    reconnectTimer_ = 0;
    listenPort_ = 0;
}

void PeerNode::setContacts(const QList<Roster::Contact> &contacts)
{
    contactStates_.clear();
    for (int i = 0; i < contacts.size(); ++i)
        contactStates_.insert(contacts.at(i).id, contacts.at(i).state);
}

int PeerNode::contactState(const QString &peerId) const
{
    return contactStates_.value(peerId, -1);
}

bool PeerNode::isAccepted(const QString &peerId) const
{
    return contactState(peerId) == Roster::ContactAccepted;
}

void PeerNode::setLocalProfile(const QString &displayName, const QString &presence, const QString &statusText)
{
    profile_.displayName = displayName;
    profile_.presence = presence;
    statusText_ = statusText;

    QHash<QString, PeerSession *>::const_iterator it = sessions_.constBegin();
    for (; it != sessions_.constEnd(); ++it) {
        if (it.value()->isEstablished() && isAccepted(it.key()))
            sendProfile(it.value(), false);
    }
    announce(false);
}

void PeerNode::setLocalPictures(const QString &avatarFile, const QString &bannerFile)
{
    const bool changed = (avatarFile_ != avatarFile) || (bannerFile_ != bannerFile);
    avatarFile_ = avatarFile;
    bannerFile_ = bannerFile;
    if (!changed)
        return;

    QHash<QString, PeerSession *>::const_iterator it = sessions_.constBegin();
    for (; it != sessions_.constEnd(); ++it) {
        if (it.value()->isEstablished() && isAccepted(it.key())) {
            sendPicture(it.value(), QString::fromLatin1("avatar"));
            sendPicture(it.value(), QString::fromLatin1("banner"));
        }
    }
}

// ------------------------------------------------------------------ discovery

void PeerNode::announce(bool query)
{
    if (!discovery_ || !running_)
        return;

    // An invisible user does not shout their presence across the network.
    if (Presence::stateFromKey(profile_.presence) == Presence::Invisible && !query)
        return;

    QJsonObject object;
    object.insert("meeru", 1);
    object.insert("type", query ? QString::fromLatin1("query") : QString::fromLatin1("announce"));
    object.insert("id", profile_.identityId);
    object.insert("port", static_cast<int>(listenPort_));
    object.insert("name", profile_.displayName);

    sendDiscovery(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void PeerNode::sendDiscovery(const QByteArray &datagram)
{
    if (!discovery_)
        return;

    discovery_->writeDatagram(datagram, QHostAddress(QHostAddress::Broadcast), kDiscoveryPort);

    // Some XP era setups only deliver on the interface broadcast address.
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (int i = 0; i < interfaces.size(); ++i) {
        const QNetworkInterface &interface = interfaces.at(i);
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QHostAddress broadcast = entries.at(j).broadcast();
            if (!broadcast.isNull())
                discovery_->writeDatagram(datagram, broadcast, kDiscoveryPort);
        }
    }
}

void PeerNode::onAnnounceTick()
{
    announce(false);
}

void PeerNode::onDiscoveryDatagram()
{
    while (discovery_ && discovery_->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(discovery_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        discovery_->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        const QJsonObject object = QJsonDocument::fromJson(datagram).object();
        if (object.value("meeru").toInt() != 1)
            continue;

        const QString id = object.value("id").toString();
        if (id.isEmpty() || id == profile_.identityId || !Roster::isValidIdentityId(id))
            continue;

        PeerEndpoint endpoint;
        endpoint.host = sender.toString();
        endpoint.port = static_cast<quint16>(object.value("port").toInt());
        endpoint.name = object.value("name").toString();
        if (endpoint.isValid())
            endpoints_.insert(id, endpoint);

        if (object.value("type").toString() == QLatin1String("query")) {
            // Answer directly rather than broadcasting again.
            QJsonObject reply;
            reply.insert("meeru", 1);
            reply.insert("type", QString::fromLatin1("announce"));
            reply.insert("id", profile_.identityId);
            reply.insert("port", static_cast<int>(listenPort_));
            reply.insert("name", profile_.displayName);
            if (Presence::stateFromKey(profile_.presence) != Presence::Invisible) {
                discovery_->writeDatagram(QJsonDocument(reply).toJson(QJsonDocument::Compact),
                                          sender, kDiscoveryPort);
            }
        }

        // Somebody we care about just appeared: reach out.
        if (endpoint.isValid() && !sessions_.contains(id)
            && (isAccepted(id) || pendingRequests_.contains(id))) {
            connectTo(id, endpoint);
        }
    }
}

bool PeerNode::isOnline(const QString &peerId) const
{
    PeerSession *session = sessions_.value(peerId, 0);
    return session && session->isEstablished();
}

// ----------------------------------------------------------------- connecting

void PeerNode::connectTo(const QString &peerId, const PeerEndpoint &endpoint)
{
    if (!running_ || !endpoint.isValid() || sessions_.contains(peerId))
        return;

    // Several candidate addresses may be tried at once; the first handshake to
    // finish wins and the rest are dropped in onSessionEstablished.
    int attempts = 0;
    QHash<QTcpSocket *, QString>::const_iterator it = connecting_.constBegin();
    for (; it != connecting_.constEnd(); ++it) {
        if (it.value() == peerId)
            ++attempts;
    }
    if (attempts >= 4)
        return;

    ++connectionAttempts_;
    QTcpSocket *socket = new QTcpSocket(this);

    // Remembered separately: while a socket is still connecting, and after it
    // is aborted, peerAddress() and peerPort() are empty, which is how the
    // diagnostics ended up reporting a useless ":0".
    socket->setProperty("meeruHost", endpoint.host);
    socket->setProperty("meeruPort", static_cast<int>(endpoint.port));
    connecting_.insert(socket, peerId);
    connect(socket, SIGNAL(connected()), this, SLOT(onOutgoingConnected()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onOutgoingError()));

    QTimer *guard = new QTimer(this);
    guard->setSingleShot(true);
    guard->setInterval(kConnectTimeoutMs);
    guard->setProperty("socket", QVariant::fromValue(static_cast<QObject *>(socket)));
    connect(guard, SIGNAL(timeout()), this, SLOT(onConnectTimeout()));
    connect(socket, SIGNAL(connected()), guard, SLOT(deleteLater()));
    guard->start();

    socket->connectToHost(endpoint.host, endpoint.port);
}

void PeerNode::onOutgoingConnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    const QString peerId = connecting_.value(socket);
    connecting_.remove(socket);
    socket->disconnect(this);
    adopt(socket, true, peerId);
}

void PeerNode::onOutgoingError()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    // Why a connection failed is the single most useful thing to know when
    // nothing appears to happen, so it is recorded rather than discarded.
    lastError_ = QString::fromLatin1("Could not connect to %1:%2 - %3")
                     .arg(socket->property("meeruHost").toString())
                     .arg(socket->property("meeruPort").toInt())
                     .arg(socket->errorString());
    lastErrorAt_ = QDateTime::currentDateTimeUtc();
    ++connectionFailures_;

    connecting_.remove(socket);
    socket->deleteLater();
}

void PeerNode::onConnectTimeout()
{
    // A dropped SYN leaves the socket in "connecting" for tens of seconds and
    // blocks further attempts to that peer, so give up early and say so.
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer)
        return;

    QTcpSocket *socket = qobject_cast<QTcpSocket *>(timer->property("socket").value<QObject *>());
    timer->deleteLater();
    if (!socket || !connecting_.contains(socket))
        return;

    lastError_ = QString::fromLatin1("No answer from %1:%2 within %3 seconds. The packets are being "
                                     "dropped somewhere between the two computers.")
                     .arg(socket->property("meeruHost").toString())
                     .arg(socket->property("meeruPort").toInt())
                     .arg(kConnectTimeoutMs / 1000);
    lastErrorAt_ = QDateTime::currentDateTimeUtc();
    ++connectionFailures_;

    connecting_.remove(socket);
    socket->abort();
    socket->deleteLater();
}

void PeerNode::onIncomingConnection()
{
    while (server_ && server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        ++inboundConnections_;
        adopt(socket, false, QString());
    }
}

void PeerNode::adopt(QTcpSocket *socket, bool initiator, const QString &expectedPeerId)
{
    PeerSession *session = new PeerSession(socket, material_, profile_.identityId,
                                           initiator, expectedPeerId, this);
    connect(session, SIGNAL(established(QString)), this, SLOT(onSessionEstablished(QString)));
    connect(session, SIGNAL(failed(QString,QString)), this, SLOT(onSessionFailed(QString,QString)));
    connect(session, SIGNAL(controlReceived(QString,QJsonObject,QByteArray)),
            this, SLOT(onSessionMessage(QString,QJsonObject,QByteArray)));

    // Only now is it safe to let the handshake run.
    session->begin();
}

void PeerNode::onSessionEstablished(const QString &peerId)
{
    PeerSession *session = qobject_cast<PeerSession *>(sender());
    if (!session)
        return;

    // One link per peer: if both sides dialled at once, keep the older one.
    if (sessions_.contains(peerId) && sessions_.value(peerId) != session) {
        session->closeSession();
        session->deleteLater();
        return;
    }
    sessions_.insert(peerId, session);

    PeerEndpoint endpoint = endpoints_.value(peerId);
    if (!endpoint.isValid()) {
        const QString address = session->peerAddress();
        const int colon = address.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            endpoint.host = address.left(colon);
            endpoint.port = static_cast<quint16>(address.mid(colon + 1).toInt());
            endpoints_.insert(peerId, endpoint);
        }
    }

    const bool accepted = isAccepted(peerId);
    sendProfile(session, accepted);

    if (pendingRequests_.contains(peerId)) {
        QJsonObject request;
        request.insert("type", QString::fromLatin1(kMsgTrustRequest));
        request.insert("displayName", profile_.displayName);
        request.insert("message", pendingRequests_.value(peerId));
        session->sendControl(request);
        pendingRequests_.remove(peerId);
    }

    if (accepted) {
        sendPicture(session, QString::fromLatin1("avatar"));
        sendPicture(session, QString::fromLatin1("banner"));
        deliverWaiting(peerId, session);
        requestHistory(peerId, directConversationId(profile_.identityId, peerId));
    }

    emit peerConnected(peerId);
    emitStatus();
}

void PeerNode::onSessionFailed(const QString &peerId, const QString &reason)
{
    // Surfaced rather than swallowed: a refused handshake is exactly the kind
    // of failure that otherwise looks like "nothing happens at all".
    if (!reason.isEmpty() && reason != QLatin1String("The connection closed")) {
        ++handshakeFailures_;
        lastError_ = reason;
        lastErrorAt_ = QDateTime::currentDateTimeUtc();
        emit statusChanged(reason);
    }

    PeerSession *session = qobject_cast<PeerSession *>(sender());
    if (session) {
        if (!peerId.isEmpty() && sessions_.value(peerId) == session)
            sessions_.remove(peerId);
        session->deleteLater();
    }
    if (!peerId.isEmpty())
        emit peerDisconnected(peerId);
    emitStatus();
}

void PeerNode::dropSession(const QString &peerId)
{
    PeerSession *session = sessions_.take(peerId);
    if (session) {
        session->closeSession();
        session->deleteLater();
    }
}

void PeerNode::forgetPeer(const QString &peerId)
{
    dropSession(peerId);
    pendingRequests_.remove(peerId);
    pendingHints_.remove(peerId);
    contactStates_.remove(peerId);
}

void PeerNode::onReconnectTick()
{
    if (!running_)
        return;

    QHash<QString, int>::const_iterator it = contactStates_.constBegin();
    for (; it != contactStates_.constEnd(); ++it) {
        if (sessions_.contains(it.key()))
            continue;
        if (it.value() != Roster::ContactAccepted && !pendingRequests_.contains(it.key()))
            continue;

        reach(it.key());
    }
    announce(true);

}

// ------------------------------------------------------------------ messaging

bool PeerNode::requestContact(const QString &peerId, const QString &endpointHint,
                              const QString &message, QString *error)
{
    if (!running_) {
        if (error)
            *error = QString::fromLatin1("Meeru is not connected to the network yet");
        return false;
    }

    pendingRequests_.insert(peerId, message);
    if (!endpointHint.trimmed().isEmpty())
        pendingHints_.insert(peerId, endpointHint.trimmed());

    PeerSession *session = sessions_.value(peerId, 0);
    if (session && session->isEstablished()) {
        QJsonObject request;
        request.insert("type", QString::fromLatin1(kMsgTrustRequest));
        request.insert("displayName", profile_.displayName);
        request.insert("message", message);
        session->sendControl(request);
        pendingRequests_.remove(peerId);
        return true;
    }

    reach(peerId);
    return true;
}

void PeerNode::acceptContact(const QString &peerId)
{
    contactStates_.insert(peerId, Roster::ContactAccepted);

    PeerSession *session = sessions_.value(peerId, 0);
    if (!session || !session->isEstablished())
        return;

    QJsonObject accepted;
    accepted.insert("type", QString::fromLatin1(kMsgTrustAccepted));
    session->sendControl(accepted);

    sendProfile(session, true);
    sendPicture(session, QString::fromLatin1("avatar"));
    sendPicture(session, QString::fromLatin1("banner"));
}

void PeerNode::setMessageStore(MessageStore *store)
{
    messages_ = store;
}

void PeerNode::sendChat(PeerSession *session, const QString &conversationId, const Chat::Message &message)
{
    if (!session || !session->isEstablished())
        return;

    QJsonObject object;
    object.insert("type", QString::fromLatin1(kMsgChat));
    object.insert("conversation", conversationId);
    object.insert("id", message.id);
    object.insert("text", message.text);
    object.insert("kind", message.kind);
    object.insert("sentAtUtc", message.sentAtUtc.toString(Qt::ISODate));
    object.insert("authorName", profile_.displayName);
    session->sendControl(object);
}

void PeerNode::sendMessage(const QString &peerId, const QString &conversationId, const Chat::Message &message)
{
    PeerSession *session = sessions_.value(peerId, 0);
    if (session && session->isEstablished()) {
        sendChat(session, conversationId, message);
        if (messages_)
            messages_->setDelivery(conversationId, message.id, Chat::DeliverySent);
        return;
    }

    // Not connected: the message stays in the store as waiting, and reaching
    // out now means it goes as soon as they answer.
    reach(peerId);
}

void PeerNode::deliverWaiting(const QString &peerId, PeerSession *session)
{
    if (!messages_ || !session || !session->isEstablished())
        return;

    const QString conversationId = directConversationId(profile_.identityId, peerId);
    const QList<Chat::Message> waiting = messages_->waitingFor(conversationId);
    for (int i = 0; i < waiting.size(); ++i) {
        sendChat(session, conversationId, waiting.at(i));
        messages_->setDelivery(conversationId, waiting.at(i).id, Chat::DeliverySent);
    }
}

void PeerNode::requestHistory(const QString &peerId, const QString &conversationId)
{
    PeerSession *session = sessions_.value(peerId, 0);
    if (!session || !session->isEstablished() || !messages_)
        return;

    // Ask only for what we do not have. Whichever side is further ahead sends
    // the difference, which is what keeps a group in step after somebody has
    // been away.
    QJsonObject object;
    object.insert("type", QString::fromLatin1(kMsgHistoryRequest));
    object.insert("conversation", conversationId);
    object.insert("since", messages_->latestTime(conversationId).toString(Qt::ISODate));
    session->sendControl(object);
}

void PeerNode::sendProfile(PeerSession *session, bool withPictures)
{
    if (!session || !session->isEstablished())
        return;

    QJsonObject object;
    object.insert("type", QString::fromLatin1(kMsgProfile));
    object.insert("displayName", profile_.displayName);
    object.insert("presence", profile_.presence);
    object.insert("statusText", isAccepted(session->peerId()) ? statusText_ : QString());
    session->sendControl(object);

    if (withPictures) {
        QJsonObject request;
        request.insert("type", QString::fromLatin1(kMsgPictureRequest));
        session->sendControl(request);
    }
}

void PeerNode::sendPicture(PeerSession *session, const QString &kind)
{
    if (!session || !session->isEstablished() || !isAccepted(session->peerId()))
        return;

    const QString path = (kind == QLatin1String("banner")) ? bannerFile_ : avatarFile_;
    if (path.isEmpty())
        return;

    ImageStore store(paths_, profile_.identityId, kind);
    if (!store.hasImage())
        return;

    QFile file(store.filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    if (file.size() <= 0 || file.size() > kMaxPictureBytes)
        return;
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonObject header;
    header.insert("type", QString::fromLatin1(kMsgPicture));
    header.insert("kind", kind);
    header.insert("animated", store.isAnimated());
    header.insert("format", QFileInfo(store.filePath()).suffix().toLower());
    if (store.isAnimated()) {
        const QRect crop = store.cropRect();
        QJsonObject rect;
        rect.insert("x", crop.x());
        rect.insert("y", crop.y());
        rect.insert("width", crop.width());
        rect.insert("height", crop.height());
        header.insert("crop", rect);
    }
    session->sendPayload(header, bytes);
}

void PeerNode::storePicture(const QString &peerId, const QString &kind,
                            const QJsonObject &header, const QByteArray &blob)
{
    if (blob.isEmpty() || blob.size() > kMaxPictureBytes)
        return;
    if (kind != QLatin1String("avatar") && kind != QLatin1String("banner"))
        return;

    const QString directory = peerDirectory(paths_, profile_.identityId, peerId);
    if (!QDir().mkpath(directory))
        return;

    const bool animated = header.value("animated").toBool(false);
    QRect crop;
    if (animated) {
        const QJsonObject rect = header.value("crop").toObject();
        crop = QRect(rect.value("x").toInt(), rect.value("y").toInt(),
                     rect.value("width").toInt(), rect.value("height").toInt());
    }

    ImageStore store(directory, kind);
    QString error;
    if (!store.saveReceived(blob, animated, crop, &error))
        return;

    emit pictureReceived(peerId, kind);
}

void PeerNode::onSessionMessage(const QString &peerId, const QJsonObject &object, const QByteArray &blob)
{
    PeerSession *session = qobject_cast<PeerSession *>(sender());
    const QString type = object.value("type").toString();

    if (type == QLatin1String(kMsgProfile)) {
        emit profileReceived(peerId,
                             object.value("displayName").toString(),
                             object.value("presence").toString(),
                             object.value("statusText").toString());
        return;
    }

    if (type == QLatin1String(kMsgTrustRequest)) {
        if (isAccepted(peerId)) {
            // Already friends: treat it as a reconnection rather than a request.
            acceptContact(peerId);
            return;
        }
        emit trustRequestReceived(peerId,
                                  object.value("displayName").toString(),
                                  object.value("message").toString());
        return;
    }

    if (type == QLatin1String(kMsgTrustAccepted)) {
        contactStates_.insert(peerId, Roster::ContactAccepted);
        emit trustAccepted(peerId);
        if (session) {
            sendProfile(session, true);
            sendPicture(session, QString::fromLatin1("avatar"));
            sendPicture(session, QString::fromLatin1("banner"));
        }
        return;
    }

    if (type == QLatin1String(kMsgPictureRequest)) {
        if (session && isAccepted(peerId)) {
            sendPicture(session, QString::fromLatin1("avatar"));
            sendPicture(session, QString::fromLatin1("banner"));
        }
        return;
    }

    if (type == QLatin1String(kMsgChat)) {
        if (!isAccepted(peerId) || !messages_)
            return;   // strangers do not get to write in our history

        Chat::Message message;
        message.id = object.value("id").toString();
        message.conversationId = object.value("conversation").toString();
        message.authorId = peerId;
        message.authorName = object.value("authorName").toString();
        message.text = object.value("text").toString();
        message.kind = object.value("kind").toInt(Chat::KindText);
        message.delivery = Chat::DeliveryReceived;
        message.sentAtUtc = QDateTime::fromString(object.value("sentAtUtc").toString(), Qt::ISODate);

        if (message.text.size() > 8000 || message.id.isEmpty() || message.conversationId.isEmpty())
            return;
        if (!message.sentAtUtc.isValid())
            message.sentAtUtc = QDateTime::currentDateTimeUtc();

        const bool known = messages_->contains(message.conversationId, message.id);
        const Chat::Message stored = messages_->append(message);
        if (!stored.isValid())
            return;

        if (session) {
            QJsonObject ack;
            ack.insert("type", QString::fromLatin1(kMsgChatAck));
            ack.insert("conversation", message.conversationId);
            ack.insert("id", message.id);
            session->sendControl(ack);
        }

        if (!known)
            emit messageReceived(peerId, message.conversationId, stored);
        return;
    }

    if (type == QLatin1String(kMsgChatAck)) {
        if (!messages_)
            return;
        const QString conversationId = object.value("conversation").toString();
        const QString messageId = object.value("id").toString();
        messages_->setDelivery(conversationId, messageId, Chat::DeliveryAcknowledged);
        emit messageDelivered(conversationId, messageId);
        return;
    }

    if (type == QLatin1String(kMsgHistoryRequest)) {
        if (!isAccepted(peerId) || !messages_ || !session)
            return;

        const QString conversationId = object.value("conversation").toString();
        const QDateTime since = QDateTime::fromString(object.value("since").toString(), Qt::ISODate);
        const QList<Chat::Message> newer = messages_->since(conversationId, since);
        for (int i = 0; i < newer.size(); ++i)
            sendChat(session, conversationId, newer.at(i));
        return;
    }

    if (type == QLatin1String(kMsgTyping)) {
        if (isAccepted(peerId)) {
            emit typingChanged(peerId, object.value("conversation").toString(),
                               object.value("typing").toBool(false));
        }
        return;
    }

    if (type == QLatin1String(kMsgPicture)) {
        if (isAccepted(peerId))
            storePicture(peerId, object.value("kind").toString(), object, blob);
        return;
    }
}

void PeerNode::emitStatus()
{
    int online = 0;
    QHash<QString, PeerSession *>::const_iterator it = sessions_.constBegin();
    for (; it != sessions_.constEnd(); ++it) {
        if (it.value()->isEstablished())
            ++online;
    }

    if (!running_) {
        emit statusChanged(QString::fromLatin1("Offline"));
        return;
    }
    if (online == 0) {
        emit statusChanged(QString::fromLatin1("Listening on port %1").arg(listenPort_));
        return;
    }
    emit statusChanged(QString::fromLatin1("%1 contact%2 connected")
                           .arg(online).arg(online == 1 ? QString() : QString::fromLatin1("s")));
}
