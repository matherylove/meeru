#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <QDateTime>
#include <QString>
#include <QStringList>

#include "meeru_paths.h"

struct AppSettings
{
    AppSettings();

    int formatVersion;
    QString activeIdentityId;
    QString displayName;
    QString presence;
    QString statusText;
    bool startWithWindows;
    QStringList rendezvousHosts;
    qint64 inviteLifetimeSeconds;   // 0 = never expires

    // Advanced networking, for when the router will not cooperate on its own.
    bool useUpnp;
    bool useDht;
    bool dhtFallback;
    QString firewallProfiles;   // "private,domain,public"
    int listenPort;                 // 0 = pick one automatically
    QString publicAddress;          // what the outside world can reach, when forwarded by hand
};

struct UsageData
{
    UsageData();

    int formatVersion;
    int launchCount;
    int identitiesCreated;
    int sessionsStarted;
    QDateTime firstRunUtc;
    QDateTime lastRunUtc;
};

class SettingsStore
{
public:
    explicit SettingsStore(const MeeruPaths &paths);

    AppSettings load() const;
    bool save(const AppSettings &settings, QString *error = 0) const;

    UsageData loadUsage() const;
    bool saveUsage(const UsageData &usage, QString *error = 0) const;
    void recordLaunch() const;

    // Writes or removes the HKCU Run entry. No-op outside Windows.
    static bool setAutoStart(bool enabled, QString *error = 0);
    static bool autoStartEnabled();

private:
    MeeruPaths paths_;
};

#endif
