#ifndef MEERU_MEDIA_WINDOW_H
#define MEERU_MEDIA_WINDOW_H

#include <QString>
#include <QWidget>

#include "identity_store.h"
#include "meeru_paths.h"
#include "message_store.h"

class QLabel;
class QMovie;
class MeeruTitleBar;

// Opens one attachment on its own, with the things you would want to do with
// it: keep a copy elsewhere, make it your picture or your banner, or find out
// where it came from.
//
// It docks against the main window like a conversation does, and the same pin
// sets it loose.
class MediaWindow : public QWidget
{
    Q_OBJECT

public:
    MediaWindow(const LocalProfile &profile,
                const MeeruPaths &paths,
                const Chat::Message &message,
                QWidget *anchor,
                QWidget *parent = 0);
    ~MediaWindow();

    void followAnchor();

signals:
    void closed();
    void pictureChanged();

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void onSaveCopy();
    void onSetAvatar();
    void onSetBanner();
    void onOpenExternally();
    void onPinToggled(bool pinned);

private:
    void showContent();
    bool applyAsPicture(const QString &kind);

    LocalProfile profile_;
    MeeruPaths paths_;
    Chat::Message message_;
    QWidget *anchor_;
    bool pinned_;

    MeeruTitleBar *titleBar_;
    QLabel *stage_;
    QLabel *caption_;
    QMovie *movie_;
};

#endif
