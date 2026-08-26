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
    KindSystem = 1,
    KindFile = 2,      // anything attached, including pictures and video
    KindPoll = 3
};

// A file is announced first and only travels once the receiver asks for it,
// so nobody has somebody else's holiday video pushed onto their disk.
enum Transfer {
    TransferOffered = 0,
    TransferRequested = 1,
    TransferRunning = 2,
    TransferComplete = 3,
    TransferFailed = 4
};

// What an attachment is, which decides where it shows up in the media, files
// and links sections later on.
enum Media {
    MediaOther = 0,
    MediaImage = 1,
    MediaVideo = 2,
    MediaAnimation = 3,
    MediaAudio = 4,
    MediaDocument = 5
};

struct Attachment
{
    Attachment();

    QString fileId;        // 32 hex, chosen by the sender
    QString fileName;
    qint64 fileSize;
    int media;
    int transfer;
    QString localPath;     // filled in once it is here
    qint64 received;

    bool isValid() const;
    static int mediaForName(const QString &fileName);
};

struct PollOption
{
    PollOption() : votes(0) {}
    QString text;
    int votes;
};

struct Poll
{
    Poll();
    QString question;
    QList<PollOption> options;
    QDateTime closesAtUtc;
    int myVote;            // -1 when not voted

    bool isValid() const;
    bool isClosed(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
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
    Attachment attachment;
    Poll poll;

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
    void setTransfer(const QString &conversationId, const QString &messageId,
                     int transfer, qint64 received, const QString &localPath = QString());
    void setVote(const QString &conversationId, const QString &messageId, int option);

    Chat::Message message(const QString &conversationId, const QString &messageId) const;
    QString attachmentDirectory() const;

    // Everything still waiting to be handed to a particular contact.
    QList<Chat::Message> waitingFor(const QString &conversationId) const;

    // Messages newer than a moment, for catching a peer up.
    QList<Chat::Message> since(const QString &conversationId, const QDateTime &moment, int limit = 200) const;

signals:
    void messageAdded(const QString &conversationId, const Chat::Message &message);
    void deliveryChanged(const QString &conversationId, const QString &messageId, int delivery);
    void transferChanged(const QString &conversationId, const QString &messageId);

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
