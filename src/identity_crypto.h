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
    static QByteArray profileSignature(const IdentityMaterial &material, const QByteArray &message);
    static bool verifySignature(const QByteArray &edPublic, const QByteArray &message, const QByteArray &signature);

    // Serialisation of the secret half, as stored inside the sealed vault.
    static QByteArray privateBundle(const IdentityMaterial &material);
    static bool fromPrivateBundle(const QByteArray &bundle, IdentityMaterial *material);
    static int privateBundleSize();

    static void wipe(QByteArray *buffer);
};

#endif
