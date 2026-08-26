#ifndef MEERU_INVITE_CODE_H
#define MEERU_INVITE_CODE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "identity_crypto.h"
#include "identity_store.h"

// An invite code is a signed "here is who I am and where to find me".
//
// A bare Meeru ID says who somebody is but not how to reach them, which is
// enough on a local network and useless across the internet. The code adds the
// addresses this device answers on, signed with the identity key so nobody can
// hand out a card that points somewhere else.
//
// Addresses go stale when a router hands out a new IP, which is what the
// lifetime is for. It is the user's call: a machine with a fixed address can
// publish a code that never expires, while a home connection that changes
// address every night should not.
namespace Invite {

struct Card
{
    Card();

    QString identityId;
    QByteArray edPublic;
    QString displayName;
    QStringList endpoints;       // host:port, best route first
    QDateTime issuedAtUtc;
    qint64 lifetimeSeconds;      // 0 means it never expires
    QByteArray signature;

    bool isSigned() const;
    bool neverExpires() const { return lifetimeSeconds <= 0; }
    QDateTime expiresAtUtc() const;
    bool isExpired(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
};

// Lifetimes offered in the interface, in seconds. 0 is "never".
QList<qint64> offeredLifetimes();
QString lifetimeLabel(qint64 seconds);
QString lifetimeWarning(qint64 seconds);
qint64 defaultLifetime();

QString build(const LocalProfile &profile,
              const IdentityMaterial &material,
              const QStringList &endpoints,
              qint64 lifetimeSeconds);

QString encode(const Card &card);
bool decode(const QString &text, Card *card, QString *error);

// True when the text looks like an invite code rather than a bare ID.
bool looksLikeCode(const QString &text);

}

#endif
