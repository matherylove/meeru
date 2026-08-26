#ifndef MEERU_CALL_WINDOW_H
#define MEERU_CALL_WINDOW_H

#include <QHash>
#include <QImage>
#include <QString>
#include <QWidget>

#include "call_engine.h"
#include "identity_store.h"

class QLabel;
class QPushButton;
class MeeruTitleBar;

// The window a call lives in, docked beside the main one like everything else
// and set loose with the same pin.
class CallWindow : public QWidget
{
    Q_OBJECT

public:
    CallWindow(const LocalProfile &profile,
               const QString &title,
               CallEngine *engine,
               QWidget *anchor,
               QWidget *parent = 0);

    void followAnchor();
    void setPeerNames(const QHash<QString, QString> &names);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void onStateChanged(int state);
    void onFrame(const QString &peerId, const QImage &frame, int source);
    void onMute();
    void onScreen();
    void onHangUp();
    void onPinToggled(bool pinned);

private:
    LocalProfile profile_;
    CallEngine *engine_;
    QWidget *anchor_;
    bool pinned_;
    QHash<QString, QString> names_;

    MeeruTitleBar *titleBar_;
    QLabel *stage_;
    QLabel *status_;
    QPushButton *muteButton_;
    QPushButton *screenButton_;
};

#endif
