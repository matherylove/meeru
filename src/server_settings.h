#ifndef MEERU_SERVER_SETTINGS_H
#define MEERU_SERVER_SETTINGS_H

#include <QPoint>
#include <QString>
#include <QWidget>

#include "identity_store.h"
#include "meeru_paths.h"
#include "server_model.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTableWidget;

// The editable side of a server: everything under Settings, in one place.
//
// Each page checks the viewer's own permissions before letting anything be
// changed, so a member looking at the roles page sees it and a member without
// the right to touch it finds the controls disabled rather than absent. Hiding
// them would leave people wondering; showing them greyed says who may.
class ServerSettings : public QWidget
{
    Q_OBJECT

public:
    enum Page {
        PageProfile = 0,
        PageEmoji,
        PageSounds,
        PageMembers,
        PageRoles,
        PageInvites,
        PageAudit
    };

    ServerSettings(const LocalProfile &profile,
                   const MeeruPaths &paths,
                   ServerModel *model,
                   QWidget *parent = 0);

    void showPage(int page);
    static QString pageName(int page);

signals:
    void changed();

    // Things the settings panel cannot do itself, because they belong to the
    // rest of the program: opening a conversation, sending a friend request.
    void memberActionRequested(const QString &identityId, const QString &action);

private slots:
    void onPickHalo();
    void onSaveProfile();
    void onChangeIcon();
    void onChangeBanner();
    void onAddEmoji();
    void onRemoveEmoji();
    void onAddSound();
    void onRemoveSound();
    void onRoleChosen();
    void onPermissionToggled();
    void onAddRole();
    void onRemoveRole();
    void onCreateInvite();
    void onRevokeInvite();
    void onMemberMenu(const QPoint &where);

private:
    QWidget *buildProfile();
    QWidget *buildEmoji();
    QWidget *buildSounds();
    QWidget *buildMembers();
    QWidget *buildRoles();
    QWidget *buildInvites();
    QWidget *buildAudit();

    void reloadEmoji();
    void reloadSounds();
    void reloadMembers();
    void reloadRoles();
    void reloadInvites();
    void reloadAudit();

    QString serverEmojiDirectory() const;
    QString serverSoundDirectory() const;
    QString serverPictureDirectory() const;
    bool may(quint32 permission) const;

    LocalProfile profile_;
    MeeruPaths paths_;
    ServerModel *model_;

    QStackedWidget *pages_;
    QLineEdit *nameEdit_;
    QLineEdit *descriptionEdit_;
    QLineEdit *haloEdit_;
    QLabel *haloSwatch_;
    QTableWidget *emojiTable_;
    QTableWidget *soundTable_;
    QTableWidget *memberTable_;
    QTableWidget *inviteTable_;
    QListWidget *roleList_;
    QWidget *permissionBox_;
    QTableWidget *auditTable_;
    QList<class QCheckBox *> permissionChecks_;
    QList<quint32> permissionFlags_;
    QString currentRoleId_;
};

#endif
