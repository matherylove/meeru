#include "std_ed25519.h"

#include "monocypher.h"
#include "sha512.h"

namespace {
const int kScalarSize = 32;
const int kPublicSize = 32;
const int kSignatureSize = 64;
}

void StdEd25519::keyPair(const QByteArray &seed, QByteArray *publicKey, QByteArray *secretExpanded)
{
    if (!publicKey || !secretExpanded || seed.size() != kScalarSize)
        return;

    const QByteArray expanded = Sha512::hash(seed);   // scalar || prefix, per RFC 8032 5.1.5

    unsigned char scalar[kScalarSize];
    crypto_eddsa_trim_scalar(scalar, reinterpret_cast<const unsigned char *>(expanded.constData()));

    unsigned char point[kPublicSize];
    crypto_eddsa_scalarbase(point, scalar);

    *secretExpanded = QByteArray(reinterpret_cast<char *>(scalar), kScalarSize) + expanded.mid(32, 32);
    *publicKey = QByteArray(reinterpret_cast<char *>(point), kPublicSize);

    crypto_wipe(scalar, sizeof(scalar));
    crypto_wipe(point, sizeof(point));
}

QByteArray StdEd25519::sign(const QByteArray &secretExpanded, const QByteArray &publicKey,
                            const QByteArray &message)
{
    if (secretExpanded.size() != 64 || publicKey.size() != kPublicSize)
        return QByteArray();

    const QByteArray scalarA = secretExpanded.left(32);
    const QByteArray prefix = secretExpanded.mid(32, 32);

    // r = SHA512(prefix || message) mod L
    QByteArray rHashInput = prefix + message;
    const QByteArray rHash = Sha512::hash(rHashInput);
    unsigned char r[kScalarSize];
    crypto_eddsa_reduce(r, reinterpret_cast<const unsigned char *>(rHash.constData()));

    // R = [r]B
    unsigned char R[kPublicSize];
    crypto_eddsa_scalarbase(R, r);

    // h = SHA512(R || A || message) mod L
    QByteArray hInput = QByteArray(reinterpret_cast<char *>(R), kPublicSize) + publicKey + message;
    const QByteArray hHash = Sha512::hash(hInput);
    unsigned char h[kScalarSize];
    crypto_eddsa_reduce(h, reinterpret_cast<const unsigned char *>(hHash.constData()));

    // S = (h * a) + r mod L
    unsigned char S[kScalarSize];
    crypto_eddsa_mul_add(S, h, reinterpret_cast<const unsigned char *>(scalarA.constData()), r);

    QByteArray signature(reinterpret_cast<char *>(R), kPublicSize);
    signature.append(reinterpret_cast<char *>(S), kScalarSize);

    crypto_wipe(r, sizeof(r));
    crypto_wipe(R, sizeof(R));
    crypto_wipe(h, sizeof(h));
    crypto_wipe(S, sizeof(S));
    return signature;
}

bool StdEd25519::verify(const QByteArray &signature, const QByteArray &publicKey, const QByteArray &message)
{
    if (signature.size() != kSignatureSize || publicKey.size() != kPublicSize)
        return false;

    const QByteArray R = signature.left(32);

    // h = SHA512(R || A || message) mod L
    QByteArray hInput = R + publicKey + message;
    const QByteArray hHash = Sha512::hash(hInput);
    unsigned char h[kScalarSize];
    crypto_eddsa_reduce(h, reinterpret_cast<const unsigned char *>(hHash.constData()));

    return crypto_eddsa_check_equation(reinterpret_cast<const unsigned char *>(signature.constData()),
                                       reinterpret_cast<const unsigned char *>(publicKey.constData()),
                                       h) == 0;
}
