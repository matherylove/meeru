#include "dht_directory.h"

#include <QHostAddress>
#include <QStringList>
#include <QTimer>

#include "bencode.h"
#include "std_ed25519.h"

namespace {

const char kSalt[] = "meeru-v1";
const int kRepublishSeconds = 45 * 60;    // items may expire after 2h; refresh well before
const int kMaxEndpoints = 4;
const int kLookupExpirySeconds = 90;

QByteArray compactEndpoint(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return QByteArray();

    const QHostAddress address(endpoint.left(colon));
    bool ok = false;
    const int port = endpoint.mid(colon + 1).toInt(&ok);
    if (!ok || port <= 0 || port > 65535)
        return QByteArray();
    if (address.isNull() || address.protocol() != QAbstractSocket::IPv4Protocol)
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

QString expandEndpoint(const QByteArray &compact)
{
    if (compact.size() != 6)
        return QString();
    const unsigned char *b = reinterpret_cast<const unsigned char *>(compact.constData());
    const quint32 ip = (static_cast<quint32>(b[0]) << 24) | (static_cast<quint32>(b[1]) << 16)
                     | (static_cast<quint32>(b[2]) << 8) | static_cast<quint32>(b[3]);
    const quint16 port = static_cast<quint16>((static_cast<quint16>(b[4]) << 8) | b[5]);
    if (port == 0)
        return QString();
    return QHostAddress(ip).toString() + QLatin1Char(':') + QString::number(port);
}

}

QByteArray DhtDirectory::recordSalt()
{
    return QByteArray(kSalt);
}

DhtDirectory::DhtDirectory(QObject *parent)
    : QObject(parent), node_(0), republish_(0), port_(0), lastSequence_(0)
{
}

DhtDirectory::~DhtDirectory()
{
    stop();
}

bool DhtDirectory::isRunning() const
{
    return node_ && node_->isRunning();
}

bool DhtDirectory::isReady() const
{
    return node_ && node_->isReady();
}

QString DhtDirectory::externalAddress() const
{
    return node_ ? node_->externalAddress() : QString();
}

int DhtDirectory::nodeCount() const
{
    return node_ ? node_->contactCount() : 0;
}

bool DhtDirectory::start(const QString &identityId, const IdentityMaterial &material,
                         quint16 port, QString *error)
{
    stop();

    publicKey_ = QByteArray::fromHex(identityId.toLatin1());
    if (publicKey_.size() != 32) {
        if (error)
            *error = QString::fromLatin1("This identity predates DHT support and cannot publish to it");
        return false;
    }

    signingKey_ = IdentityCrypto::dhtSigningKey(material);
    if (signingKey_.size() != 64) {
        if (error)
            *error = QString::fromLatin1("This identity's keys are not available");
        return false;
    }

    identityId_ = identityId;
    port_ = port;

    node_ = new DhtNode(this);
    connect(node_, SIGNAL(valueFound(QByteArray,QByteArray,QByteArray,qint64)),
            this, SLOT(onValueFound(QByteArray,QByteArray,QByteArray,qint64)));
    connect(node_, SIGNAL(lookupFinished(QByteArray,QByteArray,bool)),
            this, SLOT(onLookupFinished(QByteArray,QByteArray,bool)));
    connect(node_, SIGNAL(putFinished(QByteArray,QByteArray,int)),
            this, SLOT(onPutFinished(QByteArray,QByteArray,int)));
    connect(node_, SIGNAL(statusChanged(QString)), this, SLOT(onNodeStatus(QString)));
    connect(node_, SIGNAL(readyChanged(bool)), this, SLOT(onReadyChanged(bool)));

    if (!node_->start(0, error)) {
        delete node_;
        node_ = 0;
        return false;
    }

    republish_ = new QTimer(this);
    republish_->setInterval(kRepublishSeconds * 1000);
    connect(republish_, SIGNAL(timeout()), this, SLOT(onRepublish()));
    republish_->start();

    return true;
}

void DhtDirectory::stop()
{
    if (republish_) {
        republish_->stop();
        delete republish_;
        republish_ = 0;
    }
    if (node_) {
        node_->stop();
        delete node_;
        node_ = 0;
    }
    IdentityCrypto::wipe(&signingKey_);
    signingKey_.clear();
    lookups_.clear();
    status_.clear();
}

void DhtDirectory::setLocalEndpoints(const QStringList &endpoints)
{
    if (endpoints_ == endpoints)
        return;
    endpoints_ = endpoints;
    if (isReady())
        publish();
}

QByteArray DhtDirectory::buildRecord() const
{
    // Kept deliberately small: BEP 44 values must stay under 1000 bytes once
    // bencoded, and a fat record is a record no node wants to hold.
    QList<BencodeValue> addresses;
    for (int i = 0; i < endpoints_.size() && addresses.size() < kMaxEndpoints; ++i) {
        const QByteArray compact = compactEndpoint(endpoints_.at(i));
        if (!compact.isEmpty())
            addresses.append(BencodeValue::fromString(compact));
    }
    if (addresses.isEmpty())
        return QByteArray();

    QMap<QByteArray, BencodeValue> record;
    record.insert("a", BencodeValue::fromList(addresses));
    record.insert("p", BencodeValue::fromInt(port_));
    record.insert("v", BencodeValue::fromInt(1));
    return BencodeValue::fromDict(record).encode();
}

QStringList DhtDirectory::parseRecord(const QByteArray &value)
{
    QStringList endpoints;
    const BencodeValue record = Bencode::decode(value);
    if (!record.isValid() || record.kind() != BencodeValue::Dictionary)
        return endpoints;
    if (record.value("v").toInt() != 1)
        return endpoints;

    const BencodeValue addresses = record.value("a");
    if (addresses.kind() != BencodeValue::List)
        return endpoints;

    const QList<BencodeValue> items = addresses.toList();
    for (int i = 0; i < items.size() && endpoints.size() < kMaxEndpoints; ++i) {
        if (items.at(i).kind() != BencodeValue::String)
            continue;
        const QString endpoint = expandEndpoint(items.at(i).toByteArray());
        if (!endpoint.isEmpty())
            endpoints.append(endpoint);
    }
    return endpoints;
}

void DhtDirectory::publish()
{
    if (!node_ || !node_->isReady() || signingKey_.size() != 64)
        return;

    const QByteArray record = buildRecord();
    if (record.isEmpty())
        return;

    // The sequence number must only ever go up, so it is the wall clock in
    // seconds. A device whose clock jumps backwards would otherwise publish
    // an update the network refuses to accept.
    qint64 sequence = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000;
    if (sequence <= lastSequence_)
        sequence = lastSequence_ + 1;
    lastSequence_ = sequence;

    const QByteArray salt = recordSalt();
    const QByteArray signature = StdEd25519::sign(signingKey_, publicKey_,
                                                  DhtNode::signingBuffer(salt, sequence, record));
    if (signature.size() != 64)
        return;

    node_->put(publicKey_, salt, record, sequence, signature);
}

void DhtDirectory::locate(const QString &peerIdentityId)
{
    if (!node_ || !node_->isReady())
        return;

    const QByteArray key = QByteArray::fromHex(peerIdentityId.toLatin1());
    if (key.size() != 32 || key == publicKey_)
        return;

    lookups_.insert(peerIdentityId.toLower(), QDateTime::currentDateTimeUtc());
    node_->get(key, recordSalt());
}

void DhtDirectory::onValueFound(const QByteArray &publicKey, const QByteArray &salt,
                                const QByteArray &value, qint64 sequence)
{
    Q_UNUSED(sequence);
    if (salt != recordSalt())
        return;

    // The node already checked that this value is signed by this very key, so
    // what arrives here cannot be somebody else's record wearing this name.
    const QString peerId = QString::fromLatin1(publicKey.toHex());
    const QStringList endpoints = parseRecord(value);
    if (endpoints.isEmpty())
        return;

    lookups_.remove(peerId);
    emit peerLocated(peerId, endpoints);
}

void DhtDirectory::onLookupFinished(const QByteArray &publicKey, const QByteArray &salt, bool found)
{
    if (salt != recordSalt())
        return;
    const QString peerId = QString::fromLatin1(publicKey.toHex());
    if (!found && lookups_.contains(peerId)) {
        lookups_.remove(peerId);
        emit peerNotFound(peerId);
    }

    // Forget stale lookups so the table cannot grow without bound.
    const QList<QString> keys = lookups_.keys();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (int i = 0; i < keys.size(); ++i) {
        if (lookups_.value(keys.at(i)).secsTo(now) > kLookupExpirySeconds)
            lookups_.remove(keys.at(i));
    }
}

void DhtDirectory::onPutFinished(const QByteArray &publicKey, const QByteArray &salt, int storedOn)
{
    Q_UNUSED(publicKey);
    Q_UNUSED(salt);
    status_ = storedOn > 0
        ? QString::fromLatin1("Findable worldwide (published to %1 nodes)").arg(storedOn)
        : QString::fromLatin1("Could not publish where to find you");
    emit statusChanged(status_);
}

void DhtDirectory::onNodeStatus(const QString &summary)
{
    status_ = summary;
    emit statusChanged(summary);
}

void DhtDirectory::onReadyChanged(bool ready)
{
    if (ready)
        publish();
}

void DhtDirectory::onRepublish()
{
    publish();
}
