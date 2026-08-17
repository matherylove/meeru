#ifndef MEERU_DM_WINDOW_H
#define MEERU_DM_WINDOW_H

#include <QString>
#include <QWidget>

#include "identity_store.h"
#include "meeru_paths.h"
#include "message_store.h"
#include "roster.h"

class QLabel;
class QLineEdit;
class QTextBrowser;
class QTimer;

class AvatarFrame;
class BannerFrame;
class MeeruTitleBar;
class PeerNode;

// One conversation with one person.
//
// It opens attached to the right edge of the main window and follows it about,
// which is how a messenger of this shape has always behaved. The pin in the
// title bar sets it loose, and pressing it again brings it back to the edge.
class DmWindow : public QWidget
{
    Q_OBJECT

public:
    DmWindow(const LocalProfile &profile,
             const Roster::Contact &contact,
             const MeeruPaths &paths,
             MessageStore *messages,
             PeerNode *node,
             QWidget *anchor,
             QWidget *parent = 0);

    QString peerId() const { return contact_.id; }
    QString conversationId() const { return conversationId_; }

    void setContact(const Roster::Contact &contact);
    void appendMessage(const Chat::Message &message);
    void setPeerOnline(bool online);
    void setPeerTyping(bool typing);
    void refreshDelivery();

    // Re-seats the window against the main window when that one is moved.
    void followAnchor();
    bool isPinned() const { return pinned_; }

signals:
    void closed(const QString &peerId);

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void onSend();
    void onPinToggled(bool pinned);
    void onComposeChanged(const QString &text);
    void onTypingTimeout();

private:
    void buildUi();
    void loadHistory();
    void renderHistory();
    QString formatMessage(const Chat::Message &message, bool withHeader) const;

    LocalProfile profile_;
    Roster::Contact contact_;
    MeeruPaths paths_;
    MessageStore *messages_;
    PeerNode *node_;
    QWidget *anchor_;
    QString conversationId_;
    bool pinned_;
    bool online_;
    QList<Chat::Message> shown_;

    MeeruTitleBar *titleBar_;
    BannerFrame *header_;
    AvatarFrame *avatar_;
    QLabel *nameLabel_;
    QLabel *presenceDot_;
    QLabel *stateLabel_;
    QLabel *statusLabel_;
    QTextBrowser *history_;
    QLabel *typingLabel_;
    QLineEdit *compose_;
    QLabel *footerLabel_;
    QTimer *typingTimer_;
};

#endif
