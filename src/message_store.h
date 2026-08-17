#ifndef MEERU_MESSAGE_STORE_H
#define MEERU_MESSAGE_STORE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "meeru_paths.h"

// Every message Meeru has ever seen for one identity, on disk.
//
// A conversation is a file. Messages carry an id, so the same message arriving
// twice is stored once, and a clock, so two peers can work out who is further
// ahead and hand over what the other is missing.
//
// Delivery follows the rule you would expect from a phone: a message written
// to somebody who is offline sits here marked as waiting, and leaves the moment
// they appear. Nothing is lost by closing the program.
namespace Chat {

enum Kind {
    KindText = 0,
    KindSystem = 1
};

enum Delivery {
    DeliveryWaiting = 0,    // written, nobody to hand it to yet
    DeliverySent = 1,       // handed over to the peer
    DeliveryAcknowledged = 2,
    DeliveryReceived = 3    // it came from them
};

struct Message
{
    Message();

    QString id;             // 32 hex, made by the author
    QString conversationId;
    QString authorId;       // Meeru ID; empty means the local user
    QString authorName;     // as known when it arrived
    QString text;
    int kind;
    int delivery;
    QDateTime sentAtUtc;

    bool isValid() const;
    bool isMine() const { return authorId.isEmpty(); }
};

QString newMessageId();

}

// Passed through string-based signal connections, which is exactly the case
// where Qt needs the type registered by name.
Q_DECLARE_METATYPE(Chat::Message)

class MessageStore : public QObject
{
    Q_OBJECT

public:
    MessageStore(const MeeruPaths &paths, const QString &identityId, QObject *parent = 0);

    QList<Chat::Message> history(const QString &conversationId, int limit = 500) const;
    int unreadCount(const QString &conversationId) const;
    void markRead(const QString &conversationId);
    QDateTime latestTime(const QString &conversationId) const;
    QString lastPreview(const QString &conversationId) const;

    // Returns the stored message, with its id filled in.
    Chat::Message append(const Chat::Message &message);
    bool contains(const QString &conversationId, const QString &messageId) const;

    void setDelivery(const QString &conversationId, const QString &messageId, int delivery);

    // Everything still waiting to be handed to a particular contact.
    QList<Chat::Message> waitingFor(const QString &conversationId) const;

    // Messages newer than a moment, for catching a peer up.
    QList<Chat::Message> since(const QString &conversationId, const QDateTime &moment, int limit = 200) const;

signals:
    void messageAdded(const QString &conversationId, const Chat::Message &message);
    void deliveryChanged(const QString &conversationId, const QString &messageId, int delivery);

private:
    QString filePath(const QString &conversationId) const;
    QList<Chat::Message> load(const QString &conversationId) const;
    bool save(const QString &conversationId, const QList<Chat::Message> &messages) const;

    MeeruPaths paths_;
    QString identityId_;
    mutable QHash<QString, QList<Chat::Message> > cache_;
    QHash<QString, QDateTime> readUpTo_;
};

#endif
