#ifndef SECRET_VAULT_H
#define SECRET_VAULT_H

#include <QByteArray>
#include <QString>

// On-disk container for the identity's secret key material.
//
// The payload is encrypted with XChaCha20-Poly1305 (Monocypher) using a random
// 32 byte vault key. Because Meeru has no passwords by design, that vault key is
// then wrapped with the Windows DPAPI (CryptProtectData), which binds it to the
// current Windows user account. On Windows XP this is available through
// crypt32.dll. If DPAPI is unavailable the key is stored unwrapped and the
// header records that, so the caller can warn the user.
namespace SecretVault {

QByteArray seal(const QByteArray &plain, QString *error = 0, bool *deviceProtected = 0);
bool open(const QByteArray &blob, QByteArray *plain, QString *error = 0, bool *deviceProtected = 0);
bool deviceProtectionAvailable();

}

#endif
