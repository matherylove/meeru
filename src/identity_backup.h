#ifndef IDENTITY_BACKUP_H
#define IDENTITY_BACKUP_H

#include <QString>

#include "identity_store.h"
#include "meeru_paths.h"

// Portable identity file (.meeruid).
//
// The vault on disk is wrapped with DPAPI, which is bound to one Windows
// account, so it cannot simply be copied to another machine. A backup instead
// derives a key from a passphrase with Argon2id and seals the profile and the
// secret keys with XChaCha20-Poly1305. This is the one place in Meeru where the
// user does have a secret to derive from.
namespace IdentityBackup {

// Argon2 settings, kept modest so a Pentium III still finishes in seconds.
int memoryKilobytes();
int passes();

bool exportIdentity(const MeeruPaths &paths,
                    const QString &identityId,
                    const QString &passphrase,
                    const QString &targetFile,
                    QString *error = 0);

// Remembers that the user exported this identity at least once, so the log out
// warning can tell them whether anything would actually be recoverable.
bool wasExported(const MeeruPaths &paths, const QString &identityId);
void markExported(const MeeruPaths &paths, const QString &identityId);

bool importIdentity(const MeeruPaths &paths,
                    const QString &sourceFile,
                    const QString &passphrase,
                    LocalProfile *profile,
                    QString *error = 0);

}

#endif
