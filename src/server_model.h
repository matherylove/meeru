#ifndef MEERU_SERVER_MODEL_H
#define MEERU_SERVER_MODEL_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "meeru_paths.h"

// Everything that makes up a server: its channels, the categories they sit in,
// the roles that decide who ranks above whom, and the members themselves.
//
// A group is the same thing with the parts it does not have left out, which is
// why both use this model and the same window.
namespace Server {

enum ChannelKind {
    ChannelText = 0,
    ChannelVoice = 1,
    ChannelThread = 2,
    ChannelPoll = 3,
    ChannelPost = 4
};

struct Category
{
    Category() : position(0) {}
    QString id;
    QString name;
    int position;
};

struct Channel
{
    Channel();
    QString id;
    QString categoryId;
    QString name;
    QString topic;
    int kind;
    int position;
    bool adultOnly;        // hidden until the reader says they are an adult
    QString parentId;      // set for threads, naming the channel they hang from

    bool isValid() const;
    QString conversationId(const QString &serverId) const;
};

// What a role is allowed to do. Kept as flags so a role is one number, and so
// a check is a single test rather than a walk through a list.
enum Permission {
    PermNone              = 0,
    PermAdministrator     = 1 << 0,   // everything below, without asking
    PermManageServer      = 1 << 1,   // name, region, description, pictures
    PermManageChannels    = 1 << 2,
    PermManageRoles       = 1 << 3,   // only roles below your own highest
    PermManageEmoji       = 1 << 4,
    PermManageSounds      = 1 << 5,
    PermViewAudit         = 1 << 6,
    PermViewInvites       = 1 << 7,
    PermCreateInvites     = 1 << 8,
    PermChangeOwnNickname = 1 << 9,
    PermManageNicknames   = 1 << 10,
    PermApproveMembers    = 1 << 11,
    PermKickMembers       = 1 << 12,
    PermBanMembers        = 1 << 13,
    PermSuspendMembers    = 1 << 14,
    PermSendMessages      = 1 << 15,
    PermCreateThreads     = 1 << 16,
    PermEmbedLinks        = 1 << 17,
    PermAttachFiles       = 1 << 18,
    PermMentionOthers     = 1 << 19,
    PermDeleteMessages    = 1 << 20,
    PermPinMessages       = 1 << 21,
    PermReadHistory       = 1 << 22,
    PermSendVoiceNotes    = 1 << 23,
    PermSendPolls         = 1 << 24,
    PermVoiceConnect      = 1 << 25,
    PermVoiceSpeak        = 1 << 26,
    PermVoiceCamera       = 1 << 27,
    PermVoiceShareScreen  = 1 << 28,
    PermUseSoundboard     = 1 << 29,
    PermMuteOthers        = 1 << 30,
    PermMoveOthers        = 1u << 31
};

// The set a brand new member gets: able to take part, unable to change anything.
quint32 defaultPermissions();
QString permissionName(quint32 flag);
QList<quint32> allPermissions();

struct Role
{
    Role();
    QString id;
    QString name;
    QString colour;        // "#rrggbb"
    int rank;              // higher sits higher in the member list
    bool canManage;
    quint32 permissions;

    bool isValid() const;
};

struct Member
{
    Member() {}
    QString identityId;
    QString displayName;
    QString nickname;          // only for this server
    QStringList roleIds;       // a member can hold several
    QString roleId;            // the highest one, kept for ordering
    QDateTime joinedAtUtc;
    QDateTime accountCreatedUtc;
    QString joinedWithInvite;
    bool suspended;
};

// An invite code: who made it, what it grants, and who has walked through it.
struct Invite
{
    Invite() : uses(0), maxUses(0) {}
    QString code;
    QString createdBy;
    QString grantsRoleId;
    QDateTime createdAtUtc;
    QDateTime expiresAtUtc;
    int uses;
    int maxUses;               // zero means no limit
    QStringList joinedIdentities;

    bool isExpired(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
};

// A clip anybody in a voice channel can play.
struct Sound
{
    Sound() {}
    QString id;
    QString name;
    QString filePath;
    QString addedBy;
};

struct AuditEntry
{
    AuditEntry() {}
    QDateTime atUtc;
    QString actorName;
    QString description;
};

}

class ServerModel
{
public:
    ServerModel(const MeeruPaths &paths, const QString &identityId, const QString &serverId);

    bool load();
    bool save(QString *error = 0) const;

    QString serverId() const { return serverId_; }
    QString name() const { return name_; }
    QString topic() const { return topic_; }
    void setName(const QString &name) { name_ = name; }
    void setTopic(const QString &topic) { topic_ = topic; }

    QList<Server::Category> categories() const { return categories_; }
    QList<Server::Channel> channels() const { return channels_; }
    QList<Server::Role> roles() const { return roles_; }
    QList<Server::Member> members() const { return members_; }
    QList<Server::AuditEntry> audit() const { return audit_; }
    QList<Server::Invite> invites() const { return invites_; }
    QList<Server::Sound> sounds() const { return sounds_; }

    QString description() const { return description_; }
    QString haloColour() const { return haloColour_; }
    void setDescription(const QString &description) { description_ = description; }
    void setHaloColour(const QString &colour) { haloColour_ = colour; }

    // Everything a member may do, being the union of their roles, with an
    // administrator short-circuiting the lot.
    quint32 permissionsFor(const QString &identityId) const;
    bool may(const QString &identityId, quint32 permission) const;
    int highestRank(const QString &identityId) const;

    void addInvite(const Server::Invite &invite);
    void removeInvite(const QString &code);
    void addSound(const Server::Sound &sound);
    void removeSound(const QString &soundId);

    // Channels of a kind, in the order they should be shown: by category, then
    // by position, the way Discord arranges them.
    QList<Server::Channel> channelsOfKind(int kind) const;
    QList<Server::Channel> threadsOf(const QString &channelId) const;
    Server::Channel channel(const QString &channelId) const;
    Server::Role role(const QString &roleId) const;

    // Members grouped by role, highest rank first, which is the order the
    // member list is drawn in.
    QList<Server::Member> membersByRank() const;

    void addCategory(const Server::Category &category);
    void addChannel(const Server::Channel &channel);
    void removeChannel(const QString &channelId);
    void addRole(const Server::Role &role);
    void removeRole(const QString &roleId);
    void setMemberRole(const QString &identityId, const QString &roleId);
    void setMemberNickname(const QString &identityId, const QString &nickname);
    void setMemberSuspended(const QString &identityId, bool suspended);

    // A member may hold several roles at once; these add and remove one
    // without disturbing the others.
    void addRoleToMember(const QString &identityId, const QString &roleId);
    void removeRoleFromMember(const QString &identityId, const QString &roleId);
    bool memberHasRole(const QString &identityId, const QString &roleId) const;

    // Hands the highest role over. The old owner keeps their place in the
    // server but stops being the one who cannot be overruled.
    bool transferOwnership(const QString &fromIdentityId, const QString &toIdentityId);
    QString ownerIdentityId() const;
    void addMember(const Server::Member &member);
    void removeMember(const QString &identityId);
    void note(const QString &actorName, const QString &description);

    // A brand new server, with the channels people expect to find on arrival.
    static ServerModel createDefault(const MeeruPaths &paths, const QString &identityId,
                                     const QString &serverId, const QString &name,
                                     const QString &topic, const QString &ownerName);

private:
    QString filePath() const;

    MeeruPaths paths_;
    QString identityId_;
    QString serverId_;
    QString name_;
    QString topic_;
    QList<Server::Category> categories_;
    QList<Server::Channel> channels_;
    QList<Server::Role> roles_;
    QList<Server::Member> members_;
    QList<Server::AuditEntry> audit_;
    QList<Server::Invite> invites_;
    QList<Server::Sound> sounds_;
    QString description_;
    QString haloColour_;
};

#endif
