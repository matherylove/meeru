#ifndef MEERU_PRESENCE_H
#define MEERU_PRESENCE_H

#include <QColor>
#include <QString>

// Shared presence vocabulary used by the login screen, the stored profile
// and the main window, so the four states are defined in exactly one place.
namespace Presence {

enum State {
    Available = 0,
    Absent = 1,
    DoNotDisturb = 2,
    Invisible = 3
};

inline QColor color(int state)
{
    switch (state) {
    case Absent:       return QColor(250, 166, 26);
    case DoNotDisturb: return QColor(240, 71, 71);
    case Invisible:    return QColor(116, 127, 141);
    default:           return QColor(67, 181, 129);
    }
}

inline QString key(int state)
{
    switch (state) {
    case Absent:       return QString::fromLatin1("absent");
    case DoNotDisturb: return QString::fromLatin1("dnd");
    case Invisible:    return QString::fromLatin1("invisible");
    default:           return QString::fromLatin1("available");
    }
}

inline int stateFromKey(const QString &value)
{
    if (value == QLatin1String("absent"))    return Absent;
    if (value == QLatin1String("dnd"))       return DoNotDisturb;
    if (value == QLatin1String("invisible")) return Invisible;
    return Available;
}

inline QString label(int state)
{
    switch (state) {
    case Absent:       return QString::fromLatin1("Absent");
    case DoNotDisturb: return QString::fromLatin1("Do Not Disturb");
    case Invisible:    return QString::fromLatin1("Invisible");
    default:           return QString::fromLatin1("Available");
    }
}

inline QColor colorForKey(const QString &value) { return color(stateFromKey(value)); }
inline QString labelForKey(const QString &value) { return label(stateFromKey(value)); }

}

#endif
