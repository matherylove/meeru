#ifndef IDENTITY_STORE_H
#define IDENTITY_STORE_H

#include <QDateTime>
#include <QString>
#include <QList>

#include "identity_crypto.h"
#include "meeru_paths.h"

struct LocalProfile
{
    LocalProfile();

    int formatVersion;
    QString identityId;
    QString deviceId;
    QString displayName;
    QString presence;
    QDateTime createdAt;
    QDateTime updatedAt;
    QByteArray edPublic;
    QByteArray xPublic;
    QByteArray signature;

    bool isValid() const;
    QString shortId() const;   // first 12 hex characters, for the UI
};

class IdentityStore
{
public:
    explicit IdentityStore(const MeeruPaths &paths);

    bool hasActiveIdentity() const;
    QString activeIdentityId() const;

    // Every identity stored on this computer, newest first.
    QList<LocalProfile> listIdentities() const;

    // Switches which identity the login screen opens by default.
    bool activate(const QString &identityId, LocalProfile *profile, QString *error = 0) const;

    // Erases an identity from this computer, keys included.
    bool deleteIdentity(const QString &identityId, QString *error) const;

    // Creates a brand new identity (key generation + sealed vault + registry).
    bool create(const QString &displayName, const QString &presence, LocalProfile *profile, QString *error = 0);

    // Re-signs the existing active identity with a new display name / presence.
    // Requires unsealing the vault, since the profile is signed with Ed25519.
    bool updateActive(const QString &displayName, const QString &presence, LocalProfile *profile, QString *error = 0);

    bool loadActive(LocalProfile *profile, QString *error = 0) const;
    bool saveActive(const QString &identityId, QString *error = 0) const;

    // Unseals the secret key material. Caller must call material->clear().
    bool unlock(const QString &identityId, IdentityMaterial *material, QString *error = 0) const;

    bool lastVaultWasDeviceProtected() const;

private:
    MeeruPaths paths_;
    mutable bool deviceProtected_;

    static QByteArray canonicalPayload(const LocalProfile &profile);

    bool writeProfile(const LocalProfile &profile, QString *error) const;
    bool writeVault(const QString &identityId, const IdentityMaterial &material, QString *error) const;
    bool loadProfile(const QString &identityId, LocalProfile *profile) const;
};

#endif
