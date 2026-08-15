#include "app_settings.h"

#include "invite_code.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace {

const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char kRunValue[] = "Meeru Messenger";

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeObject(const QString &path, const QJsonObject &object, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        if (error)
            *error = QString::fromLatin1("Cannot write ") + path;
        return false;
    }
    return true;
}

}

AppSettings::AppSettings()
    : formatVersion(1), presence(QString::fromLatin1("available")), startWithWindows(true),
      inviteLifetimeSeconds(Invite::defaultLifetime()),
      useUpnp(true), listenPort(0)
{
}

UsageData::UsageData()
    : formatVersion(1), launchCount(0), identitiesCreated(0), sessionsStarted(0)
{
}

SettingsStore::SettingsStore(const MeeruPaths &paths)
    : paths_(paths)
{
}

AppSettings SettingsStore::load() const
{
    AppSettings settings;
    const QJsonObject object = readObject(paths_.settingsFile());
    if (object.isEmpty())
        return settings;

    settings.formatVersion = object.value("formatVersion").toInt(1);
    settings.activeIdentityId = object.value("activeIdentityId").toString();
    settings.displayName = object.value("displayName").toString();
    if (object.contains("presence"))
        settings.presence = object.value("presence").toString();
    settings.statusText = object.value("statusText").toString();
    const QJsonArray hosts = object.value("rendezvousHosts").toArray();
    for (int i = 0; i < hosts.size(); ++i) {
        const QString host = hosts.at(i).toString().trimmed();
        if (!host.isEmpty())
            settings.rendezvousHosts.append(host);
    }
    settings.startWithWindows = object.value("startWithWindows").toBool(true);
    if (object.contains("inviteLifetimeSeconds")) {
        settings.inviteLifetimeSeconds =
            static_cast<qint64>(object.value("inviteLifetimeSeconds").toDouble());
        if (settings.inviteLifetimeSeconds < 0)
            settings.inviteLifetimeSeconds = 0;
    }
    settings.useUpnp = object.value("useUpnp").toBool(true);
    settings.listenPort = object.value("listenPort").toInt(0);
    if (settings.listenPort < 0 || settings.listenPort > 65535)
        settings.listenPort = 0;
    settings.publicAddress = object.value("publicAddress").toString().trimmed();
    return settings;
}

bool SettingsStore::save(const AppSettings &settings, QString *error) const
{
    QJsonObject object;
    object.insert("formatVersion", settings.formatVersion);
    object.insert("activeIdentityId", settings.activeIdentityId);
    object.insert("displayName", settings.displayName);
    object.insert("presence", settings.presence);
    object.insert("statusText", settings.statusText);
    QJsonArray hosts;
    for (int i = 0; i < settings.rendezvousHosts.size(); ++i)
        hosts.append(settings.rendezvousHosts.at(i));
    object.insert("rendezvousHosts", hosts);
    object.insert("startWithWindows", settings.startWithWindows);
    object.insert("inviteLifetimeSeconds", static_cast<double>(settings.inviteLifetimeSeconds));
    object.insert("useUpnp", settings.useUpnp);
    object.insert("listenPort", settings.listenPort);
    object.insert("publicAddress", settings.publicAddress);
    return writeObject(paths_.settingsFile(), object, error);
}

UsageData SettingsStore::loadUsage() const
{
    UsageData usage;
    const QJsonObject object = readObject(paths_.usageFile());
    if (object.isEmpty())
        return usage;

    usage.formatVersion = object.value("formatVersion").toInt(1);
    usage.launchCount = object.value("launchCount").toInt();
    usage.identitiesCreated = object.value("identitiesCreated").toInt();
    usage.sessionsStarted = object.value("sessionsStarted").toInt();
    usage.firstRunUtc = QDateTime::fromString(object.value("firstRunUtc").toString(), Qt::ISODate);
    usage.lastRunUtc = QDateTime::fromString(object.value("lastRunUtc").toString(), Qt::ISODate);
    return usage;
}

bool SettingsStore::saveUsage(const UsageData &usage, QString *error) const
{
    QJsonObject object;
    object.insert("formatVersion", usage.formatVersion);
    object.insert("launchCount", usage.launchCount);
    object.insert("identitiesCreated", usage.identitiesCreated);
    object.insert("sessionsStarted", usage.sessionsStarted);
    object.insert("firstRunUtc", usage.firstRunUtc.toString(Qt::ISODate));
    object.insert("lastRunUtc", usage.lastRunUtc.toString(Qt::ISODate));
    return writeObject(paths_.usageFile(), object, error);
}

void SettingsStore::recordLaunch() const
{
    UsageData usage = loadUsage();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!usage.firstRunUtc.isValid())
        usage.firstRunUtc = now;
    usage.lastRunUtc = now;
    usage.launchCount += 1;
    saveUsage(usage, 0);
}

bool SettingsStore::setAutoStart(bool enabled, QString *error)
{
#ifdef Q_OS_WIN
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString command = QLatin1Char('"')
                              + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                              + QLatin1Char('"');
        run.setValue(QString::fromLatin1(kRunValue), command);
    } else {
        run.remove(QString::fromLatin1(kRunValue));
    }
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error)
            *error = QString::fromLatin1("Cannot update the Windows startup entry");
        return false;
    }
    return true;
#else
    Q_UNUSED(enabled);
    Q_UNUSED(error);
    return true;
#endif
}

bool SettingsStore::autoStartEnabled()
{
#ifdef Q_OS_WIN
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return !run.value(QString::fromLatin1(kRunValue)).toString().isEmpty();
#else
    return false;
#endif
}
