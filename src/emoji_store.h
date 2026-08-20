#ifndef MEERU_EMOJI_STORE_H
#define MEERU_EMOJI_STORE_H

#include <QList>
#include <QPixmap>
#include <QString>

#include "meeru_paths.h"

// Custom emoji belonging to this identity.
//
// One rule, applied to everything: an emoji is square and at most 256 by 256.
// Whatever is chosen gets cropped to a square first and then scaled down, so
// nobody can drop a twelve megapixel photograph into a line of text. Animated
// ones keep their movement, since that is half the point of them.
struct CustomEmoji
{
    CustomEmoji() : animated(false) {}
    QString name;        // written as :name: in a message
    QString filePath;
    bool animated;

    bool isValid() const { return !name.isEmpty() && !filePath.isEmpty(); }
};

class EmojiStore
{
public:
    EmojiStore(const MeeruPaths &paths, const QString &identityId);

    // A server keeps its own set, shared by everyone in it, so the same class
    // serves both by being told where to look.
    explicit EmojiStore(const QString &directory);

    QList<CustomEmoji> all() const;
    CustomEmoji byName(const QString &name) const;
    bool contains(const QString &name) const;

    // sourcePath is any picture or animation; crop is in source coordinates
    // and must be square. Still images are scaled to 256; animations are kept
    // whole with the crop applied as they play.
    bool add(const QString &name, const QString &sourcePath, const QRect &crop,
             bool animated, QString *error = 0);

    // Who put it there, kept beside the picture so a server can show it.
    QString authorOf(const QString &name) const;
    void setAuthor(const QString &name, const QString &author);
    bool remove(const QString &name, QString *error = 0);

    static int maximumSize();
    static QString sanitiseName(const QString &proposed);

private:
    QString directory() const;

    MeeruPaths paths_;
    QString identityId_;
    QString explicitDirectory_;
};

#endif
