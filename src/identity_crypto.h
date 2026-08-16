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
    // It used to be a hash of that key, which reads the same to a user (both
    // are 64 hex characters) but has one fatal drawback: a hash cannot be
    // undone, so knowing somebody's ID told you nothing you could look them
    // up by. Publishing a location in the BitTorrent DHT means storing it
    // under a public key, and a contact who has only your ID has to be able
    // to derive that key. Making the ID the key itself is what lets somebody
    // paste 64 characters and be found anywhere in the world. Tox does the
    // same thing for the same reason.
    static QString identityIdFor(const QByteArray &edPublic);

    // The 64 byte scalar||prefix that signs standard Ed25519 items under this
    // identity's own public key, for the DHT. Derived from the identity, so
    // there is no extra key to generate, store or lose.
    static QByteArray dhtSigningKey(const IdentityMaterial &material);
    static QByteArray profileSignature(const IdentityMaterial &material, const QByteArray &message);
    static bool verifySignature(const QByteArray &edPublic, const QByteArray &message, const QByteArray &signature);

    // Serialisation of the secret half, as stored inside the sealed vault.
    static QByteArray privateBundle(const IdentityMaterial &material);
    static bool fromPrivateBundle(const QByteArray &bundle, IdentityMaterial *material);
    static int privateBundleSize();

    static void wipe(QByteArray *buffer);
};

#endif
