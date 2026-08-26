#ifndef IDENTITY_CRYPTO_H
#define IDENTITY_CRYPTO_H

#include <QByteArray>
#include <QString>

struct IdentityMaterial
{
    QByteArray edSecret;     // 64 bytes: seed(32) || ed25519 public key(32)
    QByteArray edPublic;     // 32 bytes
    QByteArray xSecret;      // 32 bytes
    QByteArray xPublic;      // 32 bytes
    QByteArray databaseKey;  // 32 bytes, reserved for the local message store

    bool isValid() const;
    void clear();            // wipes every secret buffer in place
};

class IdentityCrypto
{
public:
    static bool generate(IdentityMaterial *material, QString *error = 0);

    // Cryptographically secure bytes: CryptGenRandom on Windows,
    // /dev/urandom elsewhere.
    static bool randomBytes(QByteArray *out, int size);

    static QByteArray deriveId(const QByteArray &domain, const QByteArray &publicKey);

    // A Meeru ID *is* the Ed25519 public key, in hex.
    //
    // It used to be a hash of that key. Both read the same to a user, since
    // both are 64 hex characters, but the key itself carries more: anything
    // signed by that identity can be verified straight from the ID, with
    // nothing else to look up. Tox does the same. The format is kept as it is
    // because identities already exist in it.
    static QString identityIdFor(const QByteArray &edPublic);

    static QByteArray profileSignature(const IdentityMaterial &material, const QByteArray &message);
    static bool verifySignature(const QByteArray &edPublic, const QByteArray &message, const QByteArray &signature);

    // Serialisation of the secret half, as stored inside the sealed vault.
    static QByteArray privateBundle(const IdentityMaterial &material);
    static bool fromPrivateBundle(const QByteArray &bundle, IdentityMaterial *material);
    static int privateBundleSize();

    static void wipe(QByteArray *buffer);
};

#endif
