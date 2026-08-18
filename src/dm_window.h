#ifndef MEERU_DM_WINDOW_H
#define MEERU_DM_WINDOW_H

#include <QString>
#include <QWidget>

#include "identity_store.h"
#include "meeru_paths.h"
#include "message_store.h"
#include "roster.h"
#include "voice_recorder.h"

class QLabel;
class QLineEdit;
class QTextBrowser;
class QTimer;
class QUrl;

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
    // One to one.
    DmWindow(const LocalProfile &profile,
             const Roster::Contact &contact,
             const MeeruPaths &paths,
             MessageStore *messages,
             PeerNode *node,
             QWidget *anchor,
             QWidget *parent = 0);

    // A group. Everything below behaves the same; what changes is that a
    // message goes to every member, and that catching up asks all of them and
    // keeps whatever the one furthest ahead has.
    DmWindow(const LocalProfile &profile,
             const Roster::Conversation &conversation,
             const QList<Roster::Contact> &members,
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
    void setMemberOnline(const QString &peerId, bool online);
    bool isGroup() const { return group_; }
    void setPeerTyping(bool typing);
    void refreshDelivery();

    // Re-seats the window against the main window when that one is moved.
    void followAnchor();
    bool isPinned() const { return pinned_; }

signals:
    void closed(const QString &peerId);
    void callRequested(const QString &conversationId, const QStringList &participants,
                       const QString &title, bool withVideo);

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void onSend();
    void onAttach();
    void onEmoji();
    void onPoll();
    void onHistoryLink(const QUrl &url);
    void onTransferChanged(const QString &conversationId, const QString &messageId);
    void onVoice();
    void onCall();
    void onVideoCall();
    void onVoiceTick(int seconds);
    void onPinToggled(bool pinned);
    void onComposeChanged(const QString &text);
    void onTypingTimeout();

private:
    void buildUi();
    void loadHistory();
    void renderHistory();
    QString formatMessage(const Chat::Message &message, bool withHeader) const;
    void sendAttachment(const QString &path);

    LocalProfile profile_;
    Roster::Contact contact_;
    QList<Roster::Contact> members_;
    bool group_;
    QString groupTitle_;
    VoiceRecorder *voice_;
    class QPushButton *voiceButton_;
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
    QLabel *sizeHint_;
    QTimer *typingTimer_;
};

#endif
