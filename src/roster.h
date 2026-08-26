#ifndef MEERU_ROSTER_H
#define MEERU_ROSTER_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "meeru_paths.h"

// Jami-style vocabulary: a contact is reached by the hex fingerprint of its
// identity key, and adding one sends a trust request that stays pending until
// the other side accepts. Conversations are swarms: a one-to-one swarm for a
// single contact, a group swarm for several.
namespace Roster {

enum ContactState {
    ContactPendingOutgoing = 0,   // we asked, waiting for them
    ContactPendingIncoming = 1,   // they asked, waiting for us
    ContactAccepted = 2,
    ContactBlocked = 3
};

enum ServerState {
    ServerJoined = 0,
    ServerPending = 1
};

struct Contact
{
    Contact();
    QString id;
    QString displayName;
    QString statusText;
    QString presence;
    int state;
    bool favorite;
    QDateTime addedAtUtc;
    QString endpointHint;     // host:port the user was given, when there was one
    QDateTime lastSeenUtc;

    QString bestName() const;     // display name, or a short id
};

struct Conversation
{
    Conversation();
    QString id;
    QString title;
    QStringList members;          // contact ids, excluding the local identity
    bool group;
    bool favorite;
    QString preview;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
};

struct Server
{
    Server();
    QString id;
    QString name;
    QString topic;
    int state;
    bool owner;
    bool favorite;
    QDateTime joinedAtUtc;
};

// 64 lowercase hex characters, matching IdentityCrypto::deriveId output.
bool isValidIdentityId(const QString &value);

// Accepts "meeru:<id>", "<id>" and surrounding whitespace. Empty on failure.
QString normaliseIdentityId(const QString &value);

QString newLocalId();

}

class RosterStore
{
public:
    RosterStore(const MeeruPaths &paths, const QString &identityId);

    bool load(QString *error = 0);
    bool save(QString *error = 0) const;

    QList<Roster::Contact> contacts() const { return contacts_; }
    QList<Roster::Conversation> conversations() const { return conversations_; }
    QList<Roster::Server> servers() const { return servers_; }

    QList<Roster::Contact> acceptedContacts() const;
    int pendingRequestCount() const;

    bool hasContact(const QString &id) const;
    Roster::Contact contact(const QString &id) const;

    bool addContact(const Roster::Contact &contact, QString *error = 0);
    bool removeContact(const QString &id, QString *error = 0);
    bool setContactState(const QString &id, int state, QString *error = 0);
    bool setContactFavorite(const QString &id, bool favorite, QString *error = 0);

    // Applied when a profile arrives over the network.
    bool updateContactProfile(const QString &id, const QString &displayName,
                              const QString &presence, const QString &statusText, QString *error = 0);
    bool touchContact(const QString &id, QString *error = 0);

    bool addConversation(const Roster::Conversation &conversation, QString *error = 0);
    bool removeConversation(const QString &id, QString *error = 0);
    bool renameConversation(const QString &id, const QString &title, QString *error = 0);
    bool setConversationFavorite(const QString &id, bool favorite, QString *error = 0);
    Roster::Conversation conversationWithMembers(const QStringList &members) const;

    bool addServer(const Roster::Server &server, QString *error = 0);
    bool removeServer(const QString &id, QString *error = 0);
    bool setServerFavorite(const QString &id, bool favorite, QString *error = 0);

    // Title shown for a conversation that has no explicit name.
    QString conversationTitle(const Roster::Conversation &conversation) const;

private:
    QString filePath() const;

    MeeruPaths paths_;
    QString identityId_;
    QList<Roster::Contact> contacts_;
    QList<Roster::Conversation> conversations_;
    QList<Roster::Server> servers_;
};

#endif
