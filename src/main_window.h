#ifndef MEERU_MAIN_WINDOW_H
#define MEERU_MAIN_WINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QString>

#include "app_settings.h"
#include "avatar.h"
#include "identity_store.h"
#include "meeru_paths.h"
#include "peer_node.h"
#include "roster.h"

class QAction;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTimer;
class QEvent;

class AvatarFrame;
class BannerFrame;
class ContactCard;
class ClickableLabel;
class MeeruTitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum Tab {
        MessagesTab = 0,
        ServersTab = 1,
        ContactsTab = 2
    };

    explicit MainWindow(const LocalProfile &profile, const MeeruPaths &paths, QWidget *parent = 0);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void onAvatarClicked();
    void onBannerClicked();
    void onPresenceClicked();
    void onPresenceAction(QAction *action);
    void onStatusClicked();
    void commitStatus();
    void onTabChanged(int tab);
    void onAddClicked();
    void onSearchChanged(const QString &text);
    void onItemActivated(QListWidgetItem *item);
    void onContextMenu(const QPoint &position);
    void onSettings();

    void onNetworkStatus(const QString &summary);
    void onPeerConnected(const QString &peerId);
    void onPeerDisconnected(const QString &peerId);
    void onTrustRequest(const QString &peerId, const QString &displayName, const QString &message);
    void onTrustAccepted(const QString &peerId);
    void onPeerProfile(const QString &peerId, const QString &displayName,
                       const QString &presence, const QString &statusText);
    void onPeerPicture(const QString &peerId, const QString &kind);
    void onHoverItem(QListWidgetItem *item);
    void onHoverTimeout();

private:
    void buildUi();
    QWidget *buildProfileHeader(QWidget *parent);
    QWidget *buildSearchRow(QWidget *parent);
    QWidget *buildTabRow(QWidget *parent);
    QWidget *buildFooter(QWidget *parent);

    void refreshProfile();
    void refreshAvatar();
    void refreshBanner();
    void refreshList();
    void refreshNews();
    void startNetwork();
    void publishProfile();
    void publishPictures();
    QPixmap contactTile(const Roster::Contact &contact) const;
    void hideContactCard();
    void applyPresence(int state, bool animate);

    void addMessagesEntry();
    void addServersEntry();
    void addContactsEntry();

    void openContact(const QString &contactId);
    void openConversation(const QString &conversationId);
    void openServer(const QString &serverId);
    void notYetAvailable(const QString &what);

    void appendHeader(const QString &title, const QString &trailing);
    void appendContact(const Roster::Contact &contact);
    void appendConversation(const Roster::Conversation &conversation);
    void appendServer(const Roster::Server &server);
    bool matchesFilter(const QString &title, const QString &subtitle) const;

    LocalProfile profile_;
    MeeruPaths paths_;
    SettingsStore settings_;
    RosterStore roster_;
    ImageStore avatarStore_;
    ImageStore bannerStore_;

    int presence_;
    QString statusText_;
    int currentTab_;
    QString filter_;
    bool committingStatus_;

    MeeruTitleBar *titleBar_;
    BannerFrame *banner_;
    AvatarFrame *avatar_;
    QLabel *nameLabel_;
    ClickableLabel *presenceDot_;
    ClickableLabel *stateLabel_;
    ClickableLabel *statusLabel_;
    QLineEdit *statusEdit_;
    QStackedWidget *statusStack_;
    QLineEdit *search_;
    QButtonGroup *tabs_;
    QPushButton *addButton_;
    QListWidget *list_;
    QLabel *emptyLabel_;
    QStackedWidget *listStack_;
    QLabel *newsLabel_;
    QLabel *footerLabel_;

    PeerNode *node_;
    QString networkStatus_;
    ContactCard *card_;
    QTimer *hoverTimer_;
    QString hoverContactId_;
    mutable QHash<QString, QPixmap> tileCache_;
};

#endif
