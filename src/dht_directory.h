#ifndef MEERU_DHT_DIRECTORY_H
#define MEERU_DHT_DIRECTORY_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>

#include "dht_node.h"
#include "identity_crypto.h"

// Turns the DHT into an address book for Meeru identities.
//
// Each identity publishes, under its own public key, a small signed record
// saying where it can be reached right now. A contact who knows the ID can
// derive the same key, ask the network for that record, and check the
// signature before believing a word of it.
//
// What this costs in privacy is real and worth stating plainly: publishing
// puts your public key next to your current IP address on machines run by
// strangers. Anyone who knows your ID can see when you are online and roughly
// where. That is the price of being reachable without a server, and it is why
// this is opt-in rather than the default.
class DhtDirectory : public QObject
{
    Q_OBJECT

public:
    explicit DhtDirectory(QObject *parent = 0);
    ~DhtDirectory();

    bool start(const QString &identityId, const IdentityMaterial &material,
               quint16 port, QString *error = 0);
    void stop();
    bool isRunning() const;
    bool isReady() const;
    QString status() const { return status_; }
    QString externalAddress() const;

    // How many DHT nodes we know of: zero for a long stretch means UDP to the
    // internet is not getting through.
    int nodeCount() const;

    // Endpoints this device answers on, republished whenever they change.
    void setLocalEndpoints(const QStringList &endpoints);

    // Ask the network where a contact is. The answer arrives as a signal.
    void locate(const QString &peerIdentityId);

    static QByteArray recordSalt();

signals:
    void peerLocated(const QString &peerIdentityId, const QStringList &endpoints);
    void peerNotFound(const QString &peerIdentityId);
    void statusChanged(const QString &summary);

private slots:
    void onValueFound(const QByteArray &publicKey, const QByteArray &salt,
                      const QByteArray &value, qint64 sequence);
    void onLookupFinished(const QByteArray &publicKey, const QByteArray &salt, bool found);
    void onPutFinished(const QByteArray &publicKey, const QByteArray &salt, int storedOn);
    void onNodeStatus(const QString &summary);
    void onReadyChanged(bool ready);
    void onRepublish();

private:
    void publish();
    QByteArray buildRecord() const;
    static QStringList parseRecord(const QByteArray &value);

    DhtNode *node_;
    QTimer *republish_;
    QString identityId_;
    QByteArray publicKey_;
    QByteArray signingKey_;
    QStringList endpoints_;
    quint16 port_;
    QString status_;
    qint64 lastSequence_;
    QHash<QString, QDateTime> lookups_;
};

#endif
