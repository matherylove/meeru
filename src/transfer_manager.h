#ifndef MEERU_TRANSFER_MANAGER_H
#define MEERU_TRANSFER_MANAGER_H

#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>

#include "message_store.h"

class PeerSession;

// Moves attached files between two people, in pieces, and only when asked.
//
// Sending a file announces it; nothing travels until the person on the other
// end presses receive. That is the rule you asked for, and it is also what
// keeps somebody from filling your disk with a video you never wanted. Once a
// file has arrived it stays in your own Meeru folder and opens with no network
// at all.
//
// Both people have to be online for the transfer itself: this carries the
// bytes over the same encrypted link as everything else, with nobody in the
// middle holding a copy.
class TransferManager : public QObject
{
    Q_OBJECT

public:
    TransferManager(MessageStore *messages, const QString &identityId, QObject *parent = 0);

    static int chunkSize();
    static qint64 maximumFileSize();

    // Sender side: remember where the original lives so it can be served.
    void registerOutgoing(const QString &fileId, const QString &sourcePath);

    // Receiver side: ask for it.
    bool beginReceive(const QString &conversationId, const QString &messageId,
                      const QString &peerId, PeerSession *session);

    // Wire handlers, called by PeerNode.
    void handleRequest(const QString &peerId, PeerSession *session, const QJsonObject &object);
    void handleChunk(const QString &peerId, PeerSession *session,
                     const QJsonObject &object, const QByteArray &blob);
    void abortFor(const QString &peerId);

signals:
    void progress(const QString &conversationId, const QString &messageId, qint64 received, qint64 total);
    void finished(const QString &conversationId, const QString &messageId, bool ok);

private:
    struct Incoming {
        QString conversationId;
        QString messageId;
        QString peerId;
        QString fileId;
        qint64 expected;
        qint64 received;
        QFile file;
    };

    void sendNextChunk(const QString &peerId, PeerSession *session, const QString &fileId, qint64 offset);
    void fail(Incoming *incoming);

    MessageStore *messages_;
    QString identityId_;
    QHash<QString, QString> outgoing_;                  // fileId -> path on disk
    QHash<QString, Incoming *> incoming_;               // fileId -> state
};

#endif
