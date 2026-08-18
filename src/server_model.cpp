#include "server_model.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "identity_crypto.h"

namespace {

QString newId()
{
    QByteArray random;
    if (IdentityCrypto::randomBytes(&random, 8))
        return QString::fromLatin1(random.toHex());
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
}

QString isoOrEmpty(const QDateTime &value)
{
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

}

Server::Channel::Channel()
    : kind(ChannelText), position(0), adultOnly(false)
{
}

bool Server::Channel::isValid() const
{
    return !id.isEmpty() && !name.isEmpty();
}

QString Server::Channel::conversationId(const QString &serverId) const
{
    // Each channel keeps its own history, so it is its own conversation as far
    // as the message store is concerned.
    return serverId + QLatin1Char('#') + id;
}

Server::Role::Role()
    : rank(0), canManage(false)
{
}

bool Server::Role::isValid() const
{
    return !id.isEmpty() && !name.isEmpty();
}

ServerModel::ServerModel(const MeeruPaths &paths, const QString &identityId, const QString &serverId)
    : paths_(paths), identityId_(identityId), serverId_(serverId)
{
}

QString ServerModel::filePath() const
{
    return paths_.identityDirectory(identityId_) + QLatin1String("/servers/") + serverId_
         + QLatin1String(".json");
}

bool ServerModel::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    name_ = root.value("name").toString();
    topic_ = root.value("topic").toString();

    categories_.clear();
    const QJsonArray categories = root.value("categories").toArray();
    for (int i = 0; i < categories.size(); ++i) {
        const QJsonObject object = categories.at(i).toObject();
        Server::Category category;
        category.id = object.value("id").toString();
        category.name = object.value("name").toString();
        category.position = object.value("position").toInt();
        if (!category.id.isEmpty())
            categories_.append(category);
    }

    channels_.clear();
    const QJsonArray channels = root.value("channels").toArray();
    for (int i = 0; i < channels.size(); ++i) {
        const QJsonObject object = channels.at(i).toObject();
        Server::Channel channel;
        channel.id = object.value("id").toString();
        channel.categoryId = object.value("categoryId").toString();
        channel.name = object.value("name").toString();
        channel.topic = object.value("topic").toString();
        channel.kind = object.value("kind").toInt(Server::ChannelText);
        channel.position = object.value("position").toInt();
        channel.adultOnly = object.value("adultOnly").toBool(false);
        channel.parentId = object.value("parentId").toString();
        if (channel.isValid())
            channels_.append(channel);
    }

    roles_.clear();
    const QJsonArray roles = root.value("roles").toArray();
    for (int i = 0; i < roles.size(); ++i) {
        const QJsonObject object = roles.at(i).toObject();
        Server::Role role;
        role.id = object.value("id").toString();
        role.name = object.value("name").toString();
        role.colour = object.value("colour").toString();
        role.rank = object.value("rank").toInt();
        role.canManage = object.value("canManage").toBool(false);
        if (role.isValid())
            roles_.append(role);
    }

    members_.clear();
    const QJsonArray members = root.value("members").toArray();
    for (int i = 0; i < members.size(); ++i) {
        const QJsonObject object = members.at(i).toObject();
        Server::Member member;
        member.identityId = object.value("identityId").toString();
        member.displayName = object.value("displayName").toString();
        member.roleId = object.value("roleId").toString();
        member.joinedAtUtc = QDateTime::fromString(object.value("joinedAtUtc").toString(), Qt::ISODate);
        if (!member.identityId.isEmpty())
            members_.append(member);
    }

    audit_.clear();
    const QJsonArray audit = root.value("audit").toArray();
    for (int i = 0; i < audit.size(); ++i) {
        const QJsonObject object = audit.at(i).toObject();
        Server::AuditEntry entry;
        entry.atUtc = QDateTime::fromString(object.value("atUtc").toString(), Qt::ISODate);
        entry.actorName = object.value("actorName").toString();
        entry.description = object.value("description").toString();
        audit_.append(entry);
    }
    return true;
}

bool ServerModel::save(QString *error) const
{
    const QString directory = paths_.identityDirectory(identityId_) + QLatin1String("/servers");
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QString::fromLatin1("Cannot create the servers folder");
        return false;
    }

    QJsonArray categories;
    for (int i = 0; i < categories_.size(); ++i) {
        QJsonObject object;
        object.insert("id", categories_.at(i).id);
        object.insert("name", categories_.at(i).name);
        object.insert("position", categories_.at(i).position);
        categories.append(object);
    }

    QJsonArray channels;
    for (int i = 0; i < channels_.size(); ++i) {
        const Server::Channel &channel = channels_.at(i);
        QJsonObject object;
        object.insert("id", channel.id);
        object.insert("categoryId", channel.categoryId);
        object.insert("name", channel.name);
        object.insert("topic", channel.topic);
        object.insert("kind", channel.kind);
        object.insert("position", channel.position);
        object.insert("adultOnly", channel.adultOnly);
        object.insert("parentId", channel.parentId);
        channels.append(object);
    }

    QJsonArray roles;
    for (int i = 0; i < roles_.size(); ++i) {
        const Server::Role &role = roles_.at(i);
        QJsonObject object;
        object.insert("id", role.id);
        object.insert("name", role.name);
        object.insert("colour", role.colour);
        object.insert("rank", role.rank);
        object.insert("canManage", role.canManage);
        roles.append(object);
    }

    QJsonArray members;
    for (int i = 0; i < members_.size(); ++i) {
        const Server::Member &member = members_.at(i);
        QJsonObject object;
        object.insert("identityId", member.identityId);
        object.insert("displayName", member.displayName);
        object.insert("roleId", member.roleId);
        object.insert("joinedAtUtc", isoOrEmpty(member.joinedAtUtc));
        members.append(object);
    }

    QJsonArray audit;
    const int firstEntry = qMax(0, audit_.size() - 500);
    for (int i = firstEntry; i < audit_.size(); ++i) {
        QJsonObject object;
        object.insert("atUtc", isoOrEmpty(audit_.at(i).atUtc));
        object.insert("actorName", audit_.at(i).actorName);
        object.insert("description", audit_.at(i).description);
        audit.append(object);
    }

    QJsonObject root;
    root.insert("formatVersion", 1);
    root.insert("name", name_);
    root.insert("topic", topic_);
    root.insert("categories", categories);
    root.insert("channels", channels);
    root.insert("roles", roles);
    root.insert("members", members);
    root.insert("audit", audit);

    QSaveFile file(filePath());
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot save the server");
        return false;
    }
    return true;
}

QList<Server::Channel> ServerModel::channelsOfKind(int kind) const
{
    QList<Server::Channel> result;
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).kind == kind)
            result.append(channels_.at(i));
    }

    // By category, then by position inside it.
    for (int i = 0; i < result.size(); ++i) {
        for (int j = i + 1; j < result.size(); ++j) {
            const bool swap = (result.at(j).categoryId < result.at(i).categoryId)
                           || (result.at(j).categoryId == result.at(i).categoryId
                               && result.at(j).position < result.at(i).position);
            if (swap)
                result.swap(i, j);
        }
    }
    return result;
}

QList<Server::Channel> ServerModel::threadsOf(const QString &channelId) const
{
    QList<Server::Channel> result;
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).kind == Server::ChannelThread
            && (channelId.isEmpty() || channels_.at(i).parentId == channelId)) {
            result.append(channels_.at(i));
        }
    }
    return result;
}

Server::Channel ServerModel::channel(const QString &channelId) const
{
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).id == channelId)
            return channels_.at(i);
    }
    return Server::Channel();
}

Server::Role ServerModel::role(const QString &roleId) const
{
    for (int i = 0; i < roles_.size(); ++i) {
        if (roles_.at(i).id == roleId)
            return roles_.at(i);
    }
    return Server::Role();
}

QList<Server::Member> ServerModel::membersByRank() const
{
    QList<Server::Member> result = members_;
    for (int i = 0; i < result.size(); ++i) {
        for (int j = i + 1; j < result.size(); ++j) {
            const int rankI = role(result.at(i).roleId).rank;
            const int rankJ = role(result.at(j).roleId).rank;
            const bool swap = rankJ > rankI
                           || (rankJ == rankI && result.at(j).displayName < result.at(i).displayName);
            if (swap)
                result.swap(i, j);
        }
    }
    return result;
}

void ServerModel::addCategory(const Server::Category &category)
{
    Server::Category entry = category;
    if (entry.id.isEmpty())
        entry.id = newId();
    categories_.append(entry);
}

void ServerModel::addChannel(const Server::Channel &channel)
{
    Server::Channel entry = channel;
    if (entry.id.isEmpty())
        entry.id = newId();
    if (entry.isValid())
        channels_.append(entry);
}

void ServerModel::removeChannel(const QString &channelId)
{
    for (int i = channels_.size() - 1; i >= 0; --i) {
        // A channel takes its threads with it.
        if (channels_.at(i).id == channelId || channels_.at(i).parentId == channelId)
            channels_.removeAt(i);
    }
}

void ServerModel::addRole(const Server::Role &role)
{
    Server::Role entry = role;
    if (entry.id.isEmpty())
        entry.id = newId();
    if (entry.isValid())
        roles_.append(entry);
}

void ServerModel::removeRole(const QString &roleId)
{
    for (int i = roles_.size() - 1; i >= 0; --i) {
        if (roles_.at(i).id == roleId)
            roles_.removeAt(i);
    }
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).roleId == roleId)
            members_[i].roleId.clear();
    }
}

void ServerModel::setMemberRole(const QString &identityId, const QString &roleId)
{
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId == identityId) {
            members_[i].roleId = roleId;
            return;
        }
    }
}

void ServerModel::addMember(const Server::Member &member)
{
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId == member.identityId)
            return;
    }
    Server::Member entry = member;
    if (!entry.joinedAtUtc.isValid())
        entry.joinedAtUtc = QDateTime::currentDateTimeUtc();
    members_.append(entry);
}

void ServerModel::removeMember(const QString &identityId)
{
    for (int i = members_.size() - 1; i >= 0; --i) {
        if (members_.at(i).identityId == identityId)
            members_.removeAt(i);
    }
}

void ServerModel::note(const QString &actorName, const QString &description)
{
    Server::AuditEntry entry;
    entry.atUtc = QDateTime::currentDateTimeUtc();
    entry.actorName = actorName;
    entry.description = description;
    audit_.append(entry);
}

ServerModel ServerModel::createDefault(const MeeruPaths &paths, const QString &identityId,
                                       const QString &serverId, const QString &name,
                                       const QString &topic, const QString &ownerName)
{
    ServerModel model(paths, identityId, serverId);
    model.name_ = name;
    model.topic_ = topic;

    Server::Role owner;
    owner.id = newId();
    owner.name = QString::fromLatin1("Owner");
    owner.colour = QString::fromLatin1("#F49097");
    owner.rank = 100;
    owner.canManage = true;
    model.roles_.append(owner);

    Server::Role member;
    member.id = newId();
    member.name = QString::fromLatin1("Member");
    member.colour = QString::fromLatin1("#DFB2F4");
    member.rank = 10;
    model.roles_.append(member);

    Server::Category general;
    general.id = newId();
    general.name = QString::fromLatin1("General");
    general.position = 0;
    model.categories_.append(general);

    Server::Channel text;
    text.id = newId();
    text.categoryId = general.id;
    text.name = QString::fromLatin1("general");
    text.topic = topic;
    text.kind = Server::ChannelText;
    model.channels_.append(text);

    Server::Channel voice;
    voice.id = newId();
    voice.categoryId = general.id;
    voice.name = QString::fromLatin1("Voice room");
    voice.kind = Server::ChannelVoice;
    voice.position = 1;
    model.channels_.append(voice);

    Server::Member self;
    self.identityId = identityId;
    self.displayName = ownerName;
    self.roleId = owner.id;
    self.joinedAtUtc = QDateTime::currentDateTimeUtc();
    model.members_.append(self);

    model.note(ownerName, QString::fromLatin1("Created the server"));
    return model;
}
