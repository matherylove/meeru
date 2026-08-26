#include "transfer_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "peer_session.h"

namespace {

const int kChunkSize = 48 * 1024;             // comfortably inside a session frame
const qint64 kMaximumFileSize = Q_INT64_C(4) * 1024 * 1024 * 1024;

// A name that arrived over the network is never used as a path. Only the plain
// file name survives, with anything that could climb out of the folder removed.
QString safeFileName(const QString &proposed)
{
    QString name = QFileInfo(proposed).fileName();
    name.remove(QLatin1Char('\\'));
    name.remove(QLatin1Char('/'));
    name.remove(QLatin1Char(':'));
    name.remove(QLatin1Char('*'));
    name.remove(QLatin1Char('?'));
    name.remove(QLatin1Char('"'));
    name.remove(QLatin1Char('<'));
    name.remove(QLatin1Char('>'));
    name.remove(QLatin1Char('|'));
    name = name.trimmed();
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    if (name.isEmpty())
        name = QString::fromLatin1("attachment");
    return name.left(120);
}

}

int TransferManager::chunkSize()
{
    return kChunkSize;
}

qint64 TransferManager::maximumFileSize()
{
    return kMaximumFileSize;
}

TransferManager::TransferManager(MessageStore *messages, const QString &identityId, QObject *parent)
    : QObject(parent), messages_(messages), identityId_(identityId)
{
}

void TransferManager::registerOutgoing(const QString &fileId, const QString &sourcePath)
{
    if (!fileId.isEmpty() && QFile::exists(sourcePath))
        outgoing_.insert(fileId, sourcePath);
}

bool TransferManager::beginReceive(const QString &conversationId, const QString &messageId,
                                   const QString &peerId, PeerSession *session)
{
    if (!messages_ || !session || !session->isEstablished())
        return false;

    const Chat::Message message = messages_->message(conversationId, messageId);
    if (!message.attachment.isValid())
        return false;
    if (message.attachment.transfer == Chat::TransferRunning)
        return true;
    if (message.attachment.fileSize > kMaximumFileSize)
        return false;

    const QString directory = messages_->attachmentDirectory() + QLatin1Char('/') + message.attachment.fileId;
    if (!QDir().mkpath(directory))
        return false;

    Incoming *incoming = new Incoming;
    incoming->conversationId = conversationId;
    incoming->messageId = messageId;
    incoming->peerId = peerId;
    incoming->fileId = message.attachment.fileId;
    incoming->expected = message.attachment.fileSize;
    incoming->received = 0;
    incoming->file.setFileName(directory + QLatin1Char('/') + safeFileName(message.attachment.fileName));

    if (!incoming->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete incoming;
        return false;
    }

    delete incoming_.take(incoming->fileId);
    incoming_.insert(incoming->fileId, incoming);
    messages_->setTransfer(conversationId, messageId, Chat::TransferRunning, 0);

    QJsonObject request;
    request.insert("type", QString::fromLatin1("file-request"));
    request.insert("fileId", incoming->fileId);
    request.insert("offset", static_cast<double>(0));
    session->sendControl(request);
    return true;
}

void TransferManager::handleRequest(const QString &peerId, PeerSession *session, const QJsonObject &object)
{
    Q_UNUSED(peerId);
    const QString fileId = object.value("fileId").toString();
    const qint64 offset = static_cast<qint64>(object.value("offset").toDouble());
    if (!outgoing_.contains(fileId) || offset < 0)
        return;
    sendNextChunk(peerId, session, fileId, offset);
}

void TransferManager::sendNextChunk(const QString &peerId, PeerSession *session,
                                    const QString &fileId, qint64 offset)
{
    Q_UNUSED(peerId);
    if (!session || !session->isEstablished())
        return;

    QFile file(outgoing_.value(fileId));
    if (!file.open(QIODevice::ReadOnly))
        return;
    if (offset > file.size()) {
        file.close();
        return;
    }

    file.seek(offset);
    const QByteArray chunk = file.read(kChunkSize);
    const qint64 total = file.size();
    file.close();

    QJsonObject header;
    header.insert("type", QString::fromLatin1("file-chunk"));
    header.insert("fileId", fileId);
    header.insert("offset", static_cast<double>(offset));
    header.insert("total", static_cast<double>(total));
    header.insert("last", offset + chunk.size() >= total);
    session->sendPayload(header, chunk);
}

void TransferManager::handleChunk(const QString &peerId, PeerSession *session,
                                  const QJsonObject &object, const QByteArray &blob)
{
    const QString fileId = object.value("fileId").toString();
    Incoming *incoming = incoming_.value(fileId, 0);
    if (!incoming || incoming->peerId != peerId)
        return;

    const qint64 offset = static_cast<qint64>(object.value("offset").toDouble());
    if (offset != incoming->received) {
        fail(incoming);   // out of order means something is wrong; start over rather than corrupt
        return;
    }
    if (incoming->received + blob.size() > incoming->expected) {
        fail(incoming);   // more than was announced
        return;
    }

    if (incoming->file.write(blob) != blob.size()) {
        fail(incoming);
        return;
    }
    incoming->received += blob.size();

    if (messages_) {
        messages_->setTransfer(incoming->conversationId, incoming->messageId,
                               Chat::TransferRunning, incoming->received);
    }
    emit progress(incoming->conversationId, incoming->messageId, incoming->received, incoming->expected);

    if (object.value("last").toBool(false) || incoming->received >= incoming->expected) {
        incoming->file.close();
        const bool complete = incoming->received == incoming->expected;

        if (messages_) {
            messages_->setTransfer(incoming->conversationId, incoming->messageId,
                                   complete ? Chat::TransferComplete : Chat::TransferFailed,
                                   incoming->received,
                                   complete ? incoming->file.fileName() : QString());
        }
        emit finished(incoming->conversationId, incoming->messageId, complete);

        delete incoming_.take(fileId);
        return;
    }

    // Ask for the next piece. Pulling rather than pushing keeps the receiver
    // in control of the pace and makes a broken transfer easy to resume.
    if (session && session->isEstablished()) {
        QJsonObject request;
        request.insert("type", QString::fromLatin1("file-request"));
        request.insert("fileId", fileId);
        request.insert("offset", static_cast<double>(incoming->received));
        session->sendControl(request);
    }
}

void TransferManager::fail(Incoming *incoming)
{
    if (!incoming)
        return;
    incoming->file.close();
    incoming->file.remove();
    if (messages_) {
        messages_->setTransfer(incoming->conversationId, incoming->messageId,
                               Chat::TransferFailed, 0);
    }
    emit finished(incoming->conversationId, incoming->messageId, false);
    delete incoming_.take(incoming->fileId);
}

void TransferManager::abortFor(const QString &peerId)
{
    const QList<QString> keys = incoming_.keys();
    for (int i = 0; i < keys.size(); ++i) {
        Incoming *incoming = incoming_.value(keys.at(i), 0);
        if (incoming && incoming->peerId == peerId)
            fail(incoming);
    }
}
