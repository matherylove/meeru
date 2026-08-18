#ifndef MEERU_SERVER_WINDOW_H
#define MEERU_SERVER_WINDOW_H

#include <QHash>
#include <QString>
#include <QWidget>

#include "identity_store.h"
#include "meeru_paths.h"
#include "message_store.h"
#include "roster.h"
#include "server_model.h"

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTextBrowser;

class MediaWindow;
class MeeruTitleBar;
class PeerNode;

// A server, or a group, which is the same window with the sections it does not
// have hidden.
//
// The rail on the left is a set of ways to look at the same conversation
// rather than separate places: Members and Channels choose what to read,
// Media, Threads, Pinned, Mentions, Links and Files narrow it down, and
// Settings changes the server itself.
class ServerWindow : public QWidget
{
    Q_OBJECT

public:
    enum Section {
        SectionMembers = 0,
        SectionChannels,
        SectionMedia,
        SectionThreads,
        SectionPinned,
        SectionMentions,
        SectionLinks,
        SectionFiles,
        SectionSettings
    };

    ServerWindow(const LocalProfile &profile,
                 const Roster::Server &server,
                 const MeeruPaths &paths,
                 MessageStore *messages,
                 PeerNode *node,
                 QWidget *anchor,
                 QWidget *parent = 0);

    // Groups reuse this window with the sections that make no sense removed.
    ServerWindow(const LocalProfile &profile,
                 const Roster::Conversation &group,
                 const QList<Roster::Contact> &members,
                 const MeeruPaths &paths,
                 MessageStore *messages,
                 PeerNode *node,
                 QWidget *anchor,
                 QWidget *parent = 0);

    QString serverId() const { return serverId_; }
    void followAnchor();
    void appendMessage(const Chat::Message &message);

signals:
    void closed(const QString &serverId);
    void callRequested(const QString &conversationId, const QStringList &participants,
                       const QString &title, bool withVideo);

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void onSectionChanged(int section);
    void onSideChoice();
    void onSend();
    void onMediaFilter(int index);
    void onChannelFilter(int index);
    void onHistoryLink(const QUrl &url);
    void onPinToggled(bool pinned);
    void onAdultToggled(bool allowed);

private:
    void buildUi();
    void rebuildSide();
    void rebuildContent();

    void fillMembers();
    void fillChannels();
    void fillMediaKinds();
    void fillThreads();
    void fillPinned();
    void fillMentions();
    void fillLinkChannels();
    void fillFileKinds();
    void fillSettings();

    void showChat(const QString &channelId);
    void showGallery();
    void showFiltered(int mediaKind, bool linksOnly);
    void showAnchoredHistory(const QString &channelId, const QString &messageId);

    QList<Chat::Message> messagesOf(const QString &channelId) const;
    QString currentConversationId() const;
    bool channelVisible(const Server::Channel &channel) const;
    QString renderMessages(const QList<Chat::Message> &messages, const QString &highlightId) const;

    LocalProfile profile_;
    MeeruPaths paths_;
    MessageStore *messages_;
    PeerNode *node_;
    QWidget *anchor_;

    ServerModel model_;
    QString serverId_;
    QList<Roster::Contact> contacts_;
    bool group_;
    bool pinned_;
    bool adultAllowed_;
    int section_;
    QString channelId_;
    int mediaKind_;

    MeeruTitleBar *titleBar_;
    QButtonGroup *rail_;
    QListWidget *side_;
    QLabel *sideTitle_;
    QComboBox *scopeBox_;
    QTextBrowser *content_;
    QListWidget *gallery_;
    QStackedWidget *contentStack_;
    QLineEdit *compose_;
    QWidget *composeWrap_;
    QHash<QString, MediaWindow *> viewers_;
};

#endif
