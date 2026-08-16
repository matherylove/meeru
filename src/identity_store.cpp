#include "identity_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "secret_vault.h"

namespace {

QByteArray jsonBytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString text(const QByteArray &data) { return QString::fromLatin1(data.toBase64()); }
QByteArray bytes(const QString &data) { return QByteArray::fromBase64(data.toLatin1()); }

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

}

LocalProfile::LocalProfile()
    : formatVersion(0)
{
}

bool LocalProfile::isValid() const
{
    return formatVersion == 1
        && !identityId.isEmpty()
        && !displayName.isEmpty()
        && edPublic.size() == 32
        && xPublic.size() == 32
        && signature.size() == 64;
}

QString LocalProfile::shortId() const
{
    return identityId.left(12);
}

IdentityStore::IdentityStore(const MeeruPaths &paths)
    : paths_(paths), deviceProtected_(false)
{
}

bool IdentityStore::lastVaultWasDeviceProtected() const
{
    return deviceProtected_;
}

// The signed payload. QJsonObject serialises its keys in a stable sorted
// order, so the same profile always produces the same bytes.
QByteArray IdentityStore::canonicalPayload(const LocalProfile &profile)
{
    QJsonObject object;
    object.insert("formatVersion", profile.formatVersion);
    object.insert("identityId", profile.identityId);
    object.insert("deviceId", profile.deviceId);
    object.insert("displayName", profile.displayName);
    object.insert("presence", profile.presence);
    object.insert("createdAt", profile.createdAt.toString(Qt::ISODate));
    object.insert("updatedAt", profile.updatedAt.toString(Qt::ISODate));
    object.insert("ed25519PublicKey", text(profile.edPublic));
    object.insert("x25519PublicKey", text(profile.xPublic));
    return jsonBytes(object);
}

QString IdentityStore::activeIdentityId() const
{
    QFile file(paths_.activeProfilesFile());
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject registry = QJsonDocument::fromJson(file.readAll()).object();
    return registry.value("activeIdentityId").toString();
}

QList<LocalProfile> IdentityStore::listIdentities() const
{
    QList<LocalProfile> result;
    QDir root(paths_.identities());
    if (!root.exists())
        return result;

    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i < entries.size(); ++i) {
        LocalProfile profile;
        if (loadProfile(entries.at(i), &profile))
            result.append(profile);
    }

    // Newest first, so a freshly created identity leads the carousel.
    for (int i = 0; i < result.size(); ++i) {
        for (int j = i + 1; j < result.size(); ++j) {
            if (result.at(j).createdAt > result.at(i).createdAt)
                result.swap(i, j);
        }
    }
    return result;
}

bool IdentityStore::activate(const QString &identityId, LocalProfile *profile, QString *error) const
{
    LocalProfile found;
    if (!loadProfile(identityId, &found)) {
        setError(error, QString::fromLatin1("That identity could not be read from this computer"));
        return false;
    }
    if (!saveActive(identityId, error))
        return false;
    if (profile)
        *profile = found;
    return true;
}

int IdentityStore::migrateLegacyIdentities() const
{
    QDir root(paths_.identities());
    if (!root.exists())
        return 0;

    int migrated = 0;
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (int i = 0; i < entries.size(); ++i) {
        const QString oldId = entries.at(i);

        QFile file(paths_.identityDirectory(oldId) + QLatin1String("/profile.json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();

        const QByteArray edPublic = bytes(object.value("ed25519PublicKey").toString());
        if (edPublic.size() != 32)
            continue;

        const QString correctId = IdentityCrypto::identityIdFor(edPublic);
        if (correctId.isEmpty() || correctId == oldId)
            continue;   // already in the current format

        // Re-signing needs the private key, so an identity that cannot be
        // unlocked here is left untouched rather than half converted.
        IdentityMaterial material;
        if (!unlock(oldId, &material, 0))
            continue;

        const QString oldDirectory = paths_.identityDirectory(oldId);
        const QString newDirectory = paths_.identityDirectory(correctId);
        if (QDir(newDirectory).exists() || !QDir().rename(oldDirectory, newDirectory)) {
            material.clear();
            continue;
        }

        LocalProfile profile;
        profile.formatVersion = object.value("formatVersion").toInt();
        profile.identityId = correctId;
        profile.deviceId = object.value("deviceId").toString();
        profile.displayName = object.value("displayName").toString();
        profile.presence = object.value("presence").toString();
        profile.createdAt = QDateTime::fromString(object.value("createdAt").toString(), Qt::ISODate);
        profile.updatedAt = QDateTime::currentDateTimeUtc();
        profile.edPublic = edPublic;
        profile.xPublic = bytes(object.value("x25519PublicKey").toString());
        profile.signature = IdentityCrypto::profileSignature(material, canonicalPayload(profile));
        material.clear();

        if (profile.signature.size() != 64 || !writeProfile(profile, 0)) {
            // Put it back where it was rather than leaving it stranded.
            QDir().rename(newDirectory, oldDirectory);
            continue;
        }

        if (activeIdentityId() == oldId)
            saveActive(correctId, 0);

        ++migrated;
    }

    return migrated;
}

bool IdentityStore::deleteIdentity(const QString &identityId, QString *error) const
{
    QDir directory(paths_.identityDirectory(identityId));
    if (directory.exists() && !directory.removeRecursively()) {
        setError(error, QString::fromLatin1("Some files of that identity are in use and could not be erased"));
        return false;
    }

    if (activeIdentityId() == identityId) {
        const QList<LocalProfile> remaining = listIdentities();
        if (remaining.isEmpty()) {
            QFile::remove(paths_.activeProfilesFile());
        } else if (!saveActive(remaining.first().identityId, error)) {
            return false;
        }
    }
    return true;
}

bool IdentityStore::create(const QString &displayName, const QString &presence, LocalProfile *profile, QString *error)
{
    if (!profile) {
        setError(error, QString::fromLatin1("Internal error: no profile target"));
        return false;
    }
    if (displayName.trimmed().isEmpty()) {
        setError(error, QString::fromLatin1("A display name is required"));
        return false;
    }

    QString pathError;
    if (!paths_.initialize(&pathError)) {
        setError(error, pathError);
        return false;
    }

    IdentityMaterial material;
    if (!IdentityCrypto::generate(&material, error))
        return false;

    LocalProfile result;
    result.formatVersion = 1;
    result.identityId = IdentityCrypto::identityIdFor(material.edPublic);
    result.deviceId = QString::fromLatin1(IdentityCrypto::deriveId("meeru-device", material.xPublic));
    result.displayName = displayName.trimmed();
    result.presence = presence;
    result.createdAt = QDateTime::currentDateTimeUtc();
    result.updatedAt = result.createdAt;
    result.edPublic = material.edPublic;
    result.xPublic = material.xPublic;
    result.signature = IdentityCrypto::profileSignature(material, canonicalPayload(result));

    if (result.signature.size() != 64) {
        material.clear();
        setError(error, QString::fromLatin1("Could not sign the local profile"));
        return false;
    }

    if (!writeVault(result.identityId, material, error)) {
        material.clear();
        return false;
    }
    material.clear();

    if (!writeProfile(result, error))
        return false;
    if (!saveActive(result.identityId, error))
        return false;

    *profile = result;
    return true;
}

bool IdentityStore::updateActive(const QString &displayName, const QString &presence, LocalProfile *profile, QString *error)
{
    if (!profile) {
        setError(error, QString::fromLatin1("Internal error: no profile target"));
        return false;
    }
    if (displayName.trimmed().isEmpty()) {
        setError(error, QString::fromLatin1("A display name is required"));
        return false;
    }

    LocalProfile current;
    if (!loadActive(&current, error))
        return false;

    IdentityMaterial material;
    if (!unlock(current.identityId, &material, error))
        return false;

    if (material.edPublic != current.edPublic) {
        material.clear();
        setError(error, QString::fromLatin1("Stored keys do not match this profile"));
        return false;
    }

    current.displayName = displayName.trimmed();
    current.presence = presence;
    current.updatedAt = QDateTime::currentDateTimeUtc();
    current.signature = IdentityCrypto::profileSignature(material, canonicalPayload(current));
    material.clear();

    if (current.signature.size() != 64) {
        setError(error, QString::fromLatin1("Could not sign the local profile"));
        return false;
    }

    if (!writeProfile(current, error))
        return false;

    *profile = current;
    return true;
}

bool IdentityStore::writeProfile(const LocalProfile &profile, QString *error) const
{
    const QString directory = paths_.identityDirectory(profile.identityId);
    if (!QDir().mkpath(directory)) {
        setError(error, QString::fromLatin1("Cannot create the identity folder: ") + directory);
        return false;
    }

    QJsonObject object;
    object.insert("formatVersion", profile.formatVersion);
    object.insert("identityId", profile.identityId);
    object.insert("deviceId", profile.deviceId);
    object.insert("displayName", profile.displayName);
    object.insert("presence", profile.presence);
    object.insert("createdAt", profile.createdAt.toString(Qt::ISODate));
    object.insert("updatedAt", profile.updatedAt.toString(Qt::ISODate));
    object.insert("ed25519PublicKey", text(profile.edPublic));
    object.insert("x25519PublicKey", text(profile.xPublic));
    object.insert("signature", text(profile.signature));

    QSaveFile file(directory + "/profile.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(jsonBytes(object)) < 0 || !file.commit()) {
        setError(error, QString::fromLatin1("Cannot write profile.json"));
        return false;
    }
    return true;
}

bool IdentityStore::writeVault(const QString &identityId, const IdentityMaterial &material, QString *error) const
{
    const QString directory = paths_.identityDirectory(identityId);
    if (!QDir().mkpath(directory)) {
        setError(error, QString::fromLatin1("Cannot create the identity folder: ") + directory);
        return false;
    }

    QByteArray bundle = IdentityCrypto::privateBundle(material);
    if (bundle.size() != IdentityCrypto::privateBundleSize()) {
        setError(error, QString::fromLatin1("Key material is incomplete"));
        return false;
    }

    bool protectedByDevice = false;
    QByteArray sealed = SecretVault::seal(bundle, error, &protectedByDevice);
    IdentityCrypto::wipe(&bundle);
    if (sealed.isEmpty())
        return false;

    deviceProtected_ = protectedByDevice;

    QSaveFile file(directory + "/private.bin");
    if (!file.open(QIODevice::WriteOnly) || file.write(sealed) != sealed.size() || !file.commit()) {
        setError(error, QString::fromLatin1("Cannot write the identity vault"));
        return false;
    }
    return true;
}

bool IdentityStore::unlock(const QString &identityId, IdentityMaterial *material, QString *error) const
{
    if (!material) {
        setError(error, QString::fromLatin1("Internal error: no key material target"));
        return false;
    }

    QFile file(paths_.identityDirectory(identityId) + "/private.bin");
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QString::fromLatin1("Cannot open the identity vault"));
        return false;
    }
    const QByteArray blob = file.readAll();
    file.close();

    QByteArray bundle;
    bool protectedByDevice = false;
    if (!SecretVault::open(blob, &bundle, error, &protectedByDevice))
        return false;

    deviceProtected_ = protectedByDevice;

    const bool ok = IdentityCrypto::fromPrivateBundle(bundle, material);
    IdentityCrypto::wipe(&bundle);
    if (!ok) {
        setError(error, QString::fromLatin1("The identity vault contents are malformed"));
        return false;
    }
    return true;
}

bool IdentityStore::saveActive(const QString &identityId, QString *error) const
{
    QJsonObject object;
    object.insert("version", 1);
    object.insert("activeIdentityId", identityId);

    QSaveFile file(paths_.activeProfilesFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(jsonBytes(object)) < 0 || !file.commit()) {
        setError(error, QString::fromLatin1("Cannot save the local profile registry"));
        return false;
    }
    return true;
}

bool IdentityStore::loadActive(LocalProfile *profile, QString *error) const
{
    if (!profile)
        return false;

    QFile file(paths_.activeProfilesFile());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QString::fromLatin1("No local identity has been created yet"));
        return false;
    }

    const QJsonObject registry = QJsonDocument::fromJson(file.readAll()).object();
    const QString id = registry.value("activeIdentityId").toString();
    if (id.isEmpty() || !loadProfile(id, profile)) {
        setError(error, QString::fromLatin1("The active local identity is invalid"));
        return false;
    }
    return true;
}

bool IdentityStore::loadProfile(const QString &identityId, LocalProfile *profile) const
{
    QFile file(paths_.identityDirectory(identityId) + "/profile.json");
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("identityId").toString() != identityId)
        return false;

    LocalProfile result;
    result.formatVersion = object.value("formatVersion").toInt();
    result.identityId = identityId;
    result.deviceId = object.value("deviceId").toString();
    result.displayName = object.value("displayName").toString();
    result.presence = object.value("presence").toString();
    result.createdAt = QDateTime::fromString(object.value("createdAt").toString(), Qt::ISODate);
    result.updatedAt = QDateTime::fromString(object.value("updatedAt").toString(), Qt::ISODate);
    if (!result.updatedAt.isValid())
        result.updatedAt = result.createdAt;
    result.edPublic = bytes(object.value("ed25519PublicKey").toString());
    result.xPublic = bytes(object.value("x25519PublicKey").toString());
    result.signature = bytes(object.value("signature").toString());

    if (!result.isValid())
        return false;

    // A profile that does not verify against its own key has been tampered
    // with or corrupted; refuse it rather than logging the user in.
    if (!IdentityCrypto::verifySignature(result.edPublic, canonicalPayload(result), result.signature))
        return false;

    // The ID has to be this profile's own public key. Without this check an
    // identity in an older format loads happily and then fails every single
    // connection, because the far end derives the ID from the key and gets a
    // different answer.
    if (IdentityCrypto::identityIdFor(result.edPublic) != result.identityId)
        return false;

    *profile = result;
    return true;
}
