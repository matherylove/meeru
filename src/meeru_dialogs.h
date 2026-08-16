#ifndef MEERU_DIALOGS_H
#define MEERU_DIALOGS_H

#include <QColor>
#include <QString>
#include <QStringList>

#include "avatar.h"
#include "identity_crypto.h"
#include "identity_store.h"
#include "meeru_window.h"
#include "roster.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

// Profile picture or header banner: change, crop, or remove.
class PictureDialog : public MeeruDialog
{
    Q_OBJECT

public:
    PictureDialog(ImageStore *store, const QString &initials, const QColor &presence, QWidget *parent = 0);
    bool wasChanged() const { return changed_; }

private slots:
    void onChange();
    void onRemove();

private:
    void refresh();
    bool isBanner() const;

    ImageStore *store_;
    QString initials_;
    QColor presence_;
    bool changed_;
    AvatarFrame *avatarPreview_;
    BannerFrame *bannerPreview_;
    QLabel *caption_;
    QPushButton *removeButton_;
};

// Asks for a passphrase (twice when creating) to protect a portable backup.
class PassphraseDialog : public MeeruDialog
{
    Q_OBJECT

public:
    PassphraseDialog(bool confirming, const QString &message, QWidget *parent = 0);
    QString passphrase() const;

private slots:
    void validate();

private:
    bool confirming_;
    QLineEdit *first_;
    QLineEdit *second_;
    QLabel *feedback_;
    QPushButton *acceptButton_;
};

// The three-way choice shown before erasing an identity from this computer.
class LogOutDialog : public MeeruDialog
{
    Q_OBJECT

public:
    enum Choice {
        Cancelled = 0,
        LogOut = 1,
        Backup = 2
    };

    LogOutDialog(const QString &displayName, bool backedUp, QWidget *parent = 0);
    Choice choice() const { return choice_; }

private slots:
    void chooseLogOut();
    void chooseBackup();

private:
    Choice choice_;
};

// Adding a friend by their Meeru ID, which queues a trust request.
class AddContactDialog : public MeeruDialog
{
    Q_OBJECT

public:
    AddContactDialog(const LocalProfile &profile,
                     const IdentityMaterial &material,
                     const QStringList &localEndpoints,
                     qint64 inviteLifetime,
                     QWidget *parent = 0);

    QString contactId() const;
    QString contactName() const;
    QString endpointHint() const;

    // The lifetime the user picked, so it can be remembered for next time.
    qint64 inviteLifetime() const;

private slots:
    void onCopyOwnCode();
    void onLifetimeChanged(int index);
    void validate();

private:
    LocalProfile profile_;
    IdentityMaterial material_;
    QStringList localEndpoints_;
    QStringList pastedEndpoints_;
    QString pastedId_;

    QLineEdit *idEdit_;
    QLineEdit *nameEdit_;
    QLineEdit *addressEdit_;
    QComboBox *lifetimeBox_;
    QLabel *lifetimeWarning_;
    QLabel *feedback_;
    QPushButton *acceptButton_;
};

// Picking one contact (a direct message) or several (a group swarm).
class NewConversationDialog : public MeeruDialog
{
    Q_OBJECT

public:
    NewConversationDialog(const QList<Roster::Contact> &contacts, QWidget *parent = 0);

    QStringList selectedIds() const { return selected_; }
    QString groupTitle() const;

private slots:
    void onSelectionChanged();

private:
    QListWidget *list_;
    QLineEdit *titleEdit_;
    QLabel *summary_;
    QPushButton *acceptButton_;
    QStringList selected_;
};

// Creating a server or joining one from an invite code.
class ServerDialog : public MeeruDialog
{
    Q_OBJECT

public:
    explicit ServerDialog(QWidget *parent = 0);

    bool isJoining() const { return joining_; }
    Roster::Server server() const { return server_; }

private slots:
    void chooseCreate();
    void chooseJoin();
    void submitCreate();
    void submitJoin();

private:
    void showPage(int index);

    QStackedWidget *pages_;
    QLineEdit *nameEdit_;
    QLineEdit *topicEdit_;
    QLineEdit *inviteEdit_;
    QLabel *joinFeedback_;
    bool joining_;
    Roster::Server server_;
};

// Display name, startup behaviour and where the data lives.
class SettingsDialog : public MeeruDialog
{
    Q_OBJECT

public:
    SettingsDialog(const QString &displayName,
                   const QString &identityId,
                   const QString &folder,
                   bool startWithWindows,
                   const QStringList &rendezvousHosts,
                   const QString &reachability,
                   const QString &diagnostics,
                   bool useUpnp,
                   bool useDht,
                   int listenPort,
                   const QString &publicAddress,
                   QWidget *parent = 0);

    QString displayName() const;
    bool startWithWindows() const;
    QStringList rendezvousHosts() const;

    bool useUpnp() const;
    bool useDht() const;
    int listenPort() const;
    QString publicAddress() const;

private slots:
    void onCopyId();
    void onOpenFolder();
    void onShowDiagnostics();

private:
    QString identityId_;
    QString folder_;
    QString diagnostics_;
    QLineEdit *nameEdit_;
    QCheckBox *startupBox_;
    QLineEdit *rendezvousEdit_;
    QCheckBox *upnpBox_;
    QCheckBox *dhtBox_;
    QLineEdit *portEdit_;
    QLineEdit *publicEdit_;
};

#endif
