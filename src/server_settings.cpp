#include "server_settings.h"

#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "avatar.h"
#include "crop_dialog.h"
#include "emoji_store.h"
#include "meeru_window.h"

namespace {

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1("sectionLabel"));
    return label;
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    QTableWidget *table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setStyleSheet(QString::fromLatin1(
        "QTableWidget { background: #19121f; border: 1px solid #634A70; border-radius: 6px;"
        "               color: #E4D6EA; font-size: 11px; gridline-color: transparent; }"
        "QHeaderView::section { background: #241B2E; color: #9d8ba5; border: 0;"
        "                       padding: 5px; font-size: 10px; }"
        "QTableWidget::item { padding: 5px; }"
        "QTableWidget::item:selected { background: #4a3454; color: #ffffff; }"));
    return table;
}

QString whenText(const QDateTime &value)
{
    return value.isValid() ? value.toLocalTime().toString(QString::fromLatin1("d MMM yyyy"))
                           : QString::fromLatin1("unknown");
}

}

QString ServerSettings::pageName(int page)
{
    switch (page) {
    case PageProfile: return QString::fromLatin1("Server profile");
    case PageEmoji:   return QString::fromLatin1("Emoji");
    case PageSounds:  return QString::fromLatin1("Sound panel");
    case PageMembers: return QString::fromLatin1("Members");
    case PageRoles:   return QString::fromLatin1("Roles");
    case PageInvites: return QString::fromLatin1("Invites");
    case PageAudit:   return QString::fromLatin1("Audit log");
    default:          return QString();
    }
}

ServerSettings::ServerSettings(const LocalProfile &profile,
                               const MeeruPaths &paths,
                               ServerModel *model,
                               QWidget *parent)
    : QWidget(parent), profile_(profile), paths_(paths), model_(model),
      pages_(0), nameEdit_(0), descriptionEdit_(0), haloEdit_(0), haloSwatch_(0),
      emojiTable_(0), soundTable_(0), memberTable_(0), inviteTable_(0),
      roleList_(0), permissionBox_(0), auditTable_(0)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildProfile());
    pages_->addWidget(buildEmoji());
    pages_->addWidget(buildSounds());
    pages_->addWidget(buildMembers());
    pages_->addWidget(buildRoles());
    pages_->addWidget(buildInvites());
    pages_->addWidget(buildAudit());
    layout->addWidget(pages_);
}

bool ServerSettings::may(quint32 permission) const
{
    return model_ && model_->may(profile_.identityId, permission);
}

QString ServerSettings::serverEmojiDirectory() const
{
    return paths_.identityDirectory(profile_.identityId) + QLatin1String("/servers/")
         + model_->serverId() + QLatin1String("-emoji");
}

QString ServerSettings::serverSoundDirectory() const
{
    return paths_.identityDirectory(profile_.identityId) + QLatin1String("/servers/")
         + model_->serverId() + QLatin1String("-sounds");
}

void ServerSettings::showPage(int page)
{
    if (page < 0 || page >= pages_->count())
        return;
    pages_->setCurrentIndex(page);

    switch (page) {
    case PageEmoji:   reloadEmoji();   break;
    case PageSounds:  reloadSounds();  break;
    case PageMembers: reloadMembers(); break;
    case PageRoles:   reloadRoles();   break;
    case PageInvites: reloadInvites(); break;
    case PageAudit:   reloadAudit();   break;
    default: break;
    }
}

// ------------------------------------------------------------------- profile

QWidget *ServerSettings::buildProfile()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("NAME"), page));
    nameEdit_ = new QLineEdit(model_->name(), page);
    nameEdit_->setFixedHeight(30);
    layout->addWidget(nameEdit_);

    layout->addWidget(sectionLabel(QString::fromLatin1("DESCRIPTION"), page));
    descriptionEdit_ = new QLineEdit(model_->description(), page);
    descriptionEdit_->setFixedHeight(30);
    descriptionEdit_->setPlaceholderText(
        QString::fromLatin1("Shown under the name, the way a person has a status"));
    layout->addWidget(descriptionEdit_);

    layout->addWidget(sectionLabel(QString::fromLatin1("HALO COLOUR"), page));
    QHBoxLayout *haloRow = new QHBoxLayout();
    haloRow->setSpacing(8);

    haloEdit_ = new QLineEdit(model_->haloColour(), page);
    haloEdit_->setFixedHeight(30);
    haloEdit_->setPlaceholderText(QString::fromLatin1("#RRGGBB"));

    haloSwatch_ = new QLabel(page);
    haloSwatch_->setFixedSize(30, 30);
    haloSwatch_->setStyleSheet(QString::fromLatin1(
        "background: %1; border: 1px solid #634A70; border-radius: 6px;").arg(model_->haloColour()));

    QPushButton *pick = new QPushButton(QString::fromLatin1("Pick"), page);
    haloRow->addWidget(haloSwatch_);
    haloRow->addWidget(haloEdit_, 1);
    haloRow->addWidget(pick);
    layout->addLayout(haloRow);

    layout->addWidget(sectionLabel(QString::fromLatin1("PICTURES"), page));
    QHBoxLayout *pictureRow = new QHBoxLayout();
    QPushButton *icon = new QPushButton(QString::fromLatin1("Change the icon"), page);
    QPushButton *banner = new QPushButton(QString::fromLatin1("Change the banner"), page);
    pictureRow->addWidget(icon);
    pictureRow->addWidget(banner);
    pictureRow->addStretch();
    layout->addLayout(pictureRow);

    layout->addStretch();

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *save = new QPushButton(QString::fromLatin1("Save"), page);
    save->setObjectName(QString::fromLatin1("primaryButton"));
    buttons->addWidget(save);
    layout->addLayout(buttons);

    const bool allowed = may(Server::PermManageServer);
    nameEdit_->setEnabled(allowed);
    descriptionEdit_->setEnabled(allowed);
    haloEdit_->setEnabled(allowed);
    pick->setEnabled(allowed);
    icon->setEnabled(allowed);
    banner->setEnabled(allowed);
    save->setEnabled(allowed);

    connect(pick, SIGNAL(clicked()), this, SLOT(onPickHalo()));
    connect(save, SIGNAL(clicked()), this, SLOT(onSaveProfile()));
    connect(icon, SIGNAL(clicked()), this, SLOT(onChangeIcon()));
    connect(banner, SIGNAL(clicked()), this, SLOT(onChangeBanner()));
    return page;
}

void ServerSettings::onPickHalo()
{
    const QColor start(haloEdit_->text().trimmed());
    const QColor chosen = QColorDialog::getColor(start.isValid() ? start : QColor(0xDF, 0xB2, 0xF4),
                                                 this, QString::fromLatin1("Halo colour"));
    if (!chosen.isValid())
        return;
    haloEdit_->setText(chosen.name());
    haloSwatch_->setStyleSheet(QString::fromLatin1(
        "background: %1; border: 1px solid #634A70; border-radius: 6px;").arg(chosen.name()));
}

void ServerSettings::onSaveProfile()
{
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Server profile"),
                                 QString::fromLatin1("A server needs a name."));
        return;
    }

    const QColor colour(haloEdit_->text().trimmed());
    if (!haloEdit_->text().trimmed().isEmpty() && !colour.isValid()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Server profile"),
                                 QString::fromLatin1("That is not a colour. Use #RRGGBB."));
        return;
    }

    model_->setName(name);
    model_->setDescription(descriptionEdit_->text().trimmed());
    if (colour.isValid())
        model_->setHaloColour(colour.name());
    model_->note(profile_.displayName, QString::fromLatin1("Changed the server profile"));
    model_->save(0);
    emit changed();
}

void ServerSettings::onChangeIcon()
{
    ImageStore store(serverEmojiDirectory() + QLatin1String("/../") + model_->serverId(),
                     QString::fromLatin1("avatar"));
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromLatin1("Server icon"), QDir::homePath(),
        QString::fromLatin1("Pictures (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty())
        return;

    CropDialog crop(path, store.aspect(), QString::fromLatin1("Crop the icon"), this);
    if (!crop.isReady() || crop.exec() != QDialog::Accepted)
        return;

    QString error;
    const bool ok = crop.isAnimated() ? store.saveAnimated(path, crop.cropRect(), &error)
                                      : store.saveStill(crop.croppedStill(), &error);
    if (!ok) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Server icon"), error);
        return;
    }
    model_->note(profile_.displayName, QString::fromLatin1("Changed the server icon"));
    model_->save(0);
    emit changed();
}

void ServerSettings::onChangeBanner()
{
    ImageStore store(serverEmojiDirectory() + QLatin1String("/../") + model_->serverId(),
                     QString::fromLatin1("banner"));
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromLatin1("Server banner"), QDir::homePath(),
        QString::fromLatin1("Pictures (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty())
        return;

    CropDialog crop(path, store.aspect(), QString::fromLatin1("Crop the banner"), this);
    if (!crop.isReady() || crop.exec() != QDialog::Accepted)
        return;

    QString error;
    const bool ok = crop.isAnimated() ? store.saveAnimated(path, crop.cropRect(), &error)
                                      : store.saveStill(crop.croppedStill(), &error);
    if (!ok) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Server banner"), error);
        return;
    }
    model_->note(profile_.displayName, QString::fromLatin1("Changed the server banner"));
    model_->save(0);
    emit changed();
}

// --------------------------------------------------------------------- emoji

QWidget *ServerSettings::buildEmoji()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("CUSTOM EMOJI"), page));

    QStringList headers;
    headers << QString::fromLatin1("") << QString::fromLatin1("Written as")
            << QString::fromLatin1("Added by");
    emojiTable_ = makeTable(headers, page);
    emojiTable_->setIconSize(QSize(24, 24));
    emojiTable_->setColumnWidth(0, 40);
    layout->addWidget(emojiTable_, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *add = new QPushButton(QString::fromLatin1("Add emoji"), page);
    add->setObjectName(QString::fromLatin1("primaryButton"));
    QPushButton *remove = new QPushButton(QString::fromLatin1("Remove"), page);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    add->setEnabled(may(Server::PermManageEmoji));
    remove->setEnabled(may(Server::PermManageEmoji));

    connect(add, SIGNAL(clicked()), this, SLOT(onAddEmoji()));
    connect(remove, SIGNAL(clicked()), this, SLOT(onRemoveEmoji()));
    return page;
}

void ServerSettings::reloadEmoji()
{
    EmojiStore store(serverEmojiDirectory());
    const QList<CustomEmoji> emoji = store.all();

    emojiTable_->setRowCount(emoji.size());
    for (int i = 0; i < emoji.size(); ++i) {
        QTableWidgetItem *picture = new QTableWidgetItem();
        const QPixmap thumb(emoji.at(i).filePath);
        if (!thumb.isNull())
            picture->setIcon(QIcon(thumb.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        emojiTable_->setItem(i, 0, picture);
        emojiTable_->setItem(i, 1, new QTableWidgetItem(QLatin1Char(':') + emoji.at(i).name
                                                       + QLatin1Char(':')));
        emojiTable_->setItem(i, 2, new QTableWidgetItem(store.authorOf(emoji.at(i).name)));
        emojiTable_->setRowHeight(i, 30);
    }
}

void ServerSettings::onAddEmoji()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromLatin1("Choose a picture or animation"), QDir::homePath(),
        QString::fromLatin1("Pictures and animations (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty())
        return;

    CropDialog crop(path, 1.0, QString::fromLatin1("Crop the emoji"), this);
    if (!crop.isReady() || crop.exec() != QDialog::Accepted)
        return;

    QString name = QFileInfo(path).completeBaseName();
    if (!MeeruDialog::promptText(this, QString::fromLatin1("Name this emoji"),
                                 QString::fromLatin1("Letters, numbers and underscores only"), &name))
        return;

    EmojiStore store(serverEmojiDirectory());
    QString error;
    if (!store.add(name, path, crop.cropRect(), crop.isAnimated(), &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Emoji"), error);
        return;
    }
    store.setAuthor(EmojiStore::sanitiseName(name), profile_.displayName);
    model_->note(profile_.displayName, QString::fromLatin1("Added the emoji ") + name);
    model_->save(0);
    reloadEmoji();
    emit changed();
}

void ServerSettings::onRemoveEmoji()
{
    const int row = emojiTable_->currentRow();
    if (row < 0 || !emojiTable_->item(row, 1))
        return;

    QString name = emojiTable_->item(row, 1)->text();
    name.remove(QLatin1Char(':'));

    EmojiStore store(serverEmojiDirectory());
    QString error;
    if (!store.remove(name, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Emoji"), error);
        return;
    }
    model_->note(profile_.displayName, QString::fromLatin1("Removed the emoji ") + name);
    model_->save(0);
    reloadEmoji();
    emit changed();
}

// -------------------------------------------------------------------- sounds

QWidget *ServerSettings::buildSounds()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("SOUND PANEL"), page));
    QLabel *note = new QLabel(QString::fromLatin1(
        "Clips anyone in a voice channel can play. Kept short and uncompressed so they start "
        "instantly on any machine."), page);
    note->setObjectName(QString::fromLatin1("dialogLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    QStringList headers;
    headers << QString::fromLatin1("Name") << QString::fromLatin1("Added by")
            << QString::fromLatin1("File");
    soundTable_ = makeTable(headers, page);
    layout->addWidget(soundTable_, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *add = new QPushButton(QString::fromLatin1("Add sound"), page);
    add->setObjectName(QString::fromLatin1("primaryButton"));
    QPushButton *remove = new QPushButton(QString::fromLatin1("Remove"), page);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    add->setEnabled(may(Server::PermManageSounds));
    remove->setEnabled(may(Server::PermManageSounds));

    connect(add, SIGNAL(clicked()), this, SLOT(onAddSound()));
    connect(remove, SIGNAL(clicked()), this, SLOT(onRemoveSound()));
    return page;
}

void ServerSettings::reloadSounds()
{
    const QList<Server::Sound> sounds = model_->sounds();
    soundTable_->setRowCount(sounds.size());
    for (int i = 0; i < sounds.size(); ++i) {
        soundTable_->setItem(i, 0, new QTableWidgetItem(sounds.at(i).name));
        soundTable_->setItem(i, 1, new QTableWidgetItem(sounds.at(i).addedBy));
        soundTable_->setItem(i, 2, new QTableWidgetItem(QFileInfo(sounds.at(i).filePath).fileName()));
        soundTable_->item(i, 0)->setData(Qt::UserRole, sounds.at(i).id);
    }
}

void ServerSettings::onAddSound()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromLatin1("Choose a sound"), QDir::homePath(),
        QString::fromLatin1("Sounds (*.wav *.mp3 *.ogg)"));
    if (path.isEmpty())
        return;

    QString name = QFileInfo(path).completeBaseName();
    if (!MeeruDialog::promptText(this, QString::fromLatin1("Name this sound"),
                                 QString::fromLatin1("Shown in the panel"), &name))
        return;

    // Copied in, so the clip keeps working when the original is moved away.
    QDir().mkpath(serverSoundDirectory());
    const QString target = serverSoundDirectory() + QLatin1Char('/')
                         + QFileInfo(path).fileName();
    QFile::remove(target);
    if (!QFile::copy(path, target)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Sound"),
                                 QString::fromLatin1("That file could not be copied in."));
        return;
    }

    Server::Sound sound;
    sound.name = name;
    sound.filePath = target;
    sound.addedBy = profile_.displayName;
    model_->addSound(sound);
    model_->note(profile_.displayName, QString::fromLatin1("Added the sound ") + name);
    model_->save(0);
    reloadSounds();
    emit changed();
}

void ServerSettings::onRemoveSound()
{
    const int row = soundTable_->currentRow();
    if (row < 0 || !soundTable_->item(row, 0))
        return;
    model_->removeSound(soundTable_->item(row, 0)->data(Qt::UserRole).toString());
    model_->note(profile_.displayName, QString::fromLatin1("Removed a sound"));
    model_->save(0);
    reloadSounds();
    emit changed();
}

// ------------------------------------------------------------------- members

QWidget *ServerSettings::buildMembers()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("MEMBERS"), page));

    QStringList headers;
    headers << QString::fromLatin1("Name") << QString::fromLatin1("Role")
            << QString::fromLatin1("Joined here") << QString::fromLatin1("On Meeru since")
            << QString::fromLatin1("Came in with");
    memberTable_ = makeTable(headers, page);
    memberTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(memberTable_, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(onMemberMenu(QPoint)));
    layout->addWidget(memberTable_, 1);

    QLabel *note = new QLabel(QString::fromLatin1(
        "Right click anybody, here or in the member list, to message them, give them a nickname, "
        "change their roles, or remove them. What you are offered depends on what your own role "
        "allows."), page);
    note->setObjectName(QString::fromLatin1("dialogLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);
    return page;
}

void ServerSettings::reloadMembers()
{
    const QList<Server::Member> members = model_->membersByRank();
    memberTable_->setRowCount(members.size());

    for (int i = 0; i < members.size(); ++i) {
        const Server::Member &member = members.at(i);
        const QString shown = member.nickname.isEmpty() ? member.displayName
                                                        : member.nickname + QString::fromLatin1(" (")
                                                          + member.displayName + QLatin1Char(')');

        QTableWidgetItem *name = new QTableWidgetItem(shown);
        name->setData(Qt::UserRole, member.identityId);
        if (member.suspended)
            name->setForeground(QColor(0x9d, 0x8b, 0xa5));

        // Every role the person holds, not only the highest, since that is what
        // actually decides what they can do.
        QStringList roleNames;
        QStringList held = member.roleIds;
        if (held.isEmpty() && !member.roleId.isEmpty())
            held.append(member.roleId);
        for (int j = 0; j < held.size(); ++j) {
            const Server::Role role = model_->role(held.at(j));
            if (!role.name.isEmpty())
                roleNames.append(role.name);
        }

        memberTable_->setItem(i, 0, name);
        memberTable_->setItem(i, 1, new QTableWidgetItem(
            roleNames.isEmpty() ? QString::fromLatin1("none") : roleNames.join(QString::fromLatin1(", "))));
        memberTable_->setItem(i, 2, new QTableWidgetItem(whenText(member.joinedAtUtc)));
        memberTable_->setItem(i, 3, new QTableWidgetItem(whenText(member.accountCreatedUtc)));
        memberTable_->setItem(i, 4, new QTableWidgetItem(
            member.joinedWithInvite.isEmpty() ? QString::fromLatin1("founder")
                                              : member.joinedWithInvite));
    }
}

void ServerSettings::onMemberMenu(const QPoint &where)
{
    const int row = memberTable_->rowAt(where.y());
    if (row < 0 || !memberTable_->item(row, 0))
        return;

    const QString identityId = memberTable_->item(row, 0)->data(Qt::UserRole).toString();
    if (identityId.isEmpty())
        return;

    const bool self = identityId == profile_.identityId;
    const int myRank = model_->highestRank(profile_.identityId);
    const int theirRank = model_->highestRank(identityId);
    const bool admin = may(Server::PermAdministrator);

    // Nobody may act on somebody who outranks them, administrator or not.
    const bool outranked = admin || theirRank < myRank;

    QMenu menu(memberTable_);
    QAction *message = menu.addAction(QString::fromLatin1("Send a message"));
    QAction *befriend = menu.addAction(QString::fromLatin1("Send a friend request"));
    message->setEnabled(!self);
    befriend->setEnabled(!self);

    menu.addSeparator();
    QAction *nickname = menu.addAction(QString::fromLatin1("Set a nickname here"));
    nickname->setEnabled(self ? may(Server::PermChangeOwnNickname)
                              : (may(Server::PermManageNicknames) && outranked));

    // Roles, each one a switch, and only those you outrank.
    QMenu *roleMenu = menu.addMenu(QString::fromLatin1("Roles"));
    QList<QAction *> roleActions;
    QStringList roleIds;
    const QList<Server::Role> roles = model_->roles();
    for (int i = 0; i < roles.size(); ++i) {
        QAction *action = roleMenu->addAction(roles.at(i).name);
        action->setCheckable(true);
        action->setChecked(model_->memberHasRole(identityId, roles.at(i).id));
        action->setEnabled(may(Server::PermManageRoles)
                           && (admin || roles.at(i).rank < myRank)
                           && outranked);
        roleActions.append(action);
        roleIds.append(roles.at(i).id);
    }
    roleMenu->setEnabled(!roles.isEmpty());

    menu.addSeparator();
    QAction *transfer = menu.addAction(QString::fromLatin1("Hand over the server"));
    transfer->setEnabled(!self && model_->ownerIdentityId() == profile_.identityId);

    menu.addSeparator();
    QAction *suspend = menu.addAction(QString::fromLatin1("Suspend"));
    QAction *kick = menu.addAction(QString::fromLatin1("Remove from the server"));
    QAction *ban = menu.addAction(QString::fromLatin1("Ban"));
    suspend->setEnabled(!self && outranked && may(Server::PermSuspendMembers));
    kick->setEnabled(!self && outranked && may(Server::PermKickMembers));
    ban->setEnabled(!self && outranked && may(Server::PermBanMembers));

    QAction *chosen = menu.exec(memberTable_->viewport()->mapToGlobal(where));
    if (!chosen)
        return;

    if (chosen == message || chosen == befriend) {
        emit memberActionRequested(identityId, chosen == message ? QString::fromLatin1("message")
                                                                 : QString::fromLatin1("befriend"));
        return;
    }

    if (chosen == nickname) {
        QString name;
        for (int i = 0; i < model_->members().size(); ++i) {
            if (model_->members().at(i).identityId == identityId) {
                name = model_->members().at(i).nickname;
                break;
            }
        }
        if (!MeeruDialog::promptText(this, QString::fromLatin1("Nickname"),
                                     QString::fromLatin1("Shown only inside this server"), &name))
            return;
        model_->setMemberNickname(identityId, name);
        model_->note(profile_.displayName, QString::fromLatin1("Changed a nickname"));
    } else if (chosen == transfer) {
        const QString who = memberTable_->item(row, 0)->text();
        if (!MeeruDialog::confirm(this, QString::fromLatin1("Hand over the server"),
                QString::fromLatin1("Give %1 the highest role? You keep your place here, but from "
                                    "then on they are the one who cannot be overruled, including "
                                    "by you. This cannot be undone by yourself.").arg(who),
                QString::fromLatin1("Hand it over"))) {
            return;
        }
        if (!model_->transferOwnership(profile_.identityId, identityId)) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Hand over the server"),
                                     QString::fromLatin1("That did not work. Only the current owner "
                                                         "can hand the server over."));
            return;
        }
        model_->note(profile_.displayName, QString::fromLatin1("Handed the server to ") + who);
    } else if (chosen == suspend) {
        model_->setMemberSuspended(identityId, true);
        model_->note(profile_.displayName, QString::fromLatin1("Suspended a member"));
    } else if (chosen == kick || chosen == ban) {
        model_->removeMember(identityId);
        model_->note(profile_.displayName, chosen == ban ? QString::fromLatin1("Banned a member")
                                                         : QString::fromLatin1("Removed a member"));
    } else {
        const int index = roleActions.indexOf(chosen);
        if (index < 0)
            return;
        if (chosen->isChecked())
            model_->addRoleToMember(identityId, roleIds.at(index));
        else
            model_->removeRoleFromMember(identityId, roleIds.at(index));
        model_->note(profile_.displayName,
                     QString::fromLatin1(chosen->isChecked() ? "Gave a role to a member"
                                                             : "Took a role from a member"));
    }

    model_->save(0);
    reloadMembers();
    reloadRoles();
    emit changed();
}

// --------------------------------------------------------------------- roles

QWidget *ServerSettings::buildRoles()
{
    QWidget *page = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    QWidget *left = new QWidget(page);
    left->setFixedWidth(180);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    leftLayout->addWidget(sectionLabel(QString::fromLatin1("ROLES"), left));
    roleList_ = new QListWidget(left);
    roleList_->setStyleSheet(QString::fromLatin1(
        "QListWidget { background: #19121f; border: 1px solid #634A70; border-radius: 6px;"
        "              color: #E4D6EA; font-size: 12px; }"
        "QListWidget::item { padding: 6px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #4a3454; color: #ffffff; }"));
    leftLayout->addWidget(roleList_, 1);

    QHBoxLayout *roleButtons = new QHBoxLayout();
    QPushButton *add = new QPushButton(QString::fromLatin1("New"), left);
    QPushButton *remove = new QPushButton(QString::fromLatin1("Delete"), left);
    roleButtons->addWidget(add);
    roleButtons->addWidget(remove);
    leftLayout->addLayout(roleButtons);
    layout->addWidget(left);

    QWidget *right = new QWidget(page);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(sectionLabel(QString::fromLatin1("WHAT THIS ROLE MAY DO"), right));

    // Thirty two switches is a lot to look at, so they scroll rather than
    // squeezing the window.
    QScrollArea *scroll = new QScrollArea(right);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QString::fromLatin1("QScrollArea { background: transparent; }"));

    permissionBox_ = new QWidget(scroll);
    QGridLayout *grid = new QGridLayout(permissionBox_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);

    permissionFlags_ = Server::allPermissions();
    for (int i = 0; i < permissionFlags_.size(); ++i) {
        QCheckBox *check = new QCheckBox(Server::permissionName(permissionFlags_.at(i)),
                                         permissionBox_);
        check->setEnabled(false);
        grid->addWidget(check, i / 2, i % 2);
        permissionChecks_.append(check);
        connect(check, SIGNAL(clicked()), this, SLOT(onPermissionToggled()));
    }

    scroll->setWidget(permissionBox_);
    rightLayout->addWidget(scroll, 1);
    layout->addWidget(right, 1);

    add->setEnabled(may(Server::PermManageRoles));
    remove->setEnabled(may(Server::PermManageRoles));

    connect(roleList_, SIGNAL(itemSelectionChanged()), this, SLOT(onRoleChosen()));
    connect(add, SIGNAL(clicked()), this, SLOT(onAddRole()));
    connect(remove, SIGNAL(clicked()), this, SLOT(onRemoveRole()));
    return page;
}

void ServerSettings::reloadRoles()
{
    roleList_->clear();
    const QList<Server::Role> roles = model_->roles();

    for (int i = 0; i < roles.size(); ++i) {
        int members = 0;
        const QList<Server::Member> all = model_->members();
        for (int j = 0; j < all.size(); ++j) {
            QStringList held = all.at(j).roleIds;
            if (held.isEmpty() && !all.at(j).roleId.isEmpty())
                held.append(all.at(j).roleId);
            if (held.contains(roles.at(i).id))
                ++members;
        }

        QListWidgetItem *item = new QListWidgetItem(
            QString::fromLatin1("%1  (%2)").arg(roles.at(i).name).arg(members), roleList_);
        item->setData(Qt::UserRole, roles.at(i).id);
        if (!roles.at(i).colour.isEmpty())
            item->setForeground(QColor(roles.at(i).colour));
        if (roles.at(i).id == currentRoleId_)
            roleList_->setCurrentItem(item);
    }

    if (!roleList_->currentItem() && roleList_->count() > 0)
        roleList_->setCurrentRow(0);
}

void ServerSettings::onRoleChosen()
{
    QListWidgetItem *item = roleList_->currentItem();
    if (!item)
        return;

    currentRoleId_ = item->data(Qt::UserRole).toString();
    const Server::Role role = model_->role(currentRoleId_);

    // A role at or above your own is shown but cannot be edited: that is the
    // rule that stops somebody quietly promoting themselves.
    const int myRank = model_->highestRank(profile_.identityId);
    const bool editable = may(Server::PermManageRoles)
                       && (may(Server::PermAdministrator) || role.rank < myRank);

    for (int i = 0; i < permissionChecks_.size(); ++i) {
        permissionChecks_.at(i)->blockSignals(true);
        permissionChecks_.at(i)->setChecked((role.permissions & permissionFlags_.at(i)) != 0);
        permissionChecks_.at(i)->setEnabled(editable);
        permissionChecks_.at(i)->blockSignals(false);
    }
}

void ServerSettings::onPermissionToggled()
{
    if (currentRoleId_.isEmpty())
        return;

    quint32 permissions = 0;
    for (int i = 0; i < permissionChecks_.size(); ++i) {
        if (permissionChecks_.at(i)->isChecked())
            permissions |= permissionFlags_.at(i);
    }

    QList<Server::Role> roles = model_->roles();
    for (int i = 0; i < roles.size(); ++i) {
        if (roles.at(i).id != currentRoleId_)
            continue;
        Server::Role updated = roles.at(i);
        updated.permissions = permissions;
        model_->removeRole(currentRoleId_);
        model_->addRole(updated);
        break;
    }

    model_->note(profile_.displayName, QString::fromLatin1("Changed what a role may do"));
    model_->save(0);
    emit changed();
}

void ServerSettings::onAddRole()
{
    QString name = QString::fromLatin1("New role");
    if (!MeeruDialog::promptText(this, QString::fromLatin1("New role"),
                                 QString::fromLatin1("What is it called?"), &name))
        return;

    Server::Role role;
    role.name = name;
    role.colour = QString::fromLatin1("#C9B9CF");
    // Below your own, always: a new role cannot outrank the person making it.
    role.rank = qMax(1, model_->highestRank(profile_.identityId) - 1);
    role.permissions = Server::defaultPermissions();
    model_->addRole(role);
    model_->note(profile_.displayName, QString::fromLatin1("Created the role ") + name);
    model_->save(0);
    reloadRoles();
    emit changed();
}

void ServerSettings::onRemoveRole()
{
    QListWidgetItem *item = roleList_->currentItem();
    if (!item)
        return;

    const QString roleId = item->data(Qt::UserRole).toString();
    const Server::Role role = model_->role(roleId);
    const int myRank = model_->highestRank(profile_.identityId);

    if (!may(Server::PermAdministrator) && role.rank >= myRank) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Roles"),
                                 QString::fromLatin1("You cannot delete a role at or above your own."));
        return;
    }
    if (!MeeruDialog::confirm(this, QString::fromLatin1("Delete role"),
                              QString::fromLatin1("Delete %1? Anybody holding it keeps their place "
                                                  "in the server but loses what it granted.")
                                  .arg(role.name),
                              QString::fromLatin1("Delete"))) {
        return;
    }

    model_->removeRole(roleId);
    model_->note(profile_.displayName, QString::fromLatin1("Deleted the role ") + role.name);
    model_->save(0);
    currentRoleId_.clear();
    reloadRoles();
    emit changed();
}

// ------------------------------------------------------------------- invites

QWidget *ServerSettings::buildInvites()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("INVITES"), page));

    QStringList headers;
    headers << QString::fromLatin1("Code") << QString::fromLatin1("Made by")
            << QString::fromLatin1("Grants") << QString::fromLatin1("Used")
            << QString::fromLatin1("Expires") << QString::fromLatin1("Who came in");
    inviteTable_ = makeTable(headers, page);
    layout->addWidget(inviteTable_, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *create = new QPushButton(QString::fromLatin1("Create invite"), page);
    create->setObjectName(QString::fromLatin1("primaryButton"));
    QPushButton *revoke = new QPushButton(QString::fromLatin1("Revoke"), page);
    buttons->addWidget(create);
    buttons->addWidget(revoke);
    buttons->addStretch();
    layout->addLayout(buttons);

    create->setEnabled(may(Server::PermCreateInvites));
    revoke->setEnabled(may(Server::PermManageServer) || may(Server::PermAdministrator));

    connect(create, SIGNAL(clicked()), this, SLOT(onCreateInvite()));
    connect(revoke, SIGNAL(clicked()), this, SLOT(onRevokeInvite()));
    return page;
}

void ServerSettings::reloadInvites()
{
    if (!may(Server::PermViewInvites) && !may(Server::PermAdministrator)
        && !may(Server::PermCreateInvites)) {
        inviteTable_->setRowCount(0);
        return;
    }

    const QList<Server::Invite> invites = model_->invites();
    inviteTable_->setRowCount(invites.size());

    for (int i = 0; i < invites.size(); ++i) {
        const Server::Invite &invite = invites.at(i);
        QTableWidgetItem *code = new QTableWidgetItem(invite.code);
        code->setData(Qt::UserRole, invite.code);
        if (invite.isExpired())
            code->setForeground(QColor(0x9d, 0x8b, 0xa5));

        inviteTable_->setItem(i, 0, code);
        inviteTable_->setItem(i, 1, new QTableWidgetItem(invite.createdBy));
        inviteTable_->setItem(i, 2, new QTableWidgetItem(
            model_->role(invite.grantsRoleId).name.isEmpty()
                ? QString::fromLatin1("no role")
                : model_->role(invite.grantsRoleId).name));
        inviteTable_->setItem(i, 3, new QTableWidgetItem(
            invite.maxUses > 0 ? QString::fromLatin1("%1 of %2").arg(invite.uses).arg(invite.maxUses)
                               : QString::number(invite.uses)));
        inviteTable_->setItem(i, 4, new QTableWidgetItem(
            invite.isExpired() ? QString::fromLatin1("expired")
                               : (invite.expiresAtUtc.isValid() ? whenText(invite.expiresAtUtc)
                                                                : QString::fromLatin1("never"))));
        inviteTable_->setItem(i, 5, new QTableWidgetItem(
            invite.joinedIdentities.isEmpty() ? QString::fromLatin1("nobody yet")
                                              : invite.joinedIdentities.join(QString::fromLatin1(", "))));
    }
}

void ServerSettings::onCreateInvite()
{
    MeeruDialog dialog(QString::fromLatin1("New invite"), this);
    dialog.setDialogWidth(340);

    QLabel *roleLabel = new QLabel(QString::fromLatin1("Role it grants"));
    roleLabel->setObjectName(QString::fromLatin1("dialogLabel"));
    dialog.contentLayout()->addWidget(roleLabel);

    QComboBox *roleBox = new QComboBox();
    roleBox->setFixedHeight(30);
    roleBox->addItem(QString::fromLatin1("No role"), QString());
    const QList<Server::Role> roles = model_->roles();
    const int myRank = model_->highestRank(profile_.identityId);
    for (int i = 0; i < roles.size(); ++i) {
        // You cannot hand out a role you do not outrank.
        if (may(Server::PermAdministrator) || roles.at(i).rank < myRank)
            roleBox->addItem(roles.at(i).name, roles.at(i).id);
    }
    dialog.contentLayout()->addWidget(roleBox);

    QLabel *lifeLabel = new QLabel(QString::fromLatin1("Expires"));
    lifeLabel->setObjectName(QString::fromLatin1("dialogLabel"));
    dialog.contentLayout()->addWidget(lifeLabel);

    QComboBox *lifeBox = new QComboBox();
    lifeBox->setFixedHeight(30);
    lifeBox->addItem(QString::fromLatin1("In 1 hour"), 3600);
    lifeBox->addItem(QString::fromLatin1("In 1 day"), 86400);
    lifeBox->addItem(QString::fromLatin1("In 7 days"), 7 * 86400);
    lifeBox->addItem(QString::fromLatin1("In 30 days"), 30 * 86400);
    lifeBox->addItem(QString::fromLatin1("Never"), 0);
    lifeBox->setCurrentIndex(2);
    dialog.contentLayout()->addWidget(lifeBox);

    QLabel *useLabel = new QLabel(QString::fromLatin1("How many people may use it"));
    useLabel->setObjectName(QString::fromLatin1("dialogLabel"));
    dialog.contentLayout()->addWidget(useLabel);

    QComboBox *useBox = new QComboBox();
    useBox->setFixedHeight(30);
    useBox->addItem(QString::fromLatin1("As many as like"), 0);
    useBox->addItem(QString::fromLatin1("1"), 1);
    useBox->addItem(QString::fromLatin1("5"), 5);
    useBox->addItem(QString::fromLatin1("25"), 25);
    dialog.contentLayout()->addWidget(useBox);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"));
    QPushButton *accept = new QPushButton(QString::fromLatin1("Create"));
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);

    connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));

    if (dialog.exec() != QDialog::Accepted)
        return;

    Server::Invite invite;
    invite.createdBy = profile_.displayName;
    invite.grantsRoleId = roleBox->itemData(roleBox->currentIndex()).toString();
    invite.maxUses = useBox->itemData(useBox->currentIndex()).toInt();
    const int seconds = lifeBox->itemData(lifeBox->currentIndex()).toInt();
    if (seconds > 0)
        invite.expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(seconds);

    model_->addInvite(invite);
    model_->note(profile_.displayName, QString::fromLatin1("Created an invite"));
    model_->save(0);
    reloadInvites();
    emit changed();
}

void ServerSettings::onRevokeInvite()
{
    const int row = inviteTable_->currentRow();
    if (row < 0 || !inviteTable_->item(row, 0))
        return;

    model_->removeInvite(inviteTable_->item(row, 0)->data(Qt::UserRole).toString());
    model_->note(profile_.displayName, QString::fromLatin1("Revoked an invite"));
    model_->save(0);
    reloadInvites();
    emit changed();
}

// --------------------------------------------------------------------- audit

QWidget *ServerSettings::buildAudit()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QString::fromLatin1("AUDIT LOG"), page));

    QStringList headers;
    headers << QString::fromLatin1("When") << QString::fromLatin1("Who")
            << QString::fromLatin1("What");
    auditTable_ = makeTable(headers, page);
    layout->addWidget(auditTable_, 1);
    return page;
}

void ServerSettings::reloadAudit()
{
    if (!may(Server::PermViewAudit) && !may(Server::PermAdministrator)) {
        auditTable_->setRowCount(0);
        return;
    }

    const QList<Server::AuditEntry> audit = model_->audit();
    auditTable_->setRowCount(audit.size());

    // Newest first: what just happened is what anybody opening this wants.
    for (int i = 0; i < audit.size(); ++i) {
        const Server::AuditEntry &entry = audit.at(audit.size() - 1 - i);
        auditTable_->setItem(i, 0, new QTableWidgetItem(
            entry.atUtc.toLocalTime().toString(QString::fromLatin1("d MMM h:mm AP"))));
        auditTable_->setItem(i, 1, new QTableWidgetItem(entry.actorName));
        auditTable_->setItem(i, 2, new QTableWidgetItem(entry.description));
    }
}
