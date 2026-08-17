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
