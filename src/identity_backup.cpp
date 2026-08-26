#include "identity_backup.h"

#include <new>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "identity_crypto.h"
#include "monocypher.h"
#include "secret_vault.h"

namespace {

const char kMagic[] = { 'M', 'E', 'E', 'R', 'U', 'B', 'A', 'K' };
const int kMagicSize = 8;
const quint8 kVersion = 1;
const int kSaltSize = 16;
const int kNonceSize = 24;
const int kMacSize = 16;
const int kKeySize = 32;

// 16 MiB of Argon2 blocks and three passes: heavy enough to matter, light
// enough for the machines Meeru targets.
const quint32 kBlocks = 16384;
const quint32 kPasses = 3;
const quint32 kLanes = 1;

void appendU32(QByteArray *out, quint32 value)
{
    char buffer[4];
    buffer[0] = static_cast<char>(value & 0xFF);
    buffer[1] = static_cast<char>((value >> 8) & 0xFF);
    buffer[2] = static_cast<char>((value >> 16) & 0xFF);
    buffer[3] = static_cast<char>((value >> 24) & 0xFF);
    out->append(buffer, 4);
}

bool readU32(const QByteArray &in, int *offset, quint32 *value)
{
    if (!offset || !value || *offset < 0 || in.size() - *offset < 4)
        return false;
    const unsigned char *data = reinterpret_cast<const unsigned char *>(in.constData()) + *offset;
    *value = static_cast<quint32>(data[0])
           | (static_cast<quint32>(data[1]) << 8)
           | (static_cast<quint32>(data[2]) << 16)
           | (static_cast<quint32>(data[3]) << 24);
    *offset += 4;
    return true;
}

bool readBytes(const QByteArray &in, int *offset, int size, QByteArray *out)
{
    if (!offset || !out || size < 0 || *offset < 0 || in.size() - *offset < size)
        return false;
    *out = in.mid(*offset, size);
    *offset += size;
    return true;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// Argon2id over the passphrase. Returns an empty array when the work area
// cannot be allocated, which is a real possibility on a 256 MB machine.
QByteArray deriveKey(const QByteArray &passphrase, const QByteArray &salt,
                     quint32 blocks, quint32 passes)
{
    unsigned char *workArea = 0;
    const size_t workSize = static_cast<size_t>(blocks) * 1024u;
    workArea = new (std::nothrow) unsigned char[workSize];
    if (!workArea)
        return QByteArray();

    crypto_argon2_config config;
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = blocks;
    config.nb_passes = passes;
    config.nb_lanes = kLanes;

    crypto_argon2_inputs inputs;
    inputs.pass = reinterpret_cast<const unsigned char *>(passphrase.constData());
    inputs.pass_size = static_cast<quint32>(passphrase.size());
    inputs.salt = reinterpret_cast<const unsigned char *>(salt.constData());
    inputs.salt_size = static_cast<quint32>(salt.size());

    unsigned char key[kKeySize];
    crypto_argon2(key, kKeySize, workArea, config, inputs, crypto_argon2_no_extras);

    crypto_wipe(workArea, workSize);
    delete[] workArea;

    QByteArray result(reinterpret_cast<char *>(key), kKeySize);
    crypto_wipe(key, sizeof(key));
    return result;
}

}

int IdentityBackup::memoryKilobytes() { return static_cast<int>(kBlocks); }
int IdentityBackup::passes() { return static_cast<int>(kPasses); }

namespace {
QString exportMarkerPath(const MeeruPaths &paths, const QString &identityId)
{
    return paths.identityDirectory(identityId) + QLatin1String("/backup.json");
}
}

bool IdentityBackup::wasExported(const MeeruPaths &paths, const QString &identityId)
{
    return QFile::exists(exportMarkerPath(paths, identityId));
}

void IdentityBackup::markExported(const MeeruPaths &paths, const QString &identityId)
{
    QJsonObject object;
    object.insert("formatVersion", 1);
    object.insert("exportedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(exportMarkerPath(paths, identityId));
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size())
        file.commit();
}

bool IdentityBackup::exportIdentity(const MeeruPaths &paths,
                                    const QString &identityId,
                                    const QString &passphrase,
                                    const QString &targetFile,
                                    QString *error)
{
    if (passphrase.size() < 8) {
        setError(error, QString::fromLatin1("Use a passphrase of at least 8 characters"));
        return false;
    }

    IdentityStore store(paths);
    IdentityMaterial material;
    if (!store.unlock(identityId, &material, error))
        return false;

    QFile profileFile(paths.identityDirectory(identityId) + QLatin1String("/profile.json"));
    if (!profileFile.open(QIODevice::ReadOnly)) {
        material.clear();
        setError(error, QString::fromLatin1("Cannot read the profile of this identity"));
        return false;
    }
    const QByteArray profileJson = profileFile.readAll();
    profileFile.close();

    QByteArray bundle = IdentityCrypto::privateBundle(material);
    material.clear();
    if (bundle.size() != IdentityCrypto::privateBundleSize()) {
        IdentityCrypto::wipe(&bundle);
        setError(error, QString::fromLatin1("The key material of this identity is incomplete"));
        return false;
    }

    QByteArray payload;
    appendU32(&payload, static_cast<quint32>(profileJson.size()));
    payload.append(profileJson);
    payload.append(bundle);
    IdentityCrypto::wipe(&bundle);

    QByteArray salt;
    QByteArray nonce;
    if (!IdentityCrypto::randomBytes(&salt, kSaltSize) || !IdentityCrypto::randomBytes(&nonce, kNonceSize)) {
        IdentityCrypto::wipe(&payload);
        setError(error, QString::fromLatin1("Secure random source unavailable"));
        return false;
    }

    QByteArray key = deriveKey(passphrase.toUtf8(), salt, kBlocks, kPasses);
    if (key.size() != kKeySize) {
        IdentityCrypto::wipe(&payload);
        setError(error, QString::fromLatin1("Not enough memory to protect the backup"));
        return false;
    }

    QByteArray header(kMagic, kMagicSize);
    header.append(static_cast<char>(kVersion));
    appendU32(&header, kBlocks);
    appendU32(&header, kPasses);
    header.append(salt);

    QByteArray cipher(payload.size(), '\0');
    unsigned char mac[kMacSize];
    crypto_aead_lock(reinterpret_cast<unsigned char *>(cipher.data()),
                     mac,
                     reinterpret_cast<const unsigned char *>(key.constData()),
                     reinterpret_cast<const unsigned char *>(nonce.constData()),
                     reinterpret_cast<const unsigned char *>(header.constData()),
                     static_cast<size_t>(header.size()),
                     reinterpret_cast<const unsigned char *>(payload.constData()),
                     static_cast<size_t>(payload.size()));

    IdentityCrypto::wipe(&key);
    IdentityCrypto::wipe(&payload);

    QByteArray blob = header;
    blob.append(nonce);
    blob.append(reinterpret_cast<char *>(mac), kMacSize);
    appendU32(&blob, static_cast<quint32>(cipher.size()));
    blob.append(cipher);
    crypto_wipe(mac, sizeof(mac));

    QSaveFile file(targetFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(blob) != blob.size() || !file.commit()) {
        setError(error, QString::fromLatin1("Cannot write the backup file"));
        return false;
    }

    markExported(paths, identityId);
    return true;
}

bool IdentityBackup::importIdentity(const MeeruPaths &paths,
                                    const QString &sourceFile,
                                    const QString &passphrase,
                                    LocalProfile *profile,
                                    QString *error)
{
    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QString::fromLatin1("Cannot open that backup file"));
        return false;
    }
    const QByteArray blob = file.readAll();
    file.close();

    int offset = 0;
    QByteArray magic;
    if (!readBytes(blob, &offset, kMagicSize, &magic) || magic != QByteArray(kMagic, kMagicSize)) {
        setError(error, QString::fromLatin1("That file is not a Meeru backup"));
        return false;
    }

    QByteArray versionByte;
    if (!readBytes(blob, &offset, 1, &versionByte) || static_cast<quint8>(versionByte.at(0)) != kVersion) {
        setError(error, QString::fromLatin1("That backup was written by a newer version of Meeru"));
        return false;
    }

    quint32 blocks = 0;
    quint32 rounds = 0;
    QByteArray salt;
    if (!readU32(blob, &offset, &blocks) || !readU32(blob, &offset, &rounds)
        || !readBytes(blob, &offset, kSaltSize, &salt)) {
        setError(error, QString::fromLatin1("That backup file is truncated"));
        return false;
    }
    if (blocks == 0 || blocks > 262144u || rounds == 0 || rounds > 16u) {
        setError(error, QString::fromLatin1("That backup asks for settings Meeru will not honour"));
        return false;
    }

    const QByteArray header = blob.mid(0, offset);

    QByteArray nonce;
    QByteArray mac;
    quint32 cipherSize = 0;
    QByteArray cipher;
    if (!readBytes(blob, &offset, kNonceSize, &nonce)
        || !readBytes(blob, &offset, kMacSize, &mac)
        || !readU32(blob, &offset, &cipherSize)
        || !readBytes(blob, &offset, static_cast<int>(cipherSize), &cipher)) {
        setError(error, QString::fromLatin1("That backup file is truncated"));
        return false;
    }

    QByteArray key = deriveKey(passphrase.toUtf8(), salt, blocks, rounds);
    if (key.size() != kKeySize) {
        setError(error, QString::fromLatin1("Not enough memory to open the backup"));
        return false;
    }

    QByteArray payload(cipher.size(), '\0');
    const int result = crypto_aead_unlock(reinterpret_cast<unsigned char *>(payload.data()),
                                          reinterpret_cast<const unsigned char *>(mac.constData()),
                                          reinterpret_cast<const unsigned char *>(key.constData()),
                                          reinterpret_cast<const unsigned char *>(nonce.constData()),
                                          reinterpret_cast<const unsigned char *>(header.constData()),
                                          static_cast<size_t>(header.size()),
                                          reinterpret_cast<const unsigned char *>(cipher.constData()),
                                          static_cast<size_t>(cipher.size()));
    IdentityCrypto::wipe(&key);

    if (result != 0) {
        IdentityCrypto::wipe(&payload);
        setError(error, QString::fromLatin1("Wrong passphrase, or the backup has been altered"));
        return false;
    }

    int payloadOffset = 0;
    quint32 profileSize = 0;
    QByteArray profileJson;
    QByteArray bundle;
    if (!readU32(payload, &payloadOffset, &profileSize)
        || !readBytes(payload, &payloadOffset, static_cast<int>(profileSize), &profileJson)
        || !readBytes(payload, &payloadOffset, IdentityCrypto::privateBundleSize(), &bundle)) {
        IdentityCrypto::wipe(&payload);
        setError(error, QString::fromLatin1("The backup contents are malformed"));
        return false;
    }

    const QJsonObject object = QJsonDocument::fromJson(profileJson).object();
    const QString identityId = object.value("identityId").toString();
    if (identityId.isEmpty()) {
        IdentityCrypto::wipe(&payload);
        IdentityCrypto::wipe(&bundle);
        setError(error, QString::fromLatin1("The backup does not contain a usable identity"));
        return false;
    }

    QString pathError;
    if (!paths.initialize(&pathError)) {
        IdentityCrypto::wipe(&payload);
        IdentityCrypto::wipe(&bundle);
        setError(error, pathError);
        return false;
    }

    const QString directory = paths.identityDirectory(identityId);
    if (!QDir().mkpath(directory)) {
        IdentityCrypto::wipe(&payload);
        IdentityCrypto::wipe(&bundle);
        setError(error, QString::fromLatin1("Cannot create the identity folder"));
        return false;
    }

    QSaveFile profileFile(directory + QLatin1String("/profile.json"));
    if (!profileFile.open(QIODevice::WriteOnly) || profileFile.write(profileJson) != profileJson.size()
        || !profileFile.commit()) {
        IdentityCrypto::wipe(&payload);
        IdentityCrypto::wipe(&bundle);
        setError(error, QString::fromLatin1("Cannot write the restored profile"));
        return false;
    }

    // Re-seal the secrets for this machine's Windows account.
    const QByteArray sealed = SecretVault::seal(bundle, error);
    IdentityCrypto::wipe(&bundle);
    IdentityCrypto::wipe(&payload);
    if (sealed.isEmpty())
        return false;

    QSaveFile vaultFile(directory + QLatin1String("/private.bin"));
    if (!vaultFile.open(QIODevice::WriteOnly) || vaultFile.write(sealed) != sealed.size() || !vaultFile.commit()) {
        setError(error, QString::fromLatin1("Cannot write the restored identity vault"));
        return false;
    }

    IdentityStore store(paths);
    if (!store.saveActive(identityId, error))
        return false;
    if (profile && !store.loadActive(profile, error))
        return false;
    return true;
}
