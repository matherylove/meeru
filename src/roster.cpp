#include "roster.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "identity_crypto.h"
#include "presence.h"

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QJsonArray toArray(const QStringList &values)
{
    QJsonArray array;
    for (int i = 0; i < values.size(); ++i)
        array.append(values.at(i));
    return array;
}

QStringList toStringList(const QJsonArray &array)
{
    QStringList values;
    for (int i = 0; i < array.size(); ++i) {
        const QString value = array.at(i).toString();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

QString isoOrEmpty(const QDateTime &value)
{
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

}

Roster::Contact::Contact()
    : presence(Presence::key(Presence::Invisible)),
      state(Roster::ContactPendingOutgoing),
      favorite(false)
{
}

QString Roster::Contact::bestName() const
{
    if (!displayName.trimmed().isEmpty())
        return displayName.trimmed();
    return id.left(12);
}

Roster::Conversation::Conversation()
    : group(false), favorite(false)
{
}

Roster::Server::Server()
    : state(Roster::ServerJoined), owner(false), favorite(false)
{
}

bool Roster::isValidIdentityId(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (int i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool lower = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        if (!digit && !lower)
            return false;
    }
    return true;
}

QString Roster::normaliseIdentityId(const QString &value)
{
    QString candidate = value.trimmed();
    if (candidate.startsWith(QLatin1String("meeru:"), Qt::CaseInsensitive))
        candidate = candidate.mid(6);
    candidate.remove(QLatin1Char('/'));
    candidate = candidate.trimmed().toLower();
    return isValidIdentityId(candidate) ? candidate : QString();
}

QString Roster::newLocalId()
{
    QByteArray random;
    if (IdentityCrypto::randomBytes(&random, 16))
        return QString::fromLatin1(random.toHex());
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
}

// ------------------------------------------------------------------ RosterStore

RosterStore::RosterStore(const MeeruPaths &paths, const QString &identityId)
    : paths_(paths), identityId_(identityId)
{
}

QString RosterStore::filePath() const
{
    return paths_.identityDirectory(identityId_) + QLatin1String("/roster.json");
}

bool RosterStore::load(QString *error)
{
    contacts_.clear();
    conversations_.clear();
    servers_.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return true;   // a brand new identity simply has no roster yet

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty()) {
        setError(error, QString::fromLatin1("The contact list file could not be read"));
        return false;
    }

    const QJsonArray contactArray = root.value("contacts").toArray();
    for (int i = 0; i < contactArray.size(); ++i) {
        const QJsonObject object = contactArray.at(i).toObject();
        Roster::Contact contact;
        contact.id = object.value("id").toString();
        if (!Roster::isValidIdentityId(contact.id))
            continue;
        contact.displayName = object.value("displayName").toString();
        contact.statusText = object.value("statusText").toString();
        contact.presence = object.value("presence").toString();
        contact.state = object.value("state").toInt(Roster::ContactPendingOutgoing);
        contact.favorite = object.value("favorite").toBool(false);
        contact.addedAtUtc = QDateTime::fromString(object.value("addedAtUtc").toString(), Qt::ISODate);
        contact.endpointHint = object.value("endpointHint").toString();
        contact.lastSeenUtc = QDateTime::fromString(object.value("lastSeenUtc").toString(), Qt::ISODate);
        contacts_.append(contact);
    }

    const QJsonArray conversationArray = root.value("conversations").toArray();
    for (int i = 0; i < conversationArray.size(); ++i) {
        const QJsonObject object = conversationArray.at(i).toObject();
        Roster::Conversation conversation;
        conversation.id = object.value("id").toString();
        if (conversation.id.isEmpty())
            continue;
        conversation.title = object.value("title").toString();
        conversation.members = toStringList(object.value("members").toArray());
        conversation.group = object.value("group").toBool(conversation.members.size() > 1);
        conversation.favorite = object.value("favorite").toBool(false);
        conversation.preview = object.value("preview").toString();
        conversation.createdAtUtc = QDateTime::fromString(object.value("createdAtUtc").toString(), Qt::ISODate);
        conversation.updatedAtUtc = QDateTime::fromString(object.value("updatedAtUtc").toString(), Qt::ISODate);
        conversations_.append(conversation);
    }

    const QJsonArray serverArray = root.value("servers").toArray();
    for (int i = 0; i < serverArray.size(); ++i) {
        const QJsonObject object = serverArray.at(i).toObject();
        Roster::Server server;
        server.id = object.value("id").toString();
        if (server.id.isEmpty())
            continue;
        server.name = object.value("name").toString();
        server.topic = object.value("topic").toString();
        server.state = object.value("state").toInt(Roster::ServerJoined);
        server.owner = object.value("owner").toBool(false);
        server.favorite = object.value("favorite").toBool(false);
        server.joinedAtUtc = QDateTime::fromString(object.value("joinedAtUtc").toString(), Qt::ISODate);
        servers_.append(server);
    }

    return true;
}

bool RosterStore::save(QString *error) const
{
    const QString directory = paths_.identityDirectory(identityId_);
    if (!QDir().mkpath(directory)) {
        setError(error, QString::fromLatin1("Cannot create the identity folder"));
        return false;
    }

    QJsonArray contactArray;
    for (int i = 0; i < contacts_.size(); ++i) {
        const Roster::Contact &contact = contacts_.at(i);
        QJsonObject object;
        object.insert("id", contact.id);
        object.insert("displayName", contact.displayName);
        object.insert("statusText", contact.statusText);
        object.insert("presence", contact.presence);
        object.insert("state", contact.state);
        object.insert("favorite", contact.favorite);
        object.insert("addedAtUtc", isoOrEmpty(contact.addedAtUtc));
        object.insert("endpointHint", contact.endpointHint);
        object.insert("lastSeenUtc", isoOrEmpty(contact.lastSeenUtc));
        contactArray.append(object);
    }

    QJsonArray conversationArray;
    for (int i = 0; i < conversations_.size(); ++i) {
        const Roster::Conversation &conversation = conversations_.at(i);
        QJsonObject object;
        object.insert("id", conversation.id);
        object.insert("title", conversation.title);
        object.insert("members", toArray(conversation.members));
        object.insert("group", conversation.group);
        object.insert("favorite", conversation.favorite);
        object.insert("preview", conversation.preview);
        object.insert("createdAtUtc", isoOrEmpty(conversation.createdAtUtc));
        object.insert("updatedAtUtc", isoOrEmpty(conversation.updatedAtUtc));
        conversationArray.append(object);
    }

    QJsonArray serverArray;
    for (int i = 0; i < servers_.size(); ++i) {
        const Roster::Server &server = servers_.at(i);
        QJsonObject object;
        object.insert("id", server.id);
        object.insert("name", server.name);
        object.insert("topic", server.topic);
        object.insert("state", server.state);
        object.insert("owner", server.owner);
        object.insert("favorite", server.favorite);
        object.insert("joinedAtUtc", isoOrEmpty(server.joinedAtUtc));
        serverArray.append(object);
    }

    QJsonObject root;
    root.insert("formatVersion", 1);
    root.insert("contacts", contactArray);
    root.insert("conversations", conversationArray);
    root.insert("servers", serverArray);

    QSaveFile file(filePath());
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        setError(error, QString::fromLatin1("Cannot save the contact list"));
        return false;
    }
    return true;
}

QList<Roster::Contact> RosterStore::acceptedContacts() const
{
    QList<Roster::Contact> result;
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).state == Roster::ContactAccepted)
            result.append(contacts_.at(i));
    }
    return result;
}

int RosterStore::pendingRequestCount() const
{
    int count = 0;
    for (int i = 0; i < contacts_.size(); ++i) {
        const int state = contacts_.at(i).state;
        if (state == Roster::ContactPendingIncoming || state == Roster::ContactPendingOutgoing)
            ++count;
    }
    return count;
}

bool RosterStore::hasContact(const QString &id) const
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id)
            return true;
    }
    return false;
}

Roster::Contact RosterStore::contact(const QString &id) const
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id)
            return contacts_.at(i);
    }
    return Roster::Contact();
}

bool RosterStore::addContact(const Roster::Contact &contact, QString *error)
{
    if (!Roster::isValidIdentityId(contact.id)) {
        setError(error, QString::fromLatin1("That is not a valid Meeru ID"));
        return false;
    }
    if (hasContact(contact.id)) {
        setError(error, QString::fromLatin1("That contact is already on your list"));
        return false;
    }
    contacts_.append(contact);
    return save(error);
}

bool RosterStore::removeContact(const QString &id, QString *error)
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id) {
            contacts_.removeAt(i);
            // Drop conversations that would be left without any member.
            for (int j = conversations_.size() - 1; j >= 0; --j) {
                QStringList members = conversations_.at(j).members;
                if (members.removeAll(id) > 0) {
                    if (members.isEmpty())
                        conversations_.removeAt(j);
                    else
                        conversations_[j].members = members;
                }
            }
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That contact is no longer on your list"));
    return false;
}

bool RosterStore::setContactState(const QString &id, int state, QString *error)
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id) {
            contacts_[i].state = state;
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That contact is no longer on your list"));
    return false;
}

bool RosterStore::setContactFavorite(const QString &id, bool favorite, QString *error)
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id) {
            contacts_[i].favorite = favorite;
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That contact is no longer on your list"));
    return false;
}

bool RosterStore::updateContactProfile(const QString &id, const QString &displayName,
                                       const QString &presence, const QString &statusText, QString *error)
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id != id)
            continue;

        // The name a contact reports is theirs to choose, but a name the user
        // typed when adding them is kept if the contact has not sent one.
        if (!displayName.trimmed().isEmpty())
            contacts_[i].displayName = displayName.trimmed();
        if (!presence.isEmpty())
            contacts_[i].presence = presence;
        contacts_[i].statusText = statusText;
        contacts_[i].lastSeenUtc = QDateTime::currentDateTimeUtc();
        return save(error);
    }
    setError(error, QString::fromLatin1("That contact is no longer on your list"));
    return false;
}

bool RosterStore::touchContact(const QString &id, QString *error)
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_.at(i).id == id) {
            contacts_[i].lastSeenUtc = QDateTime::currentDateTimeUtc();
            return save(error);
        }
    }
    return false;
}

Roster::Conversation RosterStore::conversationWithMembers(const QStringList &members) const
{
    QStringList wanted = members;
    wanted.sort();
    for (int i = 0; i < conversations_.size(); ++i) {
        QStringList current = conversations_.at(i).members;
        current.sort();
        if (current == wanted)
            return conversations_.at(i);
    }
    return Roster::Conversation();
}

bool RosterStore::addConversation(const Roster::Conversation &conversation, QString *error)
{
    if (conversation.id.isEmpty() || conversation.members.isEmpty()) {
        setError(error, QString::fromLatin1("A conversation needs at least one member"));
        return false;
    }
    conversations_.append(conversation);
    return save(error);
}

bool RosterStore::removeConversation(const QString &id, QString *error)
{
    for (int i = 0; i < conversations_.size(); ++i) {
        if (conversations_.at(i).id == id) {
            conversations_.removeAt(i);
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That conversation no longer exists"));
    return false;
}

bool RosterStore::renameConversation(const QString &id, const QString &title, QString *error)
{
    for (int i = 0; i < conversations_.size(); ++i) {
        if (conversations_.at(i).id == id) {
            conversations_[i].title = title.trimmed();
            conversations_[i].updatedAtUtc = QDateTime::currentDateTimeUtc();
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That conversation no longer exists"));
    return false;
}

bool RosterStore::setConversationFavorite(const QString &id, bool favorite, QString *error)
{
    for (int i = 0; i < conversations_.size(); ++i) {
        if (conversations_.at(i).id == id) {
            conversations_[i].favorite = favorite;
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That conversation no longer exists"));
    return false;
}

bool RosterStore::addServer(const Roster::Server &server, QString *error)
{
    if (server.id.isEmpty()) {
        setError(error, QString::fromLatin1("That server address is not valid"));
        return false;
    }
    for (int i = 0; i < servers_.size(); ++i) {
        if (servers_.at(i).id == server.id) {
            setError(error, QString::fromLatin1("You are already on that server"));
            return false;
        }
    }
    servers_.append(server);
    return save(error);
}

bool RosterStore::removeServer(const QString &id, QString *error)
{
    for (int i = 0; i < servers_.size(); ++i) {
        if (servers_.at(i).id == id) {
            servers_.removeAt(i);
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That server is no longer on your list"));
    return false;
}

bool RosterStore::setServerFavorite(const QString &id, bool favorite, QString *error)
{
    for (int i = 0; i < servers_.size(); ++i) {
        if (servers_.at(i).id == id) {
            servers_[i].favorite = favorite;
            return save(error);
        }
    }
    setError(error, QString::fromLatin1("That server is no longer on your list"));
    return false;
}

QString RosterStore::conversationTitle(const Roster::Conversation &conversation) const
{
    if (!conversation.title.trimmed().isEmpty())
        return conversation.title.trimmed();

    QStringList names;
    for (int i = 0; i < conversation.members.size() && names.size() < 3; ++i)
        names.append(contact(conversation.members.at(i)).bestName());

    if (names.isEmpty())
        return QString::fromLatin1("Empty conversation");
    if (conversation.members.size() > names.size())
        names.append(QString::fromLatin1("and %1 more").arg(conversation.members.size() - names.size()));
    return names.join(QString::fromLatin1(", "));
}
