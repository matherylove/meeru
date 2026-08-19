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

quint32 Server::defaultPermissions()
{
    return PermSendMessages | PermCreateThreads | PermEmbedLinks | PermAttachFiles
         | PermMentionOthers | PermReadHistory | PermSendVoiceNotes | PermSendPolls
         | PermVoiceConnect | PermVoiceSpeak | PermVoiceCamera | PermVoiceShareScreen
         | PermUseSoundboard | PermChangeOwnNickname | PermCreateInvites;
}

QString Server::permissionName(quint32 flag)
{
    switch (flag) {
    case PermAdministrator:     return QString::fromLatin1("Administrator");
    case PermManageServer:      return QString::fromLatin1("Change the server");
    case PermManageChannels:    return QString::fromLatin1("Manage channels");
    case PermManageRoles:       return QString::fromLatin1("Manage lower roles");
    case PermManageEmoji:       return QString::fromLatin1("Manage emoji");
    case PermManageSounds:      return QString::fromLatin1("Manage sounds");
    case PermViewAudit:         return QString::fromLatin1("Read the audit log");
    case PermViewInvites:       return QString::fromLatin1("See invites");
    case PermCreateInvites:     return QString::fromLatin1("Create invites");
    case PermChangeOwnNickname: return QString::fromLatin1("Change own nickname here");
    case PermManageNicknames:   return QString::fromLatin1("Change other nicknames");
    case PermApproveMembers:    return QString::fromLatin1("Approve or refuse members");
    case PermKickMembers:       return QString::fromLatin1("Remove members");
    case PermBanMembers:        return QString::fromLatin1("Ban members");
    case PermSuspendMembers:    return QString::fromLatin1("Suspend members");
    case PermSendMessages:      return QString::fromLatin1("Write messages");
    case PermCreateThreads:     return QString::fromLatin1("Start threads");
    case PermEmbedLinks:        return QString::fromLatin1("Post links");
    case PermAttachFiles:       return QString::fromLatin1("Attach files");
    case PermMentionOthers:     return QString::fromLatin1("Mention people");
    case PermDeleteMessages:    return QString::fromLatin1("Delete messages");
    case PermPinMessages:       return QString::fromLatin1("Pin messages");
    case PermReadHistory:       return QString::fromLatin1("Read past messages");
    case PermSendVoiceNotes:    return QString::fromLatin1("Send voice notes");
    case PermSendPolls:         return QString::fromLatin1("Send polls");
    case PermVoiceConnect:      return QString::fromLatin1("Join voice channels");
    case PermVoiceSpeak:        return QString::fromLatin1("Speak in voice");
    case PermVoiceCamera:       return QString::fromLatin1("Turn on a camera");
    case PermVoiceShareScreen:  return QString::fromLatin1("Share a screen");
    case PermUseSoundboard:     return QString::fromLatin1("Use the sound panel");
    case PermMuteOthers:        return QString::fromLatin1("Mute or deafen others");
    case PermMoveOthers:        return QString::fromLatin1("Move others between channels");
    default:                    return QString();
    }
}

QList<quint32> Server::allPermissions()
{
    QList<quint32> flags;
    for (int bit = 0; bit < 32; ++bit) {
        const quint32 flag = 1u << bit;
        if (!permissionName(flag).isEmpty())
            flags.append(flag);
    }
    return flags;
}

bool Server::Invite::isExpired(const QDateTime &now) const
{
    if (maxUses > 0 && uses >= maxUses)
        return true;
    return expiresAtUtc.isValid() && now > expiresAtUtc;
}

Server::Role::Role()
    : rank(0), canManage(false), permissions(Server::defaultPermissions())
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
    description_ = root.value("description").toString();
    haloColour_ = root.value("haloColour").toString();

    invites_.clear();
    const QJsonArray invites = root.value("invites").toArray();
    for (int i = 0; i < invites.size(); ++i) {
        const QJsonObject object = invites.at(i).toObject();
        Server::Invite invite;
        invite.code = object.value("code").toString();
        invite.createdBy = object.value("createdBy").toString();
        invite.grantsRoleId = object.value("grantsRoleId").toString();
        invite.createdAtUtc = QDateTime::fromString(object.value("createdAtUtc").toString(), Qt::ISODate);
        invite.expiresAtUtc = QDateTime::fromString(object.value("expiresAtUtc").toString(), Qt::ISODate);
        invite.uses = object.value("uses").toInt();
        invite.maxUses = object.value("maxUses").toInt();
        const QJsonArray joined = object.value("joinedIdentities").toArray();
        for (int j = 0; j < joined.size(); ++j)
            invite.joinedIdentities.append(joined.at(j).toString());
        if (!invite.code.isEmpty())
            invites_.append(invite);
    }

    sounds_.clear();
    const QJsonArray sounds = root.value("sounds").toArray();
    for (int i = 0; i < sounds.size(); ++i) {
        const QJsonObject object = sounds.at(i).toObject();
        Server::Sound sound;
        sound.id = object.value("id").toString();
        sound.name = object.value("name").toString();
        sound.filePath = object.value("filePath").toString();
        sound.addedBy = object.value("addedBy").toString();
        if (!sound.id.isEmpty())
            sounds_.append(sound);
    }

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
        role.permissions = static_cast<quint32>(object.value("permissions").toDouble(Server::defaultPermissions()));
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
        member.nickname = object.value("nickname").toString();
        member.accountCreatedUtc = QDateTime::fromString(object.value("accountCreatedUtc").toString(), Qt::ISODate);
        member.joinedWithInvite = object.value("joinedWithInvite").toString();
        member.suspended = object.value("suspended").toBool(false);
        const QJsonArray held = object.value("roleIds").toArray();
        for (int j = 0; j < held.size(); ++j)
            member.roleIds.append(held.at(j).toString());
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
        object.insert("permissions", static_cast<double>(role.permissions));
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
        object.insert("nickname", member.nickname);
        object.insert("accountCreatedUtc", isoOrEmpty(member.accountCreatedUtc));
        object.insert("joinedWithInvite", member.joinedWithInvite);
        object.insert("suspended", member.suspended);
        QJsonArray held;
        for (int j = 0; j < member.roleIds.size(); ++j)
            held.append(member.roleIds.at(j));
        object.insert("roleIds", held);
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
    root.insert("description", description_);
    root.insert("haloColour", haloColour_);

    QJsonArray invites;
    for (int i = 0; i < invites_.size(); ++i) {
        const Server::Invite &invite = invites_.at(i);
        QJsonObject object;
        object.insert("code", invite.code);
        object.insert("createdBy", invite.createdBy);
        object.insert("grantsRoleId", invite.grantsRoleId);
        object.insert("createdAtUtc", isoOrEmpty(invite.createdAtUtc));
        object.insert("expiresAtUtc", isoOrEmpty(invite.expiresAtUtc));
        object.insert("uses", invite.uses);
        object.insert("maxUses", invite.maxUses);
        QJsonArray joined;
        for (int j = 0; j < invite.joinedIdentities.size(); ++j)
            joined.append(invite.joinedIdentities.at(j));
        object.insert("joinedIdentities", joined);
        invites.append(object);
    }
    root.insert("invites", invites);

    QJsonArray sounds;
    for (int i = 0; i < sounds_.size(); ++i) {
        QJsonObject object;
        object.insert("id", sounds_.at(i).id);
        object.insert("name", sounds_.at(i).name);
        object.insert("filePath", sounds_.at(i).filePath);
        object.insert("addedBy", sounds_.at(i).addedBy);
        sounds.append(object);
    }
    root.insert("sounds", sounds);
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

quint32 ServerModel::permissionsFor(const QString &identityId) const
{
    quint32 allowed = 0;
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId != identityId)
            continue;

        QStringList held = members_.at(i).roleIds;
        if (held.isEmpty() && !members_.at(i).roleId.isEmpty())
            held.append(members_.at(i).roleId);

        for (int j = 0; j < held.size(); ++j) {
            const Server::Role entry = role(held.at(j));
            if (entry.permissions & Server::PermAdministrator)
                return 0xFFFFFFFFu;   // an administrator is not asked twice
            allowed |= entry.permissions;
        }
        return allowed;
    }
    return 0;
}

bool ServerModel::may(const QString &identityId, quint32 permission) const
{
    return (permissionsFor(identityId) & permission) != 0;
}

int ServerModel::highestRank(const QString &identityId) const
{
    int best = -1;
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId != identityId)
            continue;
        QStringList held = members_.at(i).roleIds;
        if (held.isEmpty() && !members_.at(i).roleId.isEmpty())
            held.append(members_.at(i).roleId);
        for (int j = 0; j < held.size(); ++j)
            best = qMax(best, role(held.at(j)).rank);
    }
    return best;
}

void ServerModel::addInvite(const Server::Invite &invite)
{
    Server::Invite entry = invite;
    if (entry.code.isEmpty())
        entry.code = newId();
    if (!entry.createdAtUtc.isValid())
        entry.createdAtUtc = QDateTime::currentDateTimeUtc();
    invites_.append(entry);
}

void ServerModel::removeInvite(const QString &code)
{
    for (int i = invites_.size() - 1; i >= 0; --i) {
        if (invites_.at(i).code == code)
            invites_.removeAt(i);
    }
}

void ServerModel::addSound(const Server::Sound &sound)
{
    Server::Sound entry = sound;
    if (entry.id.isEmpty())
        entry.id = newId();
    sounds_.append(entry);
}

void ServerModel::removeSound(const QString &soundId)
{
    for (int i = sounds_.size() - 1; i >= 0; --i) {
        if (sounds_.at(i).id == soundId)
            sounds_.removeAt(i);
    }
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

void ServerModel::setMemberNickname(const QString &identityId, const QString &nickname)
{
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId == identityId) {
            members_[i].nickname = nickname;
            return;
        }
    }
}

void ServerModel::setMemberSuspended(const QString &identityId, bool suspended)
{
    for (int i = 0; i < members_.size(); ++i) {
        if (members_.at(i).identityId == identityId) {
            members_[i].suspended = suspended;
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
    model.description_ = topic;
    model.haloColour_ = QString::fromLatin1("#DFB2F4");

    Server::Role owner;
    owner.id = newId();
    owner.name = QString::fromLatin1("Owner");
    owner.colour = QString::fromLatin1("#F49097");
    owner.rank = 100;
    owner.canManage = true;
    owner.permissions = Server::PermAdministrator;
    model.roles_.append(owner);

    Server::Role member;
    member.id = newId();
    member.name = QString::fromLatin1("Member");
    member.colour = QString::fromLatin1("#DFB2F4");
    member.rank = 10;
    member.permissions = Server::defaultPermissions();
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
    self.roleIds.append(owner.id);
    self.accountCreatedUtc = QDateTime::currentDateTimeUtc();
    self.joinedAtUtc = QDateTime::currentDateTimeUtc();
    model.members_.append(self);

    model.note(ownerName, QString::fromLatin1("Created the server"));
    return model;
}
