#ifndef MEERU_DHT_NODE_H
#define MEERU_DHT_NODE_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QHostInfo>
#include <QPair>
#include <QString>

#include "bencode.h"

class QTimer;
class QUdpSocket;

// A client for the BitTorrent mainline DHT (BEP 5) with mutable item support
// (BEP 44) and the node ID security extension (BEP 42).
//
// Meeru uses it as a public address book: each identity publishes, under its
// own public key, the addresses it currently answers on, signed so nobody can
// forge or alter the entry. A contact who knows only a Meeru ID can then find
// where that person is right now, with no server belonging to anyone.
//
// Deliberate design choices, all of them about limiting exposure:
//
//  * Read-only participation (BEP 43). Meeru queries the DHT but sets "ro":1
//    and does not store other people's data or hand out write tokens. A
//    messenger has no business being a storage node for strangers, and it
//    removes a large abuse surface at no cost to our own lookups.
//
//  * Nothing is trusted on arrival. A value is accepted only if its target
//    really is SHA1(key||salt) for the key we asked about, and only if the
//    signature verifies with standard Ed25519. A malicious node can withhold
//    or delay an answer, which is unavoidable, but it cannot fabricate one.
//
//  * Our own node ID follows BEP 42, derived from our external IP, and other
//    nodes must do the same before we will store anything on them. This is
//    what makes it expensive to surround a particular key with hostile nodes.
//
//  * The external IP is decided by agreement, not by whoever answers first:
//    several nodes, from separate queries, must report the same address
//    before it is believed.
struct DhtContact
{
    DhtContact();

    QByteArray id;          // 20 bytes
    QHostAddress address;
    quint16 port;
    QDateTime lastSeen;
    int failures;

    bool isValid() const;
    QByteArray compact() const;    // 26 byte "compact node info"
    bool operator==(const DhtContact &other) const;
};

class DhtNode : public QObject
{
    Q_OBJECT

public:
    explicit DhtNode(QObject *parent = 0);
    ~DhtNode();

    bool start(quint16 port, QString *error = 0);
    void stop();
    bool isRunning() const { return running_; }
    bool isReady() const;                       // enough contacts to do useful work
    int contactCount() const;
    QString externalAddress() const;

    // BEP 44 mutable items. The key is a standard Ed25519 public key; the
    // caller signs, so this class never sees a secret.
    void get(const QByteArray &publicKey, const QByteArray &salt);
    void put(const QByteArray &publicKey, const QByteArray &salt,
             const QByteArray &value, qint64 sequence, const QByteArray &signature);

    static QByteArray targetFor(const QByteArray &publicKey, const QByteArray &salt);
    static QByteArray signingBuffer(const QByteArray &salt, qint64 sequence, const QByteArray &value);
    static QStringList defaultBootstrapNodes();

    // Exposed for the audit tests.
    static QByteArray deriveNodeId(const QHostAddress &external, quint8 random);
    static bool nodeIdIsCompliant(const QByteArray &id, const QHostAddress &address);
    static bool addressNeedsNoVerification(const QHostAddress &address);
    static int distanceOrder(const QByteArray &a, const QByteArray &b);

    // True when a is strictly closer to target than b, comparing the full XOR
    // distance rather than only the first differing bit.
    static bool closerToTarget(const QByteArray &a, const QByteArray &b, const QByteArray &target);

signals:
    // Emitted for every verified value found for a key we asked about.
    void valueFound(const QByteArray &publicKey, const QByteArray &salt,
                    const QByteArray &value, qint64 sequence);
    void lookupFinished(const QByteArray &publicKey, const QByteArray &salt, bool found);
    void putFinished(const QByteArray &publicKey, const QByteArray &salt, int storedOn);
    void readyChanged(bool ready);
    void statusChanged(const QString &summary);

private slots:
    void onDatagram();
    void onMaintenance();
    void onBootstrapResolved(const QHostInfo &info);

private:
    struct Transaction {
        QByteArray id;
        QByteArray query;
        QByteArray target;
        DhtContact peer;
        QDateTime sentAt;
        int lookupId;
    };

    struct PendingPut {
        QByteArray publicKey;
        QByteArray salt;
        QByteArray value;
        qint64 sequence;
        QByteArray signature;
    };

    struct Lookup {
        int id;
        QByteArray target;
        QByteArray publicKey;
        QByteArray salt;
        bool forPut;
        bool found;
        qint64 bestSequence;
        QDateTime startedAt;
        QList<DhtContact> candidates;      // sorted by distance to target
        QList<QByteArray> queried;         // node ids already asked
        QHash<QByteArray, QByteArray> tokens;   // node id -> write token
        QList<DhtContact> withToken;       // compliant nodes that gave a token
        int outstanding;
    };

    void sendQuery(const DhtContact &peer, const QByteArray &method,
                   const QMap<QByteArray, BencodeValue> &arguments,
                   const QByteArray &target, int lookupId);
    void handleResponse(const BencodeValue &message, const QHostAddress &from, quint16 fromPort);
    void handleQuery(const BencodeValue &message, const QHostAddress &from, quint16 fromPort);
    void handleError(const BencodeValue &message, const QByteArray &transactionId);

    void addContact(const DhtContact &contact);
    QList<DhtContact> closestContacts(const QByteArray &target, int count) const;
    int bucketFor(const QByteArray &id) const;

    int startLookup(const QByteArray &publicKey, const QByteArray &salt, bool forPut);
    void advanceLookup(int lookupId);
    void finishLookup(int lookupId);
    void considerExternalAddress(const QHostAddress &address, const QByteArray &fromNode);
    void adoptNodeId();
    void bootstrap();

    QUdpSocket *socket_;
    QTimer *maintenance_;
    QByteArray nodeId_;
    quint16 port_;
    bool running_;
    bool ready_;

    QList<QList<DhtContact> > buckets_;     // 160 buckets, index = leading bit position
    QHash<QByteArray, Transaction> transactions_;
    QHash<int, Lookup> lookups_;
    QHash<int, PendingPut> pendingPuts_;
    int nextLookupId_;
    quint16 nextTransaction_;

    QHash<QString, int> addressVotes_;      // reported external address -> agreeing nodes
    QList<QByteArray> addressVoters_;       // node ids that already voted
    QHostAddress externalAddress_;
    quint8 nodeIdRandom_;
    bool nodeIdFromExternal_;

    QList<QPair<QString, quint16> > bootstrapQueue_;
};

#endif
