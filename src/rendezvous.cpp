#include "rendezvous.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>


namespace {

const quint16 kDefaultPort = 47450;
const int kRetryMs = 15000;
const int kKeepAliveMs = 30000;
const int kChannelLifetimeMs = 60000;
const int kMaxControlFrame = 64 * 1024;

void appendU32(QByteArray *out, quint32 value)
{
    char buffer[4];
    buffer[0] = static_cast<char>(value & 0xFF);
    buffer[1] = static_cast<char>((value >> 8) & 0xFF);
    buffer[2] = static_cast<char>((value >> 16) & 0xFF);
    buffer[3] = static_cast<char>((value >> 24) & 0xFF);
    out->append(buffer, 4);
}

quint32 readU32(const QByteArray &in, int offset)
{
    const unsigned char *data = reinterpret_cast<const unsigned char *>(in.constData()) + offset;
    return static_cast<quint32>(data[0])
         | (static_cast<quint32>(data[1]) << 8)
         | (static_cast<quint32>(data[2]) << 16)
         | (static_cast<quint32>(data[3]) << 24);
}

QStringList toStringList(const QJsonArray &array)
{
    QStringList values;
    for (int i = 0; i < array.size(); ++i) {
        const QString value = array.at(i).toString();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

QJsonArray toArray(const QStringList &values)
{
    QJsonArray array;
    for (int i = 0; i < values.size(); ++i)
        array.append(values.at(i));
    return array;
}

// A Meeru ID is 64 lowercase hex characters. Checked here rather than through
// the roster so the server can be built without any of the app's other parts.
bool isIdentityId(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (int i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool lower = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        if (!digit && !lower)
            return false;
    }
    return true;
}

QString addressOf(QTcpSocket *socket)
{
    if (!socket)
        return QString();
    return socket->peerAddress().toString() + QLatin1Char(':') + QString::number(socket->peerPort());
}

}

quint16 Rendezvous::defaultPort()
{
    return kDefaultPort;
}

QByteArray Rendezvous::frame(const QJsonObject &object)
{
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray out;
    appendU32(&out, static_cast<quint32>(payload.size()));
    out.append(payload);
    return out;
}

QString Rendezvous::registrationChallenge(const QByteArray &nonce)
{
    return QString::fromLatin1("meeru-rendezvous-v1:") + QString::fromLatin1(nonce.toHex());
}

// --------------------------------------------------------------------- client

RendezvousClient::RendezvousClient(QObject *parent)
    : QObject(parent),
      hostIndex_(0),
      listenPort_(0),
      running_(false),
      registered_(false),
      control_(0),
      retry_(0),
      keepAlive_(0)
{
    retry_ = new QTimer(this);
    retry_->setInterval(kRetryMs);
    connect(retry_, SIGNAL(timeout()), this, SLOT(onRetryTick()));

    keepAlive_ = new QTimer(this);
    keepAlive_->setInterval(kKeepAliveMs);
    connect(keepAlive_, SIGNAL(timeout()), this, SLOT(onKeepAliveTick()));
}

RendezvousClient::~RendezvousClient()
{
    stop();
    material_.clear();
}

bool RendezvousClient::isConnected() const
{
    return registered_;
}

void RendezvousClient::start(const QString &identityId, const IdentityMaterial &material,
                             const QStringList &hosts, quint16 listenPort)
{
    stop();

    identityId_ = identityId;
    material_ = material;
    listenPort_ = listenPort;
    hosts_.clear();

    for (int i = 0; i < hosts.size(); ++i) {
        QString entry = hosts.at(i).trimmed();
        if (entry.isEmpty())
            continue;
        Host host;
        host.port = kDefaultPort;
        const int colon = entry.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            bool ok = false;
            const int number = entry.mid(colon + 1).toInt(&ok);
            if (ok && number > 0 && number < 65536) {
                host.port = static_cast<quint16>(number);
                entry = entry.left(colon);
            }
        }
        host.host = entry.trimmed();
        if (!host.host.isEmpty())
            hosts_.append(host);
    }

    if (hosts_.isEmpty()) {
        emit statusChanged(QString::fromLatin1("No rendezvous node configured"));
        return;
    }

    running_ = true;
    hostIndex_ = 0;
    connectToHost();
    retry_->start();
    keepAlive_->start();
}

void RendezvousClient::stop()
{
    running_ = false;
    registered_ = false;
    if (retry_)
        retry_->stop();
    if (keepAlive_)
        keepAlive_->stop();

    if (control_) {
        control_->disconnect(this);
        control_->abort();
        control_->deleteLater();
        control_ = 0;
    }

    QHash<QTcpSocket *, QString>::const_iterator it = pendingRelays_.constBegin();
    for (; it != pendingRelays_.constEnd(); ++it) {
        it.key()->disconnect(this);
        it.key()->deleteLater();
    }
    pendingRelays_.clear();
    inbox_.clear();
}

void RendezvousClient::setLocalEndpoints(const QStringList &endpoints)
{
    endpoints_ = endpoints;
    if (registered_) {
        QJsonObject object;
        object.insert("type", QString::fromLatin1("endpoints"));
        object.insert("endpoints", toArray(endpoints_));
        sendControl(object);
    }
}

void RendezvousClient::connectToHost()
{
    if (!running_ || hosts_.isEmpty() || control_)
        return;

    const Host host = hosts_.at(hostIndex_ % hosts_.size());
    control_ = new QTcpSocket(this);
    connect(control_, SIGNAL(connected()), this, SLOT(onControlConnected()));
    connect(control_, SIGNAL(readyRead()), this, SLOT(onControlReadyRead()));
    connect(control_, SIGNAL(disconnected()), this, SLOT(onControlDisconnected()));
    connect(control_, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onControlDisconnected()));
    control_->connectToHost(host.host, host.port);
    emit statusChanged(QString::fromLatin1("Contacting %1").arg(host.host));
}

void RendezvousClient::onControlConnected()
{
    // The node challenges us first; nothing to send until then.
    emit statusChanged(QString::fromLatin1("Registering with the rendezvous node"));
}

void RendezvousClient::onControlDisconnected()
{
    registered_ = false;
    if (control_) {
        control_->disconnect(this);
        control_->deleteLater();
        control_ = 0;
    }
    inbox_.clear();
    ++hostIndex_;
    if (running_)
        emit statusChanged(QString::fromLatin1("Not reachable from outside your network"));
}

void RendezvousClient::onRetryTick()
{
    if (running_ && !control_)
        connectToHost();
}

void RendezvousClient::onKeepAliveTick()
{
    if (!registered_)
        return;
    QJsonObject object;
    object.insert("type", QString::fromLatin1("ping"));
    sendControl(object);
}

void RendezvousClient::sendControl(const QJsonObject &object)
{
    if (control_ && control_->state() == QAbstractSocket::ConnectedState)
        control_->write(Rendezvous::frame(object));
}

void RendezvousClient::onControlReadyRead()
{
    if (!control_)
        return;
    inbox_.append(control_->readAll());

    while (inbox_.size() >= 4) {
        const quint32 length = readU32(inbox_, 0);
        if (length > static_cast<quint32>(kMaxControlFrame)) {
            onControlDisconnected();
            return;
        }
        if (inbox_.size() < static_cast<int>(length) + 4)
            return;
        const QByteArray payload = inbox_.mid(4, static_cast<int>(length));
        inbox_.remove(0, 4 + static_cast<int>(length));
        handleControl(QJsonDocument::fromJson(payload).object());
    }
}

void RendezvousClient::handleControl(const QJsonObject &object)
{
    const QString type = object.value("type").toString();

    if (type == QLatin1String("challenge")) {
        const QByteArray nonce = QByteArray::fromHex(object.value("nonce").toString().toLatin1());
        const QByteArray message = Rendezvous::registrationChallenge(nonce).toUtf8();
        const QByteArray signature = IdentityCrypto::profileSignature(material_, message);

        QJsonObject reply;
        reply.insert("type", QString::fromLatin1("register"));
        reply.insert("id", identityId_);
        reply.insert("edPublic", QString::fromLatin1(material_.edPublic.toBase64()));
        reply.insert("signature", QString::fromLatin1(signature.toBase64()));
        reply.insert("port", static_cast<int>(listenPort_));
        reply.insert("endpoints", toArray(endpoints_));
        sendControl(reply);
        return;
    }

    if (type == QLatin1String("registered")) {
        registered_ = true;
        observedAddress_ = object.value("address").toString();
        emit statusChanged(QString::fromLatin1("Reachable through the rendezvous node"));
        return;
    }

    if (type == QLatin1String("relay-ready")) {
        const QString peerId = object.value("peer").toString();
        const QStringList candidates = toStringList(object.value("endpoints").toArray());
        if (!candidates.isEmpty())
            emit directCandidates(peerId, candidates);
        openRelay(object.value("channel").toString(), peerId, true);
        return;
    }

    if (type == QLatin1String("relay-request")) {
        openRelay(object.value("channel").toString(), object.value("from").toString(), false);
        return;
    }

    if (type == QLatin1String("error")) {
        emit peerUnreachable(object.value("peer").toString(), object.value("reason").toString());
        return;
    }
}

void RendezvousClient::requestConnection(const QString &peerId)
{
    if (!registered_) {
        emit peerUnreachable(peerId, QString::fromLatin1("no rendezvous node available"));
        return;
    }
    QJsonObject object;
    object.insert("type", QString::fromLatin1("connect"));
    object.insert("to", peerId);
    sendControl(object);
}

void RendezvousClient::openRelay(const QString &channel, const QString &peerId, bool initiator)
{
    if (channel.isEmpty() || peerId.isEmpty() || hosts_.isEmpty())
        return;

    const Host host = hosts_.at(hostIndex_ % hosts_.size());
    QTcpSocket *socket = new QTcpSocket(this);
    socket->setProperty("channel", channel);
    socket->setProperty("initiator", initiator);
    pendingRelays_.insert(socket, peerId);

    connect(socket, SIGNAL(connected()), this, SLOT(onRelayConnected()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onRelayError()));
    socket->connectToHost(host.host, host.port);
}

void RendezvousClient::onRelayConnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    const QString peerId = pendingRelays_.take(socket);
    const bool initiator = socket->property("initiator").toBool();

    QJsonObject object;
    object.insert("type", QString::fromLatin1("relay"));
    object.insert("channel", socket->property("channel").toString());
    object.insert("side", initiator ? QString::fromLatin1("caller") : QString::fromLatin1("callee"));
    socket->write(Rendezvous::frame(object));

    // From here the node only shuffles bytes: the peers authenticate and
    // encrypt end to end over this pipe.
    socket->disconnect(this);
    emit relayedSocket(peerId, socket, initiator);
}

void RendezvousClient::onRelayError()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    const QString peerId = pendingRelays_.take(socket);
    socket->deleteLater();
    if (!peerId.isEmpty())
        emit peerUnreachable(peerId, QString::fromLatin1("the relay could not be opened"));
}

// --------------------------------------------------------------------- server

RendezvousServer::RendezvousServer(QObject *parent)
    : QObject(parent), server_(0), sweep_(0)
{
}

quint16 RendezvousServer::port() const
{
    return server_ ? server_->serverPort() : 0;
}

bool RendezvousServer::listen(quint16 port, QString *error)
{
    delete server_;
    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::Any, port)) {
        if (error)
            *error = server_->errorString();
        delete server_;
        server_ = 0;
        return false;
    }
    connect(server_, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    if (!sweep_) {
        sweep_ = new QTimer(this);
        sweep_->setInterval(kChannelLifetimeMs);
        connect(sweep_, SIGNAL(timeout()), this, SLOT(onSweep()));
        sweep_->start();
    }

    emit logMessage(QString::fromLatin1("Listening on port %1").arg(server_->serverPort()));
    return true;
}

void RendezvousServer::onNewConnection()
{
    while (server_ && server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        connect(socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
        connect(socket, SIGNAL(disconnected()), this, SLOT(onDisconnected()));

        QByteArray nonce;
        IdentityCrypto::randomBytes(&nonce, 16);
        challenges_.insert(socket, nonce);

        QJsonObject challenge;
        challenge.insert("type", QString::fromLatin1("challenge"));
        challenge.insert("nonce", QString::fromLatin1(nonce.toHex()));
        socket->write(Rendezvous::frame(challenge));
    }
}

void RendezvousServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    // Once paired, everything is forwarded untouched.
    QTcpSocket *partner = pipes_.value(socket, 0);
    if (partner) {
        pipe(socket, partner, socket->readAll());
        return;
    }

    QByteArray &inbox = inboxes_[socket];
    inbox.append(socket->readAll());

    while (inbox.size() >= 4) {
        const quint32 length = readU32(inbox, 0);
        if (length > static_cast<quint32>(kMaxControlFrame)) {
            dropSocket(socket);
            return;
        }
        if (inbox.size() < static_cast<int>(length) + 4)
            return;

        const QByteArray payload = inbox.mid(4, static_cast<int>(length));
        inbox.remove(0, 4 + static_cast<int>(length));
        handleFrame(socket, QJsonDocument::fromJson(payload).object());

        // handleFrame may have turned this into a pipe; the rest is data.
        QTcpSocket *paired = pipes_.value(socket, 0);
        if (paired || channelOf_.contains(socket)) {
            const QByteArray rest = inbox;
            inbox.clear();
            if (!rest.isEmpty()) {
                if (paired)
                    pipe(socket, paired, rest);
                else if (channels_.contains(channelOf_.value(socket))) {
                    Pending &pending = channels_[channelOf_.value(socket)];
                    if (pending.caller == socket)
                        pending.callerBuffer.append(rest);
                    else
                        pending.calleeBuffer.append(rest);
                }
            }
            return;
        }
    }
}

void RendezvousServer::handleFrame(QTcpSocket *socket, const QJsonObject &object)
{
    const QString type = object.value("type").toString();

    if (type == QLatin1String("register")) {
        const QString id = object.value("id").toString();
        const QByteArray edPublic = QByteArray::fromBase64(object.value("edPublic").toString().toLatin1());
        const QByteArray signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
        const QByteArray nonce = challenges_.value(socket);

        // An ID is the hash of a key, so this proves the caller owns the ID it
        // claims. Without it anyone could park on somebody else's address.
        const bool derived = isIdentityId(id)
                          && QString::fromLatin1(IdentityCrypto::deriveId("meeru-identity", edPublic)) == id;
        const QByteArray message = Rendezvous::registrationChallenge(nonce).toUtf8();

        if (!derived || nonce.isEmpty() || !IdentityCrypto::verifySignature(edPublic, message, signature)) {
            emit logMessage(QString::fromLatin1("Rejected a registration from %1").arg(addressOf(socket)));
            dropSocket(socket);
            return;
        }

        QTcpSocket *previous = registrations_.value(id, 0);
        if (previous && previous != socket)
            dropSocket(previous);

        identities_.insert(socket, id);
        registrations_.insert(id, socket);
        endpoints_.insert(id, toStringList(object.value("endpoints").toArray()));
        challenges_.remove(socket);

        QJsonObject reply;
        reply.insert("type", QString::fromLatin1("registered"));
        reply.insert("address", addressOf(socket));
        socket->write(Rendezvous::frame(reply));
        emit logMessage(QString::fromLatin1("%1 registered from %2").arg(id.left(12)).arg(addressOf(socket)));
        return;
    }

    if (type == QLatin1String("endpoints")) {
        const QString id = identities_.value(socket);
        if (!id.isEmpty())
            endpoints_.insert(id, toStringList(object.value("endpoints").toArray()));
        return;
    }

    if (type == QLatin1String("ping")) {
        return;
    }

    if (type == QLatin1String("connect")) {
        const QString from = identities_.value(socket);
        const QString to = object.value("to").toString();
        QTcpSocket *target = registrations_.value(to, 0);

        if (from.isEmpty() || !target) {
            QJsonObject error;
            error.insert("type", QString::fromLatin1("error"));
            error.insert("peer", to);
            error.insert("reason", QString::fromLatin1("that contact is not reachable right now"));
            socket->write(Rendezvous::frame(error));
            return;
        }

        QByteArray random;
        IdentityCrypto::randomBytes(&random, 12);
        const QString channel = QString::fromLatin1(random.toHex());

        Pending pending;
        pending.caller = 0;
        pending.callee = 0;
        pending.createdAt = QDateTime::currentMSecsSinceEpoch();
        channels_.insert(channel, pending);

        QJsonObject request;
        request.insert("type", QString::fromLatin1("relay-request"));
        request.insert("channel", channel);
        request.insert("from", from);
        target->write(Rendezvous::frame(request));

        QJsonObject ready;
        ready.insert("type", QString::fromLatin1("relay-ready"));
        ready.insert("channel", channel);
        ready.insert("peer", to);
        ready.insert("endpoints", toArray(endpoints_.value(to)));
        socket->write(Rendezvous::frame(ready));
        return;
    }

    if (type == QLatin1String("relay")) {
        const QString channel = object.value("channel").toString();
        if (!channels_.contains(channel)) {
            dropSocket(socket);
            return;
        }

        Pending &pending = channels_[channel];
        const bool caller = object.value("side").toString() == QLatin1String("caller");
        if (caller)
            pending.caller = socket;
        else
            pending.callee = socket;
        channelOf_.insert(socket, channel);

        if (pending.caller && pending.callee) {
            pipes_.insert(pending.caller, pending.callee);
            pipes_.insert(pending.callee, pending.caller);

            // Anything that arrived before the other side showed up.
            if (!pending.callerBuffer.isEmpty())
                pending.callee->write(pending.callerBuffer);
            if (!pending.calleeBuffer.isEmpty())
                pending.caller->write(pending.calleeBuffer);

            channels_.remove(channel);
            channelOf_.remove(pending.caller);
            channelOf_.remove(pending.callee);
            emit logMessage(QString::fromLatin1("Relaying a conversation"));
        }
        return;
    }
}

void RendezvousServer::pipe(QTcpSocket *from, QTcpSocket *to, const QByteArray &data)
{
    Q_UNUSED(from);
    if (to && to->state() == QAbstractSocket::ConnectedState && !data.isEmpty())
        to->write(data);
}

void RendezvousServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket)
        dropSocket(socket);
}

void RendezvousServer::dropSocket(QTcpSocket *socket)
{
    if (!socket)
        return;

    QTcpSocket *partner = pipes_.take(socket);
    if (partner) {
        pipes_.remove(partner);
        partner->disconnectFromHost();
        partner->deleteLater();
    }

    const QString channel = channelOf_.take(socket);
    if (!channel.isEmpty())
        channels_.remove(channel);

    const QString id = identities_.take(socket);
    if (!id.isEmpty() && registrations_.value(id) == socket) {
        registrations_.remove(id);
        endpoints_.remove(id);
        emit logMessage(QString::fromLatin1("%1 went away").arg(id.left(12)));
    }

    challenges_.remove(socket);
    inboxes_.remove(socket);

    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void RendezvousServer::onSweep()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QList<QString> keys = channels_.keys();
    for (int i = 0; i < keys.size(); ++i) {
        if (now - channels_.value(keys.at(i)).createdAt > kChannelLifetimeMs)
            channels_.remove(keys.at(i));
    }
}
