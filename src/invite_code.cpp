#include "invite_code.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

namespace {

const char kPrefix[] = "meeru-invite:";
const int kMaxCodeBytes = 8 * 1024;

// The bytes that get signed. QJsonObject writes its keys in a fixed order, so
// the same card always produces the same payload on both sides.
QByteArray payloadOf(const Invite::Card &card)
{
    QJsonArray endpoints;
    for (int i = 0; i < card.endpoints.size(); ++i)
        endpoints.append(card.endpoints.at(i));

    QJsonObject object;
    object.insert("formatVersion", 1);
    object.insert("identityId", card.identityId);
    object.insert("edPublic", QString::fromLatin1(card.edPublic.toBase64()));
    object.insert("displayName", card.displayName);
    object.insert("endpoints", endpoints);
    object.insert("issuedAtUtc", card.issuedAtUtc.toString(Qt::ISODate));
    object.insert("lifetimeSeconds", static_cast<double>(card.lifetimeSeconds));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

}

Invite::Card::Card()
    : lifetimeSeconds(0)
{
}

bool Invite::Card::isSigned() const
{
    return !identityId.isEmpty()
        && edPublic.size() == 32
        && signature.size() == 64
        && issuedAtUtc.isValid();
}

QDateTime Invite::Card::expiresAtUtc() const
{
    if (neverExpires() || !issuedAtUtc.isValid())
        return QDateTime();
    return issuedAtUtc.addSecs(lifetimeSeconds);
}

bool Invite::Card::isExpired(const QDateTime &now) const
{
    if (neverExpires())
        return false;
    const QDateTime expiry = expiresAtUtc();
    return expiry.isValid() && now > expiry;
}

QList<qint64> Invite::offeredLifetimes()
{
    QList<qint64> values;
    values.append(600);          // 10 minutes
    values.append(3600);         // 1 hour
    values.append(86400);        // 1 day
    values.append(2592000);      // 30 days
    values.append(0);            // never
    return values;
}

qint64 Invite::defaultLifetime()
{
    return 600;
}

QString Invite::lifetimeLabel(qint64 seconds)
{
    if (seconds <= 0)
        return QString::fromLatin1("Never expires");
    if (seconds < 3600)
        return QString::fromLatin1("%1 minutes").arg(seconds / 60);
    if (seconds < 86400)
        return QString::fromLatin1("%1 hour%2").arg(seconds / 3600)
                   .arg(seconds / 3600 == 1 ? QString() : QString::fromLatin1("s"));
    if (seconds < 2592000)
        return QString::fromLatin1("%1 day%2").arg(seconds / 86400)
                   .arg(seconds / 86400 == 1 ? QString() : QString::fromLatin1("s"));
    return QString::fromLatin1("%1 days").arg(seconds / 86400);
}

QString Invite::lifetimeWarning(qint64 seconds)
{
    if (seconds <= 0) {
        return QString::fromLatin1(
            "This code keeps working forever. That is the right choice on a machine whose address "
            "does not change, such as one with a fixed IP. On an ordinary home connection the "
            "address changes every so often, and from that moment the code sends people nowhere "
            "until you share a new one. Anyone who keeps a copy can also try to reach you at that "
            "address for as long as it stays yours.");
    }
    if (seconds >= 2592000) {
        return QString::fromLatin1(
            "A month is a long time for a home address to stay the same. Expect to hand out a new "
            "code if your router restarts or your provider moves you.");
    }
    if (seconds >= 86400) {
        return QString::fromLatin1(
            "A day suits a connection that keeps the same address between restarts.");
    }
    if (seconds >= 3600) {
        return QString::fromLatin1(
            "An hour is comfortable for sending the code and waiting for an answer.");
    }
    return QString::fromLatin1(
        "Short lived and safest: the code is only good while you are both at it. If your friend "
        "takes longer, share a fresh one.");
}

QString Invite::build(const LocalProfile &profile,
                      const IdentityMaterial &material,
                      const QStringList &endpoints,
                      qint64 lifetimeSeconds)
{
    Card card;
    card.identityId = profile.identityId;
    card.edPublic = material.edPublic;
    card.displayName = profile.displayName;
    card.endpoints = endpoints;
    card.issuedAtUtc = QDateTime::currentDateTimeUtc();
    card.lifetimeSeconds = lifetimeSeconds > 0 ? lifetimeSeconds : 0;
    card.signature = IdentityCrypto::profileSignature(material, payloadOf(card));

    if (card.signature.size() != 64)
        return QString();
    return encode(card);
}

QString Invite::encode(const Card &card)
{
    QJsonObject object = QJsonDocument::fromJson(payloadOf(card)).object();
    object.insert("signature", QString::fromLatin1(card.signature.toBase64()));

    const QByteArray packed = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(kPrefix) + QString::fromLatin1(packed.toBase64());
}

bool Invite::looksLikeCode(const QString &text)
{
    return text.trimmed().startsWith(QString::fromLatin1(kPrefix), Qt::CaseInsensitive);
}

bool Invite::decode(const QString &text, Card *card, QString *error)
{
    if (!card)
        return false;

    QString body = text.trimmed();
    if (!looksLikeCode(body)) {
        if (error)
            *error = QString::fromLatin1("That is not a Meeru invite code");
        return false;
    }
    body = body.mid(static_cast<int>(qstrlen(kPrefix))).trimmed();
    body.remove(QLatin1Char('\n'));
    body.remove(QLatin1Char('\r'));
    body.remove(QLatin1Char(' '));

    const QByteArray packed = QByteArray::fromBase64(body.toLatin1());
    if (packed.isEmpty() || packed.size() > kMaxCodeBytes) {
        if (error)
            *error = QString::fromLatin1("That invite code is damaged");
        return false;
    }

    const QJsonObject object = QJsonDocument::fromJson(packed).object();
    if (object.isEmpty()) {
        if (error)
            *error = QString::fromLatin1("That invite code is damaged");
        return false;
    }

    Card result;
    result.identityId = object.value("identityId").toString();
    result.edPublic = QByteArray::fromBase64(object.value("edPublic").toString().toLatin1());
    result.displayName = object.value("displayName").toString();
    result.issuedAtUtc = QDateTime::fromString(object.value("issuedAtUtc").toString(), Qt::ISODate);
    result.lifetimeSeconds = static_cast<qint64>(object.value("lifetimeSeconds").toDouble());
    result.signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());

    const QJsonArray endpoints = object.value("endpoints").toArray();
    for (int i = 0; i < endpoints.size(); ++i) {
        const QString endpoint = endpoints.at(i).toString().trimmed();
        if (!endpoint.isEmpty())
            result.endpoints.append(endpoint);
    }

    if (!result.isSigned()) {
        if (error)
            *error = QString::fromLatin1("That invite code is incomplete");
        return false;
    }

    // The ID is the hash of the key, and the key signed the addresses. Without
    // both checks anyone could hand out a card pointing at a machine of theirs
    // while wearing somebody else's ID.
    if (IdentityCrypto::identityIdFor(result.edPublic) != result.identityId) {
        if (error)
            *error = QString::fromLatin1("That invite code does not match its own identity");
        return false;
    }
    if (!IdentityCrypto::verifySignature(result.edPublic, payloadOf(result), result.signature)) {
        if (error)
            *error = QString::fromLatin1("That invite code has been altered since it was created");
        return false;
    }

    *card = result;
    return true;
}
