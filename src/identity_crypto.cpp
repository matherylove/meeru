#include "identity_crypto.h"

#include <QByteArray>
#include <QFile>

#include "monocypher.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
const int kEdSecretSize = 64;
const int kKeySize = 32;
const int kBundleSize = kEdSecretSize + kKeySize + kKeySize;
}

bool IdentityMaterial::isValid() const
{
    return edSecret.size() == kEdSecretSize
        && edPublic.size() == kKeySize
        && xSecret.size() == kKeySize
        && xPublic.size() == kKeySize
        && databaseKey.size() == kKeySize;
}

void IdentityMaterial::clear()
{
    IdentityCrypto::wipe(&edSecret);
    IdentityCrypto::wipe(&edPublic);
    IdentityCrypto::wipe(&xSecret);
    IdentityCrypto::wipe(&xPublic);
    IdentityCrypto::wipe(&databaseKey);
    edSecret.clear();
    edPublic.clear();
    xSecret.clear();
    xPublic.clear();
    databaseKey.clear();
}

void IdentityCrypto::wipe(QByteArray *buffer)
{
    if (buffer && !buffer->isEmpty())
        crypto_wipe(buffer->data(), static_cast<size_t>(buffer->size()));
}

int IdentityCrypto::privateBundleSize()
{
    return kBundleSize;
}

bool IdentityCrypto::randomBytes(QByteArray *out, int size)
{
    if (!out || size <= 0)
        return false;

    QByteArray buffer(size, '\0');
    unsigned char *data = reinterpret_cast<unsigned char *>(buffer.data());

#ifdef Q_OS_WIN
    HCRYPTPROV provider = 0;
    if (CryptAcquireContextW(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        const BOOL ok = CryptGenRandom(provider, static_cast<DWORD>(size), data);
        CryptReleaseContext(provider, 0);
        if (ok) {
            *out = buffer;
            return true;
        }
    }
#endif

    QFile randomFile(QString::fromLatin1("/dev/urandom"));
    if (randomFile.open(QIODevice::ReadOnly)) {
        qint64 total = 0;
        while (total < size) {
            const qint64 got = randomFile.read(buffer.data() + total, size - total);
            if (got <= 0)
                break;
            total += got;
        }
        if (total == size) {
            *out = buffer;
            return true;
        }
    }

    crypto_wipe(buffer.data(), static_cast<size_t>(buffer.size()));
    return false;
}

bool IdentityCrypto::generate(IdentityMaterial *material, QString *error)
{
    if (!material)
        return false;

    QByteArray seed;
    QByteArray xSecret;
    QByteArray databaseKey;
    if (!randomBytes(&seed, kKeySize) || !randomBytes(&xSecret, kKeySize) || !randomBytes(&databaseKey, kKeySize)) {
        wipe(&seed);
        wipe(&xSecret);
        wipe(&databaseKey);
        if (error)
            *error = QString::fromLatin1("Secure random source unavailable");
        return false;
    }

    unsigned char edSecret[kEdSecretSize];
    unsigned char edPublic[kKeySize];
    unsigned char xPublic[kKeySize];

    // crypto_eddsa_key_pair wipes the seed it is given.
    crypto_eddsa_key_pair(edSecret, edPublic, reinterpret_cast<unsigned char *>(seed.data()));
    crypto_x25519_public_key(xPublic, reinterpret_cast<const unsigned char *>(xSecret.constData()));

    material->edSecret = QByteArray(reinterpret_cast<char *>(edSecret), kEdSecretSize);
    material->edPublic = QByteArray(reinterpret_cast<char *>(edPublic), kKeySize);
    material->xSecret = xSecret;
    material->xPublic = QByteArray(reinterpret_cast<char *>(xPublic), kKeySize);
    material->databaseKey = databaseKey;

    crypto_wipe(edSecret, sizeof(edSecret));
    crypto_wipe(edPublic, sizeof(edPublic));
    crypto_wipe(xPublic, sizeof(xPublic));
    wipe(&seed);
    return true;
}

QByteArray IdentityCrypto::deriveId(const QByteArray &domain, const QByteArray &publicKey)
{
    QByteArray input = domain + QByteArray(1, '\0') + publicKey;
    unsigned char hash[32];
    crypto_blake2b(hash, sizeof(hash), reinterpret_cast<const unsigned char *>(input.constData()), static_cast<size_t>(input.size()));
    return QByteArray(reinterpret_cast<char *>(hash), sizeof(hash)).toHex();
}

QByteArray IdentityCrypto::profileSignature(const IdentityMaterial &material, const QByteArray &message)
{
    if (material.edSecret.size() != kEdSecretSize)
        return QByteArray();

    unsigned char signature[64];
    crypto_eddsa_sign(signature,
                      reinterpret_cast<const unsigned char *>(material.edSecret.constData()),
                      reinterpret_cast<const unsigned char *>(message.constData()),
                      static_cast<size_t>(message.size()));
    QByteArray result(reinterpret_cast<char *>(signature), sizeof(signature));
    crypto_wipe(signature, sizeof(signature));
    return result;
}

bool IdentityCrypto::verifySignature(const QByteArray &edPublic, const QByteArray &message, const QByteArray &signature)
{
    if (edPublic.size() != kKeySize || signature.size() != 64)
        return false;

    return crypto_eddsa_check(reinterpret_cast<const unsigned char *>(signature.constData()),
                              reinterpret_cast<const unsigned char *>(edPublic.constData()),
                              reinterpret_cast<const unsigned char *>(message.constData()),
                              static_cast<size_t>(message.size())) == 0;
}

QByteArray IdentityCrypto::privateBundle(const IdentityMaterial &material)
{
    if (!material.isValid())
        return QByteArray();
    return material.edSecret + material.xSecret + material.databaseKey;
}

bool IdentityCrypto::fromPrivateBundle(const QByteArray &bundle, IdentityMaterial *material)
{
    if (!material || bundle.size() != kBundleSize)
        return false;

    material->edSecret = bundle.mid(0, kEdSecretSize);
    material->xSecret = bundle.mid(kEdSecretSize, kKeySize);
    material->databaseKey = bundle.mid(kEdSecretSize + kKeySize, kKeySize);

    // The Ed25519 public key is the second half of the 64 byte secret key,
    // and the X25519 public key is recomputed from its secret scalar.
    material->edPublic = material->edSecret.mid(kKeySize, kKeySize);

    unsigned char xPublic[kKeySize];
    crypto_x25519_public_key(xPublic, reinterpret_cast<const unsigned char *>(material->xSecret.constData()));
    material->xPublic = QByteArray(reinterpret_cast<char *>(xPublic), kKeySize);
    crypto_wipe(xPublic, sizeof(xPublic));

    return material->isValid();
}
