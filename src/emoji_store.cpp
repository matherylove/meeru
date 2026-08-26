#include "emoji_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>

namespace {
const int kMaximumSize = 256;
}

int EmojiStore::maximumSize()
{
    return kMaximumSize;
}

// Only letters, digits and underscore survive, so a name is always safe both
// as :text: in a message and as a file on disk.
QString EmojiStore::sanitiseName(const QString &proposed)
{
    QString name;
    const QString trimmed = proposed.trimmed().toLower();
    for (int i = 0; i < trimmed.size() && name.size() < 32; ++i) {
        const QChar character = trimmed.at(i);
        if (character.isLetterOrNumber() || character == QLatin1Char('_'))
            name.append(character);
        else if (character == QLatin1Char(' ') || character == QLatin1Char('-'))
            name.append(QLatin1Char('_'));
    }
    return name;
}

EmojiStore::EmojiStore(const MeeruPaths &paths, const QString &identityId)
    : paths_(paths), identityId_(identityId)
{
}

EmojiStore::EmojiStore(const QString &directory)
    : explicitDirectory_(directory)
{
}

QString EmojiStore::directory() const
{
    if (!explicitDirectory_.isEmpty())
        return explicitDirectory_;
    return paths_.identityDirectory(identityId_) + QLatin1String("/emoji");
}

// Authors live in a small side file rather than in the picture, so nothing has
// to be re-encoded to record who contributed what.
QString EmojiStore::authorOf(const QString &name) const
{
    QFile file(directory() + QLatin1String("/authors.json"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    return object.value(name).toString();
}

void EmojiStore::setAuthor(const QString &name, const QString &author)
{
    const QString path = directory() + QLatin1String("/authors.json");
    QJsonObject object;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        object = QJsonDocument::fromJson(existing.readAll()).object();
        existing.close();
    }
    object.insert(name, author);

    QDir().mkpath(directory());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QList<CustomEmoji> EmojiStore::all() const
{
    QList<CustomEmoji> emoji;
    QDir folder(directory());
    if (!folder.exists())
        return emoji;

    QStringList filters;
    filters << QString::fromLatin1("*.png") << QString::fromLatin1("*.gif");
    const QStringList files = folder.entryList(filters, QDir::Files, QDir::Name);

    for (int i = 0; i < files.size(); ++i) {
        CustomEmoji entry;
        entry.name = QFileInfo(files.at(i)).completeBaseName();
        entry.filePath = folder.filePath(files.at(i));
        entry.animated = files.at(i).endsWith(QLatin1String(".gif"), Qt::CaseInsensitive);
        if (entry.isValid())
            emoji.append(entry);
    }
    return emoji;
}

CustomEmoji EmojiStore::byName(const QString &name) const
{
    const QList<CustomEmoji> entries = all();
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).name == name)
            return entries.at(i);
    }
    return CustomEmoji();
}

bool EmojiStore::contains(const QString &name) const
{
    return byName(name).isValid();
}

bool EmojiStore::add(const QString &name, const QString &sourcePath, const QRect &crop,
                     bool animated, QString *error)
{
    const QString clean = sanitiseName(name);
    if (clean.isEmpty()) {
        if (error)
            *error = QString::fromLatin1("Give the emoji a name using letters or numbers");
        return false;
    }
    if (!QDir().mkpath(directory())) {
        if (error)
            *error = QString::fromLatin1("Cannot create the emoji folder");
        return false;
    }

    // Whatever the user picked, what gets stored is square.
    QRect square = crop;
    if (square.width() != square.height()) {
        const int side = qMin(square.width(), square.height());
        square.setWidth(side);
        square.setHeight(side);
    }

    const QString png = directory() + QLatin1Char('/') + clean + QLatin1String(".png");
    const QString gif = directory() + QLatin1Char('/') + clean + QLatin1String(".gif");
    QFile::remove(png);
    QFile::remove(gif);

    if (animated) {
        // Qt cannot write GIF, so the original is kept and the crop is applied
        // while it plays, the same approach used for animated avatars.
        if (!QFile::copy(sourcePath, gif)) {
            if (error)
                *error = QString::fromLatin1("Cannot store that animation");
            return false;
        }
        return true;
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = QString::fromLatin1("That picture could not be read");
        return false;
    }

    if (square.isValid())
        image = image.copy(square.intersected(QRect(QPoint(0, 0), image.size())));
    if (image.width() > kMaximumSize || image.height() > kMaximumSize) {
        image = image.scaled(kMaximumSize, kMaximumSize,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (!image.save(png, "PNG")) {
        if (error)
            *error = QString::fromLatin1("Cannot store that emoji");
        return false;
    }
    return true;
}

bool EmojiStore::remove(const QString &name, QString *error)
{
    const CustomEmoji entry = byName(sanitiseName(name));
    if (!entry.isValid()) {
        if (error)
            *error = QString::fromLatin1("There is no emoji with that name");
        return false;
    }
    if (!QFile::remove(entry.filePath)) {
        if (error)
            *error = QString::fromLatin1("That emoji file is in use");
        return false;
    }
    return true;
}
