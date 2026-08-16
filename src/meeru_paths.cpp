#include "meeru_paths.h"

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

MeeruPaths::MeeruPaths()
{
#ifdef Q_OS_WIN
    const QByteArray appData = qgetenv("APPDATA");
    if (!appData.isEmpty())
        root_ = QString::fromLocal8Bit(appData) + "/Meeru";
#endif
    if (root_.isEmpty())
        root_ = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Meeru";
    if (root_.isEmpty())
        root_ = QDir::homePath() + "/Meeru";
}

bool MeeruPaths::initialize(QString *error) const
{
    const QStringList dirs = QStringList()
        << root_ << config() << identities() << profiles()
        << messages() << contacts() << attachments()
        << cache() << backups() << logs();
    foreach (const QString &path, dirs) {
        if (!QDir().mkpath(path)) {
            if (error) *error = QString::fromLatin1("Cannot create directory: ") + path;
            return false;
        }
    }
    return true;
}

QString MeeruPaths::root() const { return root_; }
QString MeeruPaths::identities() const { return root_ + "/identities"; }
QString MeeruPaths::profiles() const { return root_ + "/profiles"; }
QString MeeruPaths::config() const { return root_ + "/config"; }
QString MeeruPaths::messages() const { return root_ + "/data/messages"; }
QString MeeruPaths::contacts() const { return root_ + "/data/contacts"; }
QString MeeruPaths::attachments() const { return root_ + "/data/attachments"; }
QString MeeruPaths::cache() const { return root_ + "/cache"; }
QString MeeruPaths::backups() const { return root_ + "/backups"; }
QString MeeruPaths::logs() const { return root_ + "/logs"; }
QString MeeruPaths::activeProfilesFile() const { return profiles() + "/local-profiles.json"; }
QString MeeruPaths::settingsFile() const { return config() + "/settings.json"; }
QString MeeruPaths::usageFile() const { return config() + "/usage.json"; }
QString MeeruPaths::identityDirectory(const QString &identityId) const { return identities() + "/" + identityId; }