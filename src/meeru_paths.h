#ifndef MEERU_PATHS_H
#define MEERU_PATHS_H

#include <QString>

class MeeruPaths
{
public:
    MeeruPaths();
    bool initialize(QString *error = 0) const;
    QString root() const;
    QString identities() const;
    QString profiles() const;
    QString config() const;
    QString messages() const;
    QString contacts() const;
    QString attachments() const;
    QString cache() const;
    QString backups() const;
    QString logs() const;
    QString activeProfilesFile() const;
    QString settingsFile() const;
    QString usageFile() const;
    QString identityDirectory(const QString &identityId) const;

private:
    QString root_;
};

#endif