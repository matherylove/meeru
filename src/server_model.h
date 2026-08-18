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

struct Role
{
    Role();
    QString id;
    QString name;
    QString colour;        // "#rrggbb"
    int rank;              // higher sits higher in the member list
    bool canManage;

    bool isValid() const;
};

struct Member
{
    Member() {}
    QString identityId;
    QString displayName;
    QString roleId;
    QDateTime joinedAtUtc;
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
};

#endif
