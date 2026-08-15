#include "secret_vault.h"

#include "identity_crypto.h"
#include "monocypher.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

const char kMagic[] = { 'M', 'E', 'E', 'R', 'U' };
const int kMagicSize = 5;
const quint8 kVersionLegacyPlain = 1;
const quint8 kVersionSealed = 2;
const quint8 kProtectionNone = 0;
const quint8 kProtectionDevice = 1;
const int kKeySize = 32;
const int kNonceSize = 24;
const int kMacSize = 16;

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

bool deviceProtect(const QByteArray &in, QByteArray *out)
{
#ifdef Q_OS_WIN
    if (!out)
        return false;

    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(in.constData()));
    input.cbData = static_cast<DWORD>(in.size());

    DATA_BLOB output;
    output.pbData = 0;
    output.cbData = 0;

    if (!CryptProtectData(&input, 0, 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;

    *out = QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
#else
    Q_UNUSED(in);
    Q_UNUSED(out);
    return false;
#endif
}

bool deviceUnprotect(const QByteArray &in, QByteArray *out)
{
#ifdef Q_OS_WIN
    if (!out)
        return false;

    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(in.constData()));
    input.cbData = static_cast<DWORD>(in.size());

    DATA_BLOB output;
    output.pbData = 0;
    output.cbData = 0;

    if (!CryptUnprotectData(&input, 0, 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;

    *out = QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
#else
    Q_UNUSED(in);
    Q_UNUSED(out);
    return false;
#endif
}

}

bool SecretVault::deviceProtectionAvailable()
{
#ifdef Q_OS_WIN
    QByteArray probe("meeru", 5);
    QByteArray sealed;
    if (!deviceProtect(probe, &sealed))
        return false;
    QByteArray opened;
    const bool ok = deviceUnprotect(sealed, &opened) && opened == probe;
    IdentityCrypto::wipe(&opened);
    return ok;
#else
    return false;
#endif
}

QByteArray SecretVault::seal(const QByteArray &plain, QString *error, bool *deviceProtected)
{
    if (plain.isEmpty()) {
        if (error)
            *error = QString::fromLatin1("Nothing to protect");
        return QByteArray();
    }

    QByteArray key;
    QByteArray nonce;
    if (!IdentityCrypto::randomBytes(&key, kKeySize) || !IdentityCrypto::randomBytes(&nonce, kNonceSize)) {
        IdentityCrypto::wipe(&key);
        if (error)
            *error = QString::fromLatin1("Secure random source unavailable");
        return QByteArray();
    }

    QByteArray wrapped;
    quint8 protection = kProtectionDevice;
    if (!deviceProtect(key, &wrapped)) {
        protection = kProtectionNone;
        wrapped = key;
    }
    if (deviceProtected)
        *deviceProtected = (protection == kProtectionDevice);

    // Everything before the nonce is authenticated as associated data, so the
    // wrapped key cannot be swapped for another one.
    QByteArray header(kMagic, kMagicSize);
    header.append(static_cast<char>(kVersionSealed));
    header.append(static_cast<char>(protection));
    appendU32(&header, static_cast<quint32>(wrapped.size()));
    header.append(wrapped);

    QByteArray cipher(plain.size(), '\0');
    unsigned char mac[kMacSize];
    crypto_aead_lock(reinterpret_cast<unsigned char *>(cipher.data()),
                     mac,
                     reinterpret_cast<const unsigned char *>(key.constData()),
                     reinterpret_cast<const unsigned char *>(nonce.constData()),
                     reinterpret_cast<const unsigned char *>(header.constData()),
                     static_cast<size_t>(header.size()),
                     reinterpret_cast<const unsigned char *>(plain.constData()),
                     static_cast<size_t>(plain.size()));

    QByteArray blob = header;
    blob.append(nonce);
    blob.append(reinterpret_cast<char *>(mac), kMacSize);
    appendU32(&blob, static_cast<quint32>(cipher.size()));
    blob.append(cipher);

    IdentityCrypto::wipe(&key);
    crypto_wipe(mac, sizeof(mac));
    return blob;
}

bool SecretVault::open(const QByteArray &blob, QByteArray *plain, QString *error, bool *deviceProtected)
{
    if (!plain)
        return false;

    int offset = 0;
    QByteArray magic;
    if (!readBytes(blob, &offset, kMagicSize, &magic) || magic != QByteArray(kMagic, kMagicSize)) {
        if (error)
            *error = QString::fromLatin1("Identity vault is not a Meeru file");
        return false;
    }

    QByteArray versionByte;
    if (!readBytes(blob, &offset, 1, &versionByte)) {
        if (error)
            *error = QString::fromLatin1("Identity vault is truncated");
        return false;
    }
    const quint8 version = static_cast<quint8>(versionByte.at(0));

    if (version == kVersionLegacyPlain) {
        // Files written before the vault format existed: raw secrets.
        *plain = blob.mid(offset);
        if (deviceProtected)
            *deviceProtected = false;
        return !plain->isEmpty();
    }

    if (version != kVersionSealed) {
        if (error)
            *error = QString::fromLatin1("Unsupported identity vault version");
        return false;
    }

    QByteArray protectionByte;
    if (!readBytes(blob, &offset, 1, &protectionByte)) {
        if (error)
            *error = QString::fromLatin1("Identity vault is truncated");
        return false;
    }
    const quint8 protection = static_cast<quint8>(protectionByte.at(0));

    quint32 wrappedSize = 0;
    QByteArray wrapped;
    if (!readU32(blob, &offset, &wrappedSize) || !readBytes(blob, &offset, static_cast<int>(wrappedSize), &wrapped)) {
        if (error)
            *error = QString::fromLatin1("Identity vault is truncated");
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
        if (error)
            *error = QString::fromLatin1("Identity vault is truncated");
        return false;
    }

    QByteArray key;
    if (protection == kProtectionDevice) {
        if (!deviceUnprotect(wrapped, &key)) {
            if (error)
                *error = QString::fromLatin1("This identity belongs to another Windows user account");
            return false;
        }
    } else {
        key = wrapped;
    }
    if (deviceProtected)
        *deviceProtected = (protection == kProtectionDevice);

    if (key.size() != kKeySize) {
        IdentityCrypto::wipe(&key);
        if (error)
            *error = QString::fromLatin1("Identity vault key is malformed");
        return false;
    }

    QByteArray decrypted(cipher.size(), '\0');
    const int result = crypto_aead_unlock(reinterpret_cast<unsigned char *>(decrypted.data()),
                                          reinterpret_cast<const unsigned char *>(mac.constData()),
                                          reinterpret_cast<const unsigned char *>(key.constData()),
                                          reinterpret_cast<const unsigned char *>(nonce.constData()),
                                          reinterpret_cast<const unsigned char *>(header.constData()),
                                          static_cast<size_t>(header.size()),
                                          reinterpret_cast<const unsigned char *>(cipher.constData()),
                                          static_cast<size_t>(cipher.size()));
    IdentityCrypto::wipe(&key);

    if (result != 0) {
        IdentityCrypto::wipe(&decrypted);
        if (error)
            *error = QString::fromLatin1("Identity vault failed its integrity check");
        return false;
    }

    *plain = decrypted;
    return true;
}
