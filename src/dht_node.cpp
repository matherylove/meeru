#include "dht_node.h"

#include <QHostInfo>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>

#include "crc32c.h"
#include "identity_crypto.h"
#include "sha1.h"
#include "std_ed25519.h"

namespace {

const int kBucketSize = 8;              // k, per BEP 5
const int kAlpha = 4;                   // concurrent queries per lookup
const int kMaxLookupNodes = 64;
const int kTransactionTimeoutMs = 9000;
const int kLookupTimeoutMs = 45000;
const int kMaintenanceMs = 3000;
const int kMaxDatagram = 4096;          // KRPC messages are small; anything larger is not for us
const int kMaxValueSize = 1000;         // BEP 44
const int kMaxSaltSize = 64;            // BEP 44
const int kRequiredAddressVotes = 3;    // agreeing, from separate nodes, before believing our IP
const int kMaxTransactions = 256;
const quint16 kDefaultBootstrapPort = 6881;

QByteArray compactAddress(const QHostAddress &address, quint16 port)
{
    if (address.protocol() != QAbstractSocket::IPv4Protocol)
        return QByteArray();
    const quint32 ip = address.toIPv4Address();
    QByteArray out;
    out.append(static_cast<char>((ip >> 24) & 0xFF));
    out.append(static_cast<char>((ip >> 16) & 0xFF));
    out.append(static_cast<char>((ip >> 8) & 0xFF));
    out.append(static_cast<char>(ip & 0xFF));
    out.append(static_cast<char>((port >> 8) & 0xFF));
    out.append(static_cast<char>(port & 0xFF));
    return out;
}

bool parseCompactAddress(const QByteArray &data, QHostAddress *address, quint16 *port)
{
    if (data.size() != 6 || !address || !port)
        return false;
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(data.constData());
    const quint32 ip = (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16)
                     | (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
    *address = QHostAddress(ip);
    *port = static_cast<quint16>((static_cast<quint16>(bytes[4]) << 8) | bytes[5]);
    return *port != 0;
}

}

DhtContact::DhtContact()
    : port(0), failures(0)
{
}

bool DhtContact::isValid() const
{
    return id.size() == 20 && port != 0 && !address.isNull();
}

QByteArray DhtContact::compact() const
{
    return id + compactAddress(address, port);
}

bool DhtContact::operator==(const DhtContact &other) const
{
    return id == other.id && address == other.address && port == other.port;
}

// ------------------------------------------------------------------ BEP 42

bool DhtNode::addressNeedsNoVerification(const QHostAddress &address)
{
    // The specification exempts these ranges, since a node ID cannot be tied
    // to an address that is not globally unique in the first place.
    if (address.protocol() != QAbstractSocket::IPv4Protocol)
        return true;
    const quint32 ip = address.toIPv4Address();
    if ((ip & 0xFF000000u) == 0x0A000000u) return true;   // 10.0.0.0/8
    if ((ip & 0xFFF00000u) == 0xAC100000u) return true;   // 172.16.0.0/12
    if ((ip & 0xFFFF0000u) == 0xC0A80000u) return true;   // 192.168.0.0/16
    if ((ip & 0xFFFF0000u) == 0xA9FE0000u) return true;   // 169.254.0.0/16
    if ((ip & 0xFF000000u) == 0x7F000000u) return true;   // 127.0.0.0/8
    return false;
}

QByteArray DhtNode::deriveNodeId(const QHostAddress &external, quint8 random)
{
    QByteArray id(20, '\0');
    unsigned char *out = reinterpret_cast<unsigned char *>(id.data());

    QByteArray entropy;
    IdentityCrypto::randomBytes(&entropy, 20);
    for (int i = 0; i < 20; ++i)
        out[i] = static_cast<unsigned char>(entropy.at(i));

    if (external.isNull() || external.protocol() != QAbstractSocket::IPv4Protocol) {
        out[19] = random;
        return id;
    }

    const quint32 ip = external.toIPv4Address();
    unsigned char masked[4];
    masked[0] = static_cast<unsigned char>((ip >> 24) & 0x03);
    masked[1] = static_cast<unsigned char>((ip >> 16) & 0x0F);
    masked[2] = static_cast<unsigned char>((ip >> 8) & 0x3F);
    masked[3] = static_cast<unsigned char>(ip & 0xFF);

    const unsigned char r = static_cast<unsigned char>(random & 0x7);
    masked[0] |= static_cast<unsigned char>(r << 5);

    const quint32 crc = Crc32c::compute(masked, 4);

    out[0] = static_cast<unsigned char>((crc >> 24) & 0xFF);
    out[1] = static_cast<unsigned char>((crc >> 16) & 0xFF);
    out[2] = static_cast<unsigned char>(((crc >> 8) & 0xF8) | (out[2] & 0x07));
    out[19] = random;
    return id;
}

bool DhtNode::nodeIdIsCompliant(const QByteArray &id, const QHostAddress &address)
{
    if (id.size() != 20)
        return false;
    if (addressNeedsNoVerification(address))
        return true;

    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(id.constData());
    const QByteArray expected = deriveNodeId(address, bytes[19]);
    const unsigned char *want = reinterpret_cast<const unsigned char *>(expected.constData());

    // The first 21 bits must match, and the last byte is the random seed.
    if (bytes[0] != want[0] || bytes[1] != want[1])
        return false;
    if ((bytes[2] & 0xF8) != (want[2] & 0xF8))
        return false;
    return true;
}

int DhtNode::distanceOrder(const QByteArray &a, const QByteArray &b)
{
    // Index of the most significant differing bit; smaller means closer.
    const int size = qMin(a.size(), b.size());
    for (int i = 0; i < size; ++i) {
        const unsigned char x = static_cast<unsigned char>(a.at(i)) ^ static_cast<unsigned char>(b.at(i));
        if (x == 0)
            continue;
        for (int bit = 7; bit >= 0; --bit) {
            if (x & (1 << bit))
                return i * 8 + (7 - bit);
        }
    }
    return size * 8;
}

QByteArray DhtNode::targetFor(const QByteArray &publicKey, const QByteArray &salt)
{
    return Sha1::hash(publicKey + salt);
}

QByteArray DhtNode::signingBuffer(const QByteArray &salt, qint64 sequence, const QByteArray &value)
{
    // Assembled by hand exactly as BEP 44 prescribes, never by serialising a
    // dictionary: the order and the lengths are part of what is signed.
    QByteArray buffer;
    if (!salt.isEmpty()) {
        buffer += "4:salt";
        buffer += QByteArray::number(salt.size());
        buffer += ":";
        buffer += salt;
    }
    buffer += "3:seqi";
    buffer += QByteArray::number(sequence);
    buffer += "e1:v";
    buffer += BencodeValue::fromString(value).encode();
    return buffer;
}

QStringList DhtNode::defaultBootstrapNodes()
{
    QStringList nodes;
    // The only third-party infrastructure Meeru touches: contacted once at
    // startup to learn a handful of ordinary nodes, then never again.
    nodes.append(QString::fromLatin1("router.bittorrent.com:6881"));
    nodes.append(QString::fromLatin1("dht.transmissionbt.com:6881"));
    nodes.append(QString::fromLatin1("router.utorrent.com:6881"));
    return nodes;
}

// --------------------------------------------------------------------- node

DhtNode::DhtNode(QObject *parent)
    : QObject(parent),
      socket_(0), maintenance_(0), port_(0), running_(false), ready_(false),
      nextLookupId_(1), nextTransaction_(1), nodeIdRandom_(0), nodeIdFromExternal_(false)
{
    for (int i = 0; i < 161; ++i)
        buckets_.append(QList<DhtContact>());
}

DhtNode::~DhtNode()
{
    stop();
}

bool DhtNode::start(quint16 port, QString *error)
{
    stop();

    QByteArray random;
    if (!IdentityCrypto::randomBytes(&random, 1)) {
        if (error)
            *error = QString::fromLatin1("No secure random source");
        return false;
    }
    nodeIdRandom_ = static_cast<quint8>(random.at(0));
    nodeId_ = deriveNodeId(QHostAddress(), nodeIdRandom_);
    nodeIdFromExternal_ = false;

    socket_ = new QUdpSocket(this);
    if (!socket_->bind(QHostAddress(QHostAddress::AnyIPv4), port,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        if (!socket_->bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0))) {
            if (error)
                *error = QString::fromLatin1("Could not open a UDP port for the DHT");
            delete socket_;
            socket_ = 0;
            return false;
        }
    }
    port_ = socket_->localPort();
    connect(socket_, SIGNAL(readyRead()), this, SLOT(onDatagram()));

    maintenance_ = new QTimer(this);
    maintenance_->setInterval(kMaintenanceMs);
    connect(maintenance_, SIGNAL(timeout()), this, SLOT(onMaintenance()));
    maintenance_->start();

    running_ = true;
    bootstrap();
    emit statusChanged(QString::fromLatin1("Joining the distributed network"));
    return true;
}

void DhtNode::stop()
{
    running_ = false;
    ready_ = false;

    if (maintenance_) {
        maintenance_->stop();
        delete maintenance_;
        maintenance_ = 0;
    }
    if (socket_) {
        socket_->close();
        delete socket_;
        socket_ = 0;
    }

    transactions_.clear();
    lookups_.clear();
    pendingPuts_.clear();
    addressVotes_.clear();
    addressVoters_.clear();
    bootstrapQueue_.clear();
    for (int i = 0; i < buckets_.size(); ++i)
        buckets_[i].clear();
}

bool DhtNode::isReady() const
{
    return ready_;
}

int DhtNode::contactCount() const
{
    int total = 0;
    for (int i = 0; i < buckets_.size(); ++i)
        total += buckets_.at(i).size();
    return total;
}

QString DhtNode::externalAddress() const
{
    return externalAddress_.isNull() ? QString() : externalAddress_.toString();
}

void DhtNode::bootstrap()
{
    const QStringList nodes = defaultBootstrapNodes();
    for (int i = 0; i < nodes.size(); ++i) {
        const QString entry = nodes.at(i);
        const int colon = entry.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString host = entry.left(colon);
        const quint16 hostPort = static_cast<quint16>(entry.mid(colon + 1).toInt());
        bootstrapQueue_.append(qMakePair(host, hostPort));
        QHostInfo::lookupHost(host, this, SLOT(onBootstrapResolved(QHostInfo)));
    }
}

void DhtNode::onBootstrapResolved(const QHostInfo &info)
{
    if (!running_ || info.error() != QHostInfo::NoError)
        return;

    // The bootstrap hosts are the one part of this that is somebody else's
    // infrastructure. They are contacted once to learn a handful of ordinary
    // nodes, and are not used again.
    quint16 port = kDefaultBootstrapPort;
    for (int i = 0; i < bootstrapQueue_.size(); ++i) {
        if (bootstrapQueue_.at(i).first == info.hostName()) {
            port = bootstrapQueue_.at(i).second;
            break;
        }
    }

    const QList<QHostAddress> addresses = info.addresses();
    for (int i = 0; i < addresses.size(); ++i) {
        if (addresses.at(i).protocol() != QAbstractSocket::IPv4Protocol)
            continue;

        // A bootstrap node's ID is unknown until it answers, so it is queried
        // directly rather than entered into the routing table on faith.
        DhtContact contact;
        contact.id = QByteArray(20, '\0');
        contact.address = addresses.at(i);
        contact.port = port;

        QMap<QByteArray, BencodeValue> arguments;
        arguments.insert("target", BencodeValue::fromString(nodeId_));
        sendQuery(contact, "find_node", arguments, nodeId_, 0);
    }
}

bool DhtNode::closerToTarget(const QByteArray &a, const QByteArray &b, const QByteArray &target)
{
    if (a.size() != 20 || b.size() != 20 || target.size() != 20)
        return false;
    for (int i = 0; i < 20; ++i) {
        const unsigned char da = static_cast<unsigned char>(a.at(i)) ^ static_cast<unsigned char>(target.at(i));
        const unsigned char db = static_cast<unsigned char>(b.at(i)) ^ static_cast<unsigned char>(target.at(i));
        if (da != db)
            return da < db;
    }
    return false;
}

void DhtNode::addContact(const DhtContact &contact)
{
    if (!contact.isValid() || contact.id == nodeId_)
        return;
    if (contact.address.isNull() || contact.port == 0)
        return;

    const int bucket = bucketFor(contact.id);
    if (bucket < 0 || bucket >= buckets_.size())
        return;

    QList<DhtContact> &list = buckets_[bucket];
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == contact.id) {
            list[i].address = contact.address;
            list[i].port = contact.port;
            list[i].lastSeen = QDateTime::currentDateTimeUtc();
            list[i].failures = 0;
            return;
        }
    }

    if (list.size() >= kBucketSize) {
        // Replace the worst entry only if it has actually been failing.
        int worst = -1;
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).failures > 0 && (worst < 0 || list.at(i).failures > list.at(worst).failures))
                worst = i;
        }
        if (worst < 0)
            return;
        list[worst] = contact;
        list[worst].lastSeen = QDateTime::currentDateTimeUtc();
        return;
    }

    DhtContact fresh = contact;
    fresh.lastSeen = QDateTime::currentDateTimeUtc();
    fresh.failures = 0;
    list.append(fresh);

    if (!ready_ && contactCount() >= kBucketSize) {
        ready_ = true;
        emit readyChanged(true);
        emit statusChanged(QString::fromLatin1("Connected to the distributed network"));
    }
}

int DhtNode::bucketFor(const QByteArray &id) const
{
    return distanceOrder(nodeId_, id);
}

QList<DhtContact> DhtNode::closestContacts(const QByteArray &target, int count) const
{
    QList<DhtContact> all;
    for (int i = 0; i < buckets_.size(); ++i)
        all += buckets_.at(i);

    // Selection sort on the full XOR distance; the table holds a few hundred
    // entries at most, so the simplicity is worth more than the asymptotics.
    QList<DhtContact> result;
    QList<bool> used;
    for (int i = 0; i < all.size(); ++i)
        used.append(false);

    for (int picked = 0; picked < count && picked < all.size(); ++picked) {
        int best = -1;
        for (int i = 0; i < all.size(); ++i) {
            if (used.at(i))
                continue;
            if (best < 0 || closerToTarget(all.at(i).id, all.at(best).id, target))
                best = i;
        }
        if (best < 0)
            break;
        used[best] = true;
        result.append(all.at(best));
    }
    return result;
}

// ---------------------------------------------------------------- messaging

void DhtNode::sendQuery(const DhtContact &peer, const QByteArray &method,
                        const QMap<QByteArray, BencodeValue> &arguments,
                        const QByteArray &target, int lookupId)
{
    if (!socket_ || !running_ || transactions_.size() >= kMaxTransactions)
        return;

    QByteArray transactionId(2, '\0');
    transactionId[0] = static_cast<char>((nextTransaction_ >> 8) & 0xFF);
    transactionId[1] = static_cast<char>(nextTransaction_ & 0xFF);
    ++nextTransaction_;

    QMap<QByteArray, BencodeValue> fullArguments = arguments;
    fullArguments.insert("id", BencodeValue::fromString(nodeId_));

    QMap<QByteArray, BencodeValue> message;
    message.insert("t", BencodeValue::fromString(transactionId));
    message.insert("y", BencodeValue::fromString("q"));
    message.insert("q", BencodeValue::fromString(method));
    message.insert("a", BencodeValue::fromDict(fullArguments));
    message.insert("v", BencodeValue::fromString("MU01"));
    // BEP 43: Meeru queries the network but does not serve it.
    message.insert("ro", BencodeValue::fromInt(1));

    Transaction transaction;
    transaction.id = transactionId;
    transaction.query = method;
    transaction.target = target;
    transaction.peer = peer;
    transaction.sentAt = QDateTime::currentDateTimeUtc();
    transaction.lookupId = lookupId;
    transactions_.insert(transactionId, transaction);

    const QByteArray datagram = BencodeValue::fromDict(message).encode();
    socket_->writeDatagram(datagram, peer.address, peer.port);
}

void DhtNode::onDatagram()
{
    while (socket_ && socket_->hasPendingDatagrams()) {
        const qint64 pending = socket_->pendingDatagramSize();
        QByteArray datagram;
        datagram.resize(static_cast<int>(qMin<qint64>(pending, kMaxDatagram)));

        QHostAddress from;
        quint16 fromPort = 0;
        const qint64 read = socket_->readDatagram(datagram.data(), datagram.size(), &from, &fromPort);
        if (read <= 0)
            continue;
        datagram.resize(static_cast<int>(read));

        // Anything that is not well-formed bencode is dropped without comment.
        const BencodeValue message = Bencode::decode(datagram);
        if (!message.isValid() || message.kind() != BencodeValue::Dictionary)
            continue;

        const QByteArray type = message.value("y").toByteArray();
        if (type == "r")
            handleResponse(message, from, fromPort);
        else if (type == "q")
            handleQuery(message, from, fromPort);
        else if (type == "e")
            handleError(message, message.value("t").toByteArray());
    }
}

void DhtNode::handleQuery(const BencodeValue &message, const QHostAddress &from, quint16 fromPort)
{
    // Read-only node: answer a ping so we are not seen as dead by peers we
    // asked, and decline everything else. Meeru does not store data for
    // strangers, which keeps this out of the business of being abused as a
    // storage or amplification node.
    if (!socket_)
        return;

    const QByteArray method = message.value("q").toByteArray();
    const QByteArray transactionId = message.value("t").toByteArray();
    if (transactionId.isEmpty() || transactionId.size() > 32)
        return;

    QMap<QByteArray, BencodeValue> reply;
    reply.insert("t", BencodeValue::fromString(transactionId));
    reply.insert("v", BencodeValue::fromString("MU01"));

    if (method == "ping") {
        QMap<QByteArray, BencodeValue> response;
        response.insert("id", BencodeValue::fromString(nodeId_));
        reply.insert("y", BencodeValue::fromString("r"));
        reply.insert("r", BencodeValue::fromDict(response));
    } else {
        QList<BencodeValue> error;
        error.append(BencodeValue::fromInt(204));
        error.append(BencodeValue::fromString("read-only node"));
        reply.insert("y", BencodeValue::fromString("e"));
        reply.insert("e", BencodeValue::fromList(error));
    }

    socket_->writeDatagram(BencodeValue::fromDict(reply).encode(), from, fromPort);
}

void DhtNode::handleError(const BencodeValue &message, const QByteArray &transactionId)
{
    Q_UNUSED(message);
    if (!transactions_.contains(transactionId))
        return;
    const Transaction transaction = transactions_.take(transactionId);
    if (transaction.lookupId > 0 && lookups_.contains(transaction.lookupId)) {
        Lookup &lookup = lookups_[transaction.lookupId];
        if (lookup.outstanding > 0)
            --lookup.outstanding;
        advanceLookup(transaction.lookupId);
    }
}

void DhtNode::handleResponse(const BencodeValue &message, const QHostAddress &from, quint16 fromPort)
{
    const QByteArray transactionId = message.value("t").toByteArray();
    if (!transactions_.contains(transactionId))
        return;   // unsolicited or already timed out: ignore

    const Transaction transaction = transactions_.take(transactionId);

    // The reply must come from the address the query went to. Without this a
    // third party that can guess a transaction id could answer for somebody.
    if (transaction.peer.address != from || transaction.peer.port != fromPort)
        return;

    const BencodeValue response = message.value("r");
    if (!response.isValid() || response.kind() != BencodeValue::Dictionary)
        return;

    const QByteArray peerId = response.value("id").toByteArray();
    if (peerId.size() != 20)
        return;

    DhtContact contact;
    contact.id = peerId;
    contact.address = from;
    contact.port = fromPort;
    addContact(contact);

    // BEP 42 bootstrapping: nodes report the address they see us at.
    const QByteArray reportedIp = message.value("ip").toByteArray();
    if (reportedIp.size() >= 4) {
        QHostAddress seen;
        quint16 seenPort = 0;
        if (parseCompactAddress(reportedIp.left(6), &seen, &seenPort) || reportedIp.size() == 4) {
            if (reportedIp.size() == 4) {
                const unsigned char *b = reinterpret_cast<const unsigned char *>(reportedIp.constData());
                seen = QHostAddress((static_cast<quint32>(b[0]) << 24) | (static_cast<quint32>(b[1]) << 16)
                                    | (static_cast<quint32>(b[2]) << 8) | static_cast<quint32>(b[3]));
            }
            considerExternalAddress(seen, peerId);
        }
    }

    // Learn about the nodes it suggested.
    const QByteArray nodes = response.value("nodes").toByteArray();
    QList<DhtContact> discovered;
    for (int offset = 0; offset + 26 <= nodes.size(); offset += 26) {
        DhtContact found;
        found.id = nodes.mid(offset, 20);
        QHostAddress address;
        quint16 port = 0;
        if (!parseCompactAddress(nodes.mid(offset + 20, 6), &address, &port))
            continue;
        if (addressNeedsNoVerification(address) && !address.isLoopback())
            continue;   // never chase a peer into a private range we cannot reach
        found.address = address;
        found.port = port;
        if (!found.isValid())
            continue;
        addContact(found);
        discovered.append(found);
    }

    if (transaction.lookupId <= 0 || !lookups_.contains(transaction.lookupId))
        return;

    Lookup &lookup = lookups_[transaction.lookupId];
    if (lookup.outstanding > 0)
        --lookup.outstanding;

    // Feed newly learnt nodes into the lookup, closest first.
    for (int i = 0; i < discovered.size(); ++i) {
        const DhtContact &found = discovered.at(i);
        if (lookup.queried.contains(found.id))
            continue;
        bool already = false;
        for (int j = 0; j < lookup.candidates.size(); ++j) {
            if (lookup.candidates.at(j).id == found.id) { already = true; break; }
        }
        if (already)
            continue;

        int position = lookup.candidates.size();
        for (int j = 0; j < lookup.candidates.size(); ++j) {
            if (closerToTarget(found.id, lookup.candidates.at(j).id, lookup.target)) {
                position = j;
                break;
            }
        }
        lookup.candidates.insert(position, found);
    }
    while (lookup.candidates.size() > kMaxLookupNodes)
        lookup.candidates.removeLast();

    // A write token means this node is willing to store for us, but we only
    // keep it if the node also plays by the BEP 42 rules.
    const QByteArray token = response.value("token").toByteArray();
    if (!token.isEmpty() && token.size() <= 64 && nodeIdIsCompliant(peerId, from)) {
        lookup.tokens.insert(peerId, token);
        bool known = false;
        for (int i = 0; i < lookup.withToken.size(); ++i) {
            if (lookup.withToken.at(i).id == peerId) { known = true; break; }
        }
        if (!known)
            lookup.withToken.append(contact);
    }

    // A stored value came back. Everything about it is checked before use.
    if (response.contains("v")) {
        const BencodeValue stored = response.value("v");
        const QByteArray key = response.value("k").toByteArray();
        const QByteArray signature = response.value("sig").toByteArray();
        const qint64 sequence = response.value("seq").toInt();
        const QByteArray encodedValue = stored.encode();

        const bool sane = key.size() == 32
                       && signature.size() == 64
                       && sequence >= 0
                       && encodedValue.size() <= kMaxValueSize
                       && stored.isValid();

        if (sane && key == lookup.publicKey
            && targetFor(key, lookup.salt) == lookup.target
            && StdEd25519::verify(signature, key,
                                  signingBuffer(lookup.salt, sequence, stored.toByteArray()))) {
            if (sequence > lookup.bestSequence) {
                lookup.bestSequence = sequence;
                lookup.found = true;
                emit valueFound(lookup.publicKey, lookup.salt, stored.toByteArray(), sequence);
            }
        }
    }

    advanceLookup(transaction.lookupId);
}

// ----------------------------------------------------------------- lookups

int DhtNode::startLookup(int kind, const QByteArray &target,
                         const QByteArray &publicKey, const QByteArray &salt)
{
    Lookup lookup;
    lookup.id = nextLookupId_++;
    lookup.kind = kind;
    lookup.publicKey = publicKey;
    lookup.salt = salt;
    lookup.target = target;
    lookup.found = false;
    lookup.bestSequence = -1;
    lookup.startedAt = QDateTime::currentDateTimeUtc();
    lookup.outstanding = 0;
    lookup.candidates = closestContacts(lookup.target, kMaxLookupNodes);

    lookups_.insert(lookup.id, lookup);
    advanceLookup(lookup.id);
    return lookup.id;
}

void DhtNode::advanceLookup(int lookupId)
{
    if (!lookups_.contains(lookupId))
        return;

    Lookup &lookup = lookups_[lookupId];

    if (lookup.startedAt.msecsTo(QDateTime::currentDateTimeUtc()) > kLookupTimeoutMs) {
        finishLookup(lookupId);
        return;
    }

    while (lookup.outstanding < kAlpha) {
        int next = -1;
        for (int i = 0; i < lookup.candidates.size(); ++i) {
            if (!lookup.queried.contains(lookup.candidates.at(i).id)) {
                next = i;
                break;
            }
        }
        if (next < 0)
            break;

        const DhtContact peer = lookup.candidates.at(next);
        lookup.queried.append(peer.id);

        QMap<QByteArray, BencodeValue> arguments;
        arguments.insert("target", BencodeValue::fromString(lookup.target));
        sendQuery(peer, lookup.kind == LookupDiscover ? "find_node" : "get",
                  arguments, lookup.target, lookupId);
        ++lookup.outstanding;
    }

    if (lookup.outstanding == 0)
        finishLookup(lookupId);
}

void DhtNode::finishLookup(int lookupId)
{
    if (!lookups_.contains(lookupId))
        return;

    const Lookup lookup = lookups_.take(lookupId);

    if (lookup.kind == LookupDiscover) {
        emit statusChanged(QString::fromLatin1("Connected to %1 nodes of the distributed network")
                               .arg(contactCount()));
        return;
    }

    if (lookup.kind == LookupPut && pendingPuts_.contains(lookupId)) {
        const PendingPut pending = pendingPuts_.take(lookupId);

        // Store on the closest nodes that both offered a token and derive
        // their node ID from their IP the way BEP 42 requires. Announcing to
        // nodes that ignore that rule is how an attacker collects writes.
        int stored = 0;
        for (int i = 0; i < lookup.withToken.size() && stored < kBucketSize; ++i) {
            const DhtContact &peer = lookup.withToken.at(i);
            const QByteArray token = lookup.tokens.value(peer.id);
            if (token.isEmpty())
                continue;

            QMap<QByteArray, BencodeValue> arguments;
            arguments.insert("k", BencodeValue::fromString(pending.publicKey));
            if (!pending.salt.isEmpty())
                arguments.insert("salt", BencodeValue::fromString(pending.salt));
            arguments.insert("seq", BencodeValue::fromInt(pending.sequence));
            arguments.insert("sig", BencodeValue::fromString(pending.signature));
            arguments.insert("token", BencodeValue::fromString(token));
            arguments.insert("v", BencodeValue::fromString(pending.value));

            sendQuery(peer, "put", arguments, lookup.target, 0);
            ++stored;
        }
        emit putFinished(pending.publicKey, pending.salt, stored);
        return;
    }

    emit lookupFinished(lookup.publicKey, lookup.salt, lookup.found);
}

void DhtNode::startDiscovery()
{
    if (!running_ || contactCount() == 0)
        return;

    lastDiscovery_ = QDateTime::currentDateTimeUtc();
    startLookup(LookupDiscover, nodeId_, QByteArray(), QByteArray());
}

void DhtNode::get(const QByteArray &publicKey, const QByteArray &salt)
{
    if (!running_ || publicKey.size() != 32 || salt.size() > kMaxSaltSize)
        return;
    startLookup(LookupGet, targetFor(publicKey, salt), publicKey, salt);
}

void DhtNode::put(const QByteArray &publicKey, const QByteArray &salt,
                  const QByteArray &value, qint64 sequence, const QByteArray &signature)
{
    if (!running_ || publicKey.size() != 32 || signature.size() != 64)
        return;
    if (salt.size() > kMaxSaltSize || sequence < 0)
        return;
    if (BencodeValue::fromString(value).encode().size() > kMaxValueSize)
        return;

    // Never publish something we could not verify ourselves.
    if (!StdEd25519::verify(signature, publicKey, signingBuffer(salt, sequence, value)))
        return;

    const int lookupId = startLookup(LookupPut, targetFor(publicKey, salt), publicKey, salt);

    PendingPut pending;
    pending.publicKey = publicKey;
    pending.salt = salt;
    pending.value = value;
    pending.sequence = sequence;
    pending.signature = signature;
    pendingPuts_.insert(lookupId, pending);
}

// --------------------------------------------------------------- housekeeping

void DhtNode::considerExternalAddress(const QHostAddress &address, const QByteArray &fromNode)
{
    if (address.isNull() || addressNeedsNoVerification(address))
        return;
    if (nodeIdFromExternal_ && address == externalAddress_)
        return;
    if (addressVoters_.contains(fromNode))
        return;

    addressVoters_.append(fromNode);
    const QString key = address.toString();
    addressVotes_[key] = addressVotes_.value(key, 0) + 1;

    // One node saying so proves nothing; several, reached independently, do.
    if (addressVotes_.value(key) < kRequiredAddressVotes)
        return;
    if (nodeIdFromExternal_ && externalAddress_ == address)
        return;

    externalAddress_ = address;
    adoptNodeId();
}

void DhtNode::adoptNodeId()
{
    const QByteArray previous = nodeId_;
    nodeId_ = deriveNodeId(externalAddress_, nodeIdRandom_);
    nodeIdFromExternal_ = true;

    // The routing table is organised around our own ID, so it has to be
    // rebuilt when that ID changes.
    QList<DhtContact> all;
    for (int i = 0; i < buckets_.size(); ++i)
        all += buckets_.at(i);
    for (int i = 0; i < buckets_.size(); ++i)
        buckets_[i].clear();
    for (int i = 0; i < all.size(); ++i)
        addContact(all.at(i));

    if (previous != nodeId_)
        emit statusChanged(QString::fromLatin1("Reachable at %1").arg(externalAddress_.toString()));
}

void DhtNode::onMaintenance()
{
    if (!running_)
        return;

    const QDateTime now = QDateTime::currentDateTimeUtc();

    // Time out transactions and mark their peers as unreliable.
    const QList<QByteArray> keys = transactions_.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const Transaction transaction = transactions_.value(keys.at(i));
        if (transaction.sentAt.msecsTo(now) < kTransactionTimeoutMs)
            continue;

        transactions_.remove(keys.at(i));

        const int bucket = bucketFor(transaction.peer.id);
        if (bucket >= 0 && bucket < buckets_.size()) {
            QList<DhtContact> &list = buckets_[bucket];
            for (int j = 0; j < list.size(); ++j) {
                if (list.at(j).id == transaction.peer.id) {
                    ++list[j].failures;
                    if (list.at(j).failures >= 3)
                        list.removeAt(j);
                    break;
                }
            }
        }

        if (transaction.lookupId > 0 && lookups_.contains(transaction.lookupId)) {
            Lookup &lookup = lookups_[transaction.lookupId];
            if (lookup.outstanding > 0)
                --lookup.outstanding;
            advanceLookup(transaction.lookupId);
        }
    }

    // Close out lookups that have run long enough.
    const QList<int> lookupIds = lookups_.keys();
    for (int i = 0; i < lookupIds.size(); ++i) {
        if (!lookups_.contains(lookupIds.at(i)))
            continue;
        if (lookups_.value(lookupIds.at(i)).startedAt.msecsTo(now) > kLookupTimeoutMs)
            finishLookup(lookupIds.at(i));
    }

    if (ready_ && contactCount() == 0) {
        ready_ = false;
        emit readyChanged(false);
        bootstrap();
        return;
    }

    // Keep widening the table: often while joining, occasionally afterwards so
    // nodes that have gone away are replaced.
    bool discovering = false;
    QHash<int, Lookup>::const_iterator lookup = lookups_.constBegin();
    for (; lookup != lookups_.constEnd(); ++lookup) {
        if (lookup.value().kind == LookupDiscover) {
            discovering = true;
            break;
        }
    }

    if (!discovering && contactCount() > 0) {
        const int waitSeconds = ready_ ? 300 : 5;
        if (!lastDiscovery_.isValid() || lastDiscovery_.secsTo(now) >= waitSeconds)
            startDiscovery();
    }
}
