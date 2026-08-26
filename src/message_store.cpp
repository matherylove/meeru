#include "message_store.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "identity_crypto.h"

namespace {

const int kMaxStoredPerConversation = 5000;

QString isoOrEmpty(const QDateTime &value)
{
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

}

Chat::Attachment::Attachment()
    : fileSize(0), media(MediaOther), transfer(TransferOffered), received(0)
{
}

bool Chat::Attachment::isValid() const
{
    return !fileId.isEmpty() && !fileName.isEmpty() && fileSize > 0;
}

int Chat::Attachment::mediaForName(const QString &fileName)
{
    const QString suffix = fileName.section(QLatin1Char('.'), -1).toLower();
    if (suffix == QLatin1String("gif"))
        return MediaAnimation;
    if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("bmp") || suffix == QLatin1String("webp"))
        return MediaImage;
    if (suffix == QLatin1String("mp4") || suffix == QLatin1String("avi") || suffix == QLatin1String("mkv")
        || suffix == QLatin1String("mov") || suffix == QLatin1String("wmv") || suffix == QLatin1String("webm"))
        return MediaVideo;
    if (suffix == QLatin1String("mp3") || suffix == QLatin1String("wav") || suffix == QLatin1String("ogg")
        || suffix == QLatin1String("flac") || suffix == QLatin1String("m4a"))
        return MediaAudio;
    if (suffix == QLatin1String("pdf") || suffix == QLatin1String("doc") || suffix == QLatin1String("docx")
        || suffix == QLatin1String("txt") || suffix == QLatin1String("odt") || suffix == QLatin1String("rtf"))
        return MediaDocument;
    return MediaOther;
}

Chat::Poll::Poll()
    : myVote(-1)
{
}

bool Chat::Poll::isValid() const
{
    return !question.isEmpty() && options.size() >= 2;
}

bool Chat::Poll::isClosed(const QDateTime &now) const
{
    return closesAtUtc.isValid() && now > closesAtUtc;
}

Chat::Message::Message()
    : kind(KindText), delivery(DeliveryWaiting)
{
}

bool Chat::Message::isValid() const
{
    return !id.isEmpty() && !conversationId.isEmpty() && sentAtUtc.isValid();
}

QString Chat::newMessageId()
{
    QByteArray random;
    if (IdentityCrypto::randomBytes(&random, 16))
        return QString::fromLatin1(random.toHex());
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
}

MessageStore::MessageStore(const MeeruPaths &paths, const QString &identityId, QObject *parent)
    : QObject(parent), paths_(paths), identityId_(identityId)
{
}

QString MessageStore::filePath(const QString &conversationId) const
{
    return paths_.identityDirectory(identityId_) + QLatin1String("/messages/") + conversationId
         + QLatin1String(".json");
}

QList<Chat::Message> MessageStore::load(const QString &conversationId) const
{
    if (cache_.contains(conversationId))
        return cache_.value(conversationId);

    QList<Chat::Message> messages;
    QFile file(filePath(conversationId));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonArray array = root.value("messages").toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QJsonObject object = array.at(i).toObject();
            Chat::Message message;
            message.id = object.value("id").toString();
            message.conversationId = conversationId;
            message.authorId = object.value("authorId").toString();
            message.authorName = object.value("authorName").toString();
            message.text = object.value("text").toString();
            message.kind = object.value("kind").toInt(Chat::KindText);
            message.delivery = object.value("delivery").toInt(Chat::DeliveryWaiting);
            message.sentAtUtc = QDateTime::fromString(object.value("sentAtUtc").toString(), Qt::ISODate);

            const QJsonObject attachment = object.value("attachment").toObject();
            if (!attachment.isEmpty()) {
                message.attachment.fileId = attachment.value("fileId").toString();
                message.attachment.fileName = attachment.value("fileName").toString();
                message.attachment.fileSize = static_cast<qint64>(attachment.value("fileSize").toDouble());
                message.attachment.media = attachment.value("media").toInt(Chat::MediaOther);
                message.attachment.transfer = attachment.value("transfer").toInt(Chat::TransferOffered);
                message.attachment.localPath = attachment.value("localPath").toString();
                message.attachment.received = static_cast<qint64>(attachment.value("received").toDouble());
            }

            const QJsonObject poll = object.value("poll").toObject();
            if (!poll.isEmpty()) {
                message.poll.question = poll.value("question").toString();
                message.poll.closesAtUtc = QDateTime::fromString(poll.value("closesAtUtc").toString(), Qt::ISODate);
                message.poll.myVote = poll.value("myVote").toInt(-1);
                const QJsonArray options = poll.value("options").toArray();
                for (int j = 0; j < options.size(); ++j) {
                    Chat::PollOption option;
                    option.text = options.at(j).toObject().value("text").toString();
                    option.votes = options.at(j).toObject().value("votes").toInt();
                    message.poll.options.append(option);
                }
            }

            if (message.isValid())
                messages.append(message);
        }
    }

    cache_.insert(conversationId, messages);
    return messages;
}

bool MessageStore::save(const QString &conversationId, const QList<Chat::Message> &messages) const
{
    const QString directory = paths_.identityDirectory(identityId_) + QLatin1String("/messages");
    if (!QDir().mkpath(directory))
        return false;

    QJsonArray array;
    // Only the tail is kept: a conversation that has run for years should not
    // have to be read in full every time it is opened.
    const int first = qMax(0, messages.size() - kMaxStoredPerConversation);
    for (int i = first; i < messages.size(); ++i) {
        const Chat::Message &message = messages.at(i);
        QJsonObject object;
        object.insert("id", message.id);
        object.insert("authorId", message.authorId);
        object.insert("authorName", message.authorName);
        object.insert("text", message.text);
        object.insert("kind", message.kind);
        object.insert("delivery", message.delivery);
        object.insert("sentAtUtc", isoOrEmpty(message.sentAtUtc));

        if (message.attachment.isValid()) {
            QJsonObject attachment;
            attachment.insert("fileId", message.attachment.fileId);
            attachment.insert("fileName", message.attachment.fileName);
            attachment.insert("fileSize", static_cast<double>(message.attachment.fileSize));
            attachment.insert("media", message.attachment.media);
            attachment.insert("transfer", message.attachment.transfer);
            attachment.insert("localPath", message.attachment.localPath);
            attachment.insert("received", static_cast<double>(message.attachment.received));
            object.insert("attachment", attachment);
        }

        if (message.poll.isValid()) {
            QJsonArray options;
            for (int j = 0; j < message.poll.options.size(); ++j) {
                QJsonObject option;
                option.insert("text", message.poll.options.at(j).text);
                option.insert("votes", message.poll.options.at(j).votes);
                options.append(option);
            }
            QJsonObject poll;
            poll.insert("question", message.poll.question);
            poll.insert("options", options);
            poll.insert("closesAtUtc", isoOrEmpty(message.poll.closesAtUtc));
            poll.insert("myVote", message.poll.myVote);
            object.insert("poll", poll);
        }

        array.append(object);
    }

    QJsonObject root;
    root.insert("formatVersion", 1);
    root.insert("messages", array);

    QSaveFile file(filePath(conversationId));
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit())
        return false;

    cache_.insert(conversationId, messages);
    return true;
}

QList<Chat::Message> MessageStore::history(const QString &conversationId, int limit) const
{
    const QList<Chat::Message> all = load(conversationId);
    if (all.size() <= limit)
        return all;
    return all.mid(all.size() - limit);
}

bool MessageStore::contains(const QString &conversationId, const QString &messageId) const
{
    const QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == messageId)
            return true;
    }
    return false;
}

Chat::Message MessageStore::append(const Chat::Message &message)
{
    Chat::Message stored = message;
    if (stored.id.isEmpty())
        stored.id = Chat::newMessageId();
    if (!stored.sentAtUtc.isValid())
        stored.sentAtUtc = QDateTime::currentDateTimeUtc();
    if (!stored.isValid())
        return Chat::Message();

    QList<Chat::Message> all = load(stored.conversationId);

    // The same message can arrive twice: once directly and once while catching
    // up from a peer. The id decides, not the position.
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == stored.id)
            return all.at(i);
    }

    // Kept in time order so a late arrival from a sync lands where it belongs
    // rather than at the bottom pretending to be new.
    int position = all.size();
    while (position > 0 && all.at(position - 1).sentAtUtc > stored.sentAtUtc)
        --position;
    all.insert(position, stored);

    save(stored.conversationId, all);
    emit messageAdded(stored.conversationId, stored);
    return stored;
}

void MessageStore::setDelivery(const QString &conversationId, const QString &messageId, int delivery)
{
    QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id != messageId)
            continue;
        if (all.at(i).delivery == delivery)
            return;
        all[i].delivery = delivery;
        save(conversationId, all);
        emit deliveryChanged(conversationId, messageId, delivery);
        return;
    }
}

void MessageStore::setTransfer(const QString &conversationId, const QString &messageId,
                               int transfer, qint64 received, const QString &localPath)
{
    QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id != messageId)
            continue;
        all[i].attachment.transfer = transfer;
        all[i].attachment.received = received;
        if (!localPath.isEmpty())
            all[i].attachment.localPath = localPath;
        save(conversationId, all);
        emit transferChanged(conversationId, messageId);
        return;
    }
}

void MessageStore::setVote(const QString &conversationId, const QString &messageId, int option)
{
    QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id != messageId)
            continue;
        if (option < 0 || option >= all.at(i).poll.options.size())
            return;
        if (all.at(i).poll.myVote == option)
            return;

        // One vote each: an earlier choice is taken back before the new one
        // is counted.
        if (all.at(i).poll.myVote >= 0 && all.at(i).poll.myVote < all.at(i).poll.options.size())
            all[i].poll.options[all.at(i).poll.myVote].votes -= 1;
        all[i].poll.options[option].votes += 1;
        all[i].poll.myVote = option;
        save(conversationId, all);
        emit transferChanged(conversationId, messageId);
        return;
    }
}

Chat::Message MessageStore::message(const QString &conversationId, const QString &messageId) const
{
    const QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == messageId)
            return all.at(i);
    }
    return Chat::Message();
}

QString MessageStore::attachmentDirectory() const
{
    return paths_.identityDirectory(identityId_) + QLatin1String("/attachments");
}

QList<Chat::Message> MessageStore::waitingFor(const QString &conversationId) const
{
    QList<Chat::Message> waiting;
    const QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).isMine() && all.at(i).delivery == Chat::DeliveryWaiting)
            waiting.append(all.at(i));
    }
    return waiting;
}

QList<Chat::Message> MessageStore::since(const QString &conversationId,
                                         const QDateTime &moment, int limit) const
{
    QList<Chat::Message> newer;
    const QList<Chat::Message> all = load(conversationId);
    for (int i = 0; i < all.size(); ++i) {
        if (!moment.isValid() || all.at(i).sentAtUtc > moment)
            newer.append(all.at(i));
    }
    if (newer.size() > limit)
        newer = newer.mid(newer.size() - limit);
    return newer;
}

QDateTime MessageStore::latestTime(const QString &conversationId) const
{
    const QList<Chat::Message> all = load(conversationId);
    return all.isEmpty() ? QDateTime() : all.last().sentAtUtc;
}

QString MessageStore::lastPreview(const QString &conversationId) const
{
    const QList<Chat::Message> all = load(conversationId);
    if (all.isEmpty())
        return QString();
    const Chat::Message &last = all.last();
    const QString body = last.text.simplified();
    return last.isMine() ? QString::fromLatin1("You: ") + body : body;
}

int MessageStore::unreadCount(const QString &conversationId) const
{
    const QDateTime mark = readUpTo_.value(conversationId);
    const QList<Chat::Message> all = load(conversationId);
    int count = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).isMine())
            continue;
        if (!mark.isValid() || all.at(i).sentAtUtc > mark)
            ++count;
    }
    return count;
}

void MessageStore::markRead(const QString &conversationId)
{
    readUpTo_.insert(conversationId, QDateTime::currentDateTimeUtc());
}
