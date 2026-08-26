#include "server_window.h"

#include <QCloseEvent>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <QAction>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>

#include "app_settings.h"
#include "camera_source.h"
#include "transfer_manager.h"
#include "voice_recorder.h"
#include "avatar.h"
#include "media_window.h"
#include "meeru_dialogs.h"
#include "meeru_paint.h"
#include "meeru_style.h"
#include "meeru_window.h"
#include "peer_node.h"
#include "presence.h"

namespace {

const int kWindowWidth = 720;
const int kWindowHeight = 560;
const int kSideWidth = 210;
const int kDockGap = 6;

struct SectionInfo { int id; const char *label; };

const SectionInfo kSections[] = {
    { ServerWindow::SectionMembers,  "Members" },
    { ServerWindow::SectionChannels, "Channels" },
    { ServerWindow::SectionMedia,    "Media" },
    { ServerWindow::SectionThreads,  "Threads" },
    { ServerWindow::SectionPinned,   "Pinned" },
    { ServerWindow::SectionMentions, "@you" },
    { ServerWindow::SectionLinks,    "Links" },
    { ServerWindow::SectionFiles,    "Files" },
    { ServerWindow::SectionSettings, "Settings" }
};

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

bool hasLink(const QString &text)
{
    return text.contains(QLatin1String("http://")) || text.contains(QLatin1String("https://"))
        || text.contains(QLatin1String("www."));
}

}

ServerWindow::ServerWindow(const LocalProfile &profile,
                           const Roster::Server &server,
                           const MeeruPaths &paths,
                           MessageStore *messages,
                           PeerNode *node,
                           QWidget *anchor,
                           QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), paths_(paths), messages_(messages), node_(node), anchor_(anchor),
      model_(paths, profile.identityId, server.id), serverId_(server.id),
      group_(false), pinned_(true), adultAllowed_(false), section_(SectionMembers),
      mediaKind_(Chat::MediaImage),
      titleBar_(0), rail_(0), side_(0), sideTitle_(0), scopeBox_(0),
      content_(0), gallery_(0), contentStack_(0), compose_(0), composeWrap_(0),
      settings_(0), voice_(0), voiceButton_(0)
{
    if (!model_.load()) {
        model_ = ServerModel::createDefault(paths_, profile_.identityId, server.id,
                                            server.name, server.topic, profile_.displayName);
        model_.save(0);
    }

    adultAllowed_ = SettingsStore(paths_).load().adultAllowed;

    const QList<Server::Channel> text = model_.channelsOfKind(Server::ChannelText);
    if (!text.isEmpty())
        channelId_ = text.first().id;

    buildUi();
    onSectionChanged(SectionMembers);
    followAnchor();
}

ServerWindow::ServerWindow(const LocalProfile &profile,
                           const Roster::Conversation &group,
                           const QList<Roster::Contact> &members,
                           const MeeruPaths &paths,
                           MessageStore *messages,
                           PeerNode *node,
                           QWidget *anchor,
                           QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), paths_(paths), messages_(messages), node_(node), anchor_(anchor),
      model_(paths, profile.identityId, group.id), serverId_(group.id),
      contacts_(members), group_(true), pinned_(true), adultAllowed_(false),
      section_(SectionMembers), mediaKind_(Chat::MediaImage),
      titleBar_(0), rail_(0), side_(0), sideTitle_(0), scopeBox_(0),
      content_(0), gallery_(0), contentStack_(0), compose_(0), composeWrap_(0),
      settings_(0), voice_(0), voiceButton_(0)
{
    if (!model_.load()) {
        QString title = group.title.trimmed();
        if (title.isEmpty())
            title = QString::fromLatin1("Group");
        model_ = ServerModel::createDefault(paths_, profile_.identityId, group.id,
                                            title, QString(), profile_.displayName);
        for (int i = 0; i < members.size(); ++i) {
            Server::Member member;
            member.identityId = members.at(i).id;
            member.displayName = members.at(i).bestName();
            model_.addMember(member);
        }
        model_.save(0);
    }

    adultAllowed_ = SettingsStore(paths_).load().adultAllowed;

    buildUi();
    onSectionChanged(SectionMembers);
    followAnchor();
}

void ServerWindow::buildUi()
{
    setWindowTitle(model_.name());
    setStyleSheet(MeeruStyle::sheet());
    resize(kWindowWidth, kWindowHeight);
    setMinimumSize(640, 470);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("meeruRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(root);

    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    titleBar_ = new MeeruTitleBar(model_.name(), true, true, root);
    titleBar_->addPinButton(true);
    connect(titleBar_, SIGNAL(pinToggled(bool)), this, SLOT(onPinToggled(bool)));
    layout->addWidget(titleBar_);

    // --- the hero: server picture, name and one line about it
    hero_ = new BannerFrame(root);
    hero_->setFixedHeight(96);
    hero_->setCursor(Qt::ArrowCursor);

    heroAvatar_ = new AvatarFrame(hero_);
    heroAvatar_->setTileSize(56);
    heroAvatar_->setInitials(MeeruPaint::initialsFor(model_.name()));

    QHBoxLayout *heroLayout = new QHBoxLayout(hero_);
    heroLayout->setContentsMargins(16 - heroAvatar_->pictureInset(), 0, 16, 0);
    heroLayout->setSpacing(14);
    heroLayout->addWidget(heroAvatar_, 0, Qt::AlignVCenter);

    QWidget *heroText = new QWidget(hero_);
    QVBoxLayout *heroTextLayout = new QVBoxLayout(heroText);
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(4);

    ImageStore heroIcon(pictureDirectory(), QString::fromLatin1("avatar"));
    ImageStore heroBanner(pictureDirectory(), QString::fromLatin1("banner"));
    heroAvatar_->setImage(heroIcon);
    hero_->setImage(heroBanner);

    heroName_ = new QLabel(model_.name(), heroText);
    heroName_->setObjectName(QString::fromLatin1("heroName"));
    heroSubtitle_ = new QLabel(heroText);
    heroSubtitle_->setObjectName(QString::fromLatin1("heroSubtitle"));

    heroTextLayout->addWidget(heroName_);
    heroTextLayout->addWidget(heroSubtitle_);
    heroLayout->addWidget(heroText, 1, Qt::AlignVCenter);
    layout->addWidget(hero_);

    // --- the sections, as a row of tabs like the main window uses
    QWidget *tabStrip = new QWidget(root);
    tabStrip->setObjectName(QString::fromLatin1("serverTabs"));
    tabStrip->setAttribute(Qt::WA_StyledBackground, true);
    tabStrip->setFixedHeight(34);

    QHBoxLayout *tabLayout = new QHBoxLayout(tabStrip);
    tabLayout->setContentsMargins(12, 0, 12, 0);
    tabLayout->setSpacing(2);

    rail_ = new QButtonGroup(this);
    rail_->setExclusive(true);
    for (int i = 0; i < 9; ++i) {
        const SectionInfo &info = kSections[i];
        if (group_ && (info.id == SectionChannels || info.id == SectionThreads))
            continue;

        QPushButton *button = new QPushButton(QString::fromLatin1(info.label), tabStrip);
        button->setObjectName(QString::fromLatin1("serverTab"));
        button->setCheckable(true);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedHeight(30);
        if (info.id == SectionMembers)
            button->setChecked(true);
        rail_->addButton(button, info.id);
        tabLayout->addWidget(button);
    }
    tabLayout->addStretch();
    layout->addWidget(tabStrip);
    connect(rail_, SIGNAL(buttonClicked(int)), this, SLOT(onSectionChanged(int)));

    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // --- the panel that changes with the section
    QWidget *sidePane = new QWidget(root);
    sidePane->setObjectName(QString::fromLatin1("serverSide"));
    sidePane->setAttribute(Qt::WA_StyledBackground, true);
    sidePane->setFixedWidth(kSideWidth);

    QVBoxLayout *sideLayout = new QVBoxLayout(sidePane);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(6);

    sideTitle_ = new QLabel(sidePane);
    sideTitle_->setObjectName(QString::fromLatin1("sectionLabel"));
    sideLayout->addWidget(sideTitle_);

    side_ = new QListWidget(sidePane);
    side_->setFrameShape(QFrame::NoFrame);
    side_->setStyleSheet(QString::fromLatin1(
        "QListWidget { background: transparent; border: 0; }"
        "QListWidget::item { padding: 4px 6px; border-radius: 5px; color: #E4D6EA; }"
        "QListWidget::item:hover { background: #2f2139; }"
        "QListWidget::item:selected { background: #4a3454; color: #ffffff; }"));
    sideLayout->addWidget(side_, 1);
    body->addWidget(sidePane);
    side_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(side_, SIGNAL(itemSelectionChanged()), this, SLOT(onSideChoice()));
    connect(side_, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onChannelMenu(QPoint)));

    // --- the content
    QWidget *right = new QWidget(root);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    QWidget *chatHead = new QWidget(right);
    chatHead->setObjectName(QString::fromLatin1("chatHead"));
    chatHead->setAttribute(Qt::WA_StyledBackground, true);
    chatHead->setFixedHeight(44);
    QHBoxLayout *chatHeadLayout = new QHBoxLayout(chatHead);
    chatHeadLayout->setContentsMargins(16, 0, 16, 0);
    chatHeadLayout->setSpacing(8);

    channelTitle_ = new QLabel(chatHead);
    channelTitle_->setObjectName(QString::fromLatin1("channelTitle"));
    channelMeta_ = new QLabel(chatHead);
    channelMeta_->setObjectName(QString::fromLatin1("channelMeta"));
    chatHeadLayout->addWidget(channelTitle_, 1);
    chatHeadLayout->addWidget(channelMeta_, 0, Qt::AlignRight);
    rightLayout->addWidget(chatHead);

    scopeBox_ = new QComboBox(right);
    scopeBox_->setFixedHeight(26);
    scopeBox_->hide();
    rightLayout->addWidget(scopeBox_);

    contentStack_ = new QStackedWidget(right);

    content_ = new QTextBrowser(contentStack_);
    content_->setObjectName(QString::fromLatin1("dmHistory"));
    content_->setFrameShape(QFrame::NoFrame);
    content_->setOpenLinks(false);
    contentStack_->addWidget(content_);

    gallery_ = new QListWidget(contentStack_);
    gallery_->setViewMode(QListView::IconMode);
    gallery_->setIconSize(QSize(96, 96));
    gallery_->setGridSize(QSize(112, 118));
    gallery_->setResizeMode(QListView::Adjust);
    gallery_->setMovement(QListView::Static);
    gallery_->setFrameShape(QFrame::NoFrame);
    gallery_->setStyleSheet(QString::fromLatin1(
        "QListWidget { background: #241B2E; border: 0; }"
        "QListWidget::item { border-radius: 6px; color: #C9B9CF; }"
        "QListWidget::item:hover { background: #2f2139; }"
        "QListWidget::item:selected { background: #4a3454; }"));
    contentStack_->addWidget(gallery_);

    settings_ = new ServerSettings(profile_, paths_, &model_, contentStack_);
    connect(settings_, SIGNAL(changed()), this, SLOT(onSettingsChanged()));
    connect(settings_, SIGNAL(memberActionRequested(QString,QString)),
            this, SIGNAL(memberActionRequested(QString,QString)));
    contentStack_->addWidget(settings_);

    rightLayout->addWidget(contentStack_, 1);

    composeWrap_ = new QWidget(right);
    composeWrap_->setObjectName(QString::fromLatin1("dmComposeWrap"));
    composeWrap_->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *composeLayout = new QHBoxLayout(composeWrap_);
    composeLayout->setContentsMargins(10, 8, 10, 8);
    composeLayout->setSpacing(6);

    QPushButton *plus = new QPushButton(QString::fromLatin1("+"), composeWrap_);
    plus->setObjectName(QString::fromLatin1("glassButton"));
    plus->setFixedSize(28, 28);
    plus->setToolTip(QString::fromLatin1("Attach, start a thread or a poll"));

    QPushButton *emoji = new QPushButton(QString::fromUtf8("\342\230\272"), composeWrap_);
    emoji->setObjectName(QString::fromLatin1("glassButton"));
    emoji->setFixedSize(28, 28);
    emoji->setToolTip(QString::fromLatin1("Emoji"));

    voiceButton_ = new QPushButton(QString::fromUtf8("\342\231\252"), composeWrap_);
    voiceButton_->setObjectName(QString::fromLatin1("glassButton"));
    voiceButton_->setFixedSize(28, 28);
    voiceButton_->setToolTip(QString::fromLatin1("Record a voice note"));

    composeLayout->addWidget(plus);
    composeLayout->addWidget(emoji);
    composeLayout->addWidget(voiceButton_);

    connect(plus, SIGNAL(clicked()), this, SLOT(onPlus()));
    connect(emoji, SIGNAL(clicked()), this, SLOT(onEmoji()));
    connect(voiceButton_, SIGNAL(clicked()), this, SLOT(onVoiceNote()));

    compose_ = new QLineEdit(composeWrap_);
    compose_->setObjectName(QString::fromLatin1("dmCompose"));
    compose_->setPlaceholderText(QString::fromLatin1("Write a message..."));
    compose_->setFixedHeight(28);
    QPushButton *send = new QPushButton(QString::fromLatin1("Send"), composeWrap_);
    send->setObjectName(QString::fromLatin1("primaryButton"));
    send->setFixedHeight(28);
    composeLayout->addWidget(compose_, 1);
    composeLayout->addWidget(send);
    rightLayout->addWidget(composeWrap_);

    body->addWidget(right, 1);
    layout->addLayout(body, 1);

    connect(send, SIGNAL(clicked()), this, SLOT(onSend()));
    connect(compose_, SIGNAL(returnPressed()), this, SLOT(onSend()));
    connect(content_, SIGNAL(anchorClicked(QUrl)), this, SLOT(onHistoryLink(QUrl)));
    connect(gallery_, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onSideChoice()));
    connect(scopeBox_, SIGNAL(currentIndexChanged(int)), this, SLOT(onChannelFilter(int)));
}

// ------------------------------------------------------------------- sections

void ServerWindow::onSectionChanged(int section)
{
    section_ = section;
    rebuildSide();
    rebuildContent();
}

void ServerWindow::rebuildSide()
{
    side_->clear();
    scopeBox_->hide();

    switch (section_) {
    case SectionMembers:  sideTitle_->setText(QString::fromLatin1("MEMBERS"));  fillMembers();      break;
    case SectionChannels: sideTitle_->setText(QString::fromLatin1("CHANNELS")); fillChannels();     break;
    case SectionMedia:    sideTitle_->setText(QString::fromLatin1("MEDIA"));    fillMediaKinds();   break;
    case SectionThreads:  sideTitle_->setText(QString::fromLatin1("THREADS"));  fillThreads();      break;
    case SectionPinned:   sideTitle_->setText(QString::fromLatin1("PINNED"));   fillPinned();       break;
    case SectionMentions: sideTitle_->setText(QString::fromLatin1("MENTIONS")); fillMentions();     break;
    case SectionLinks:    sideTitle_->setText(QString::fromLatin1("CHANNELS")); fillLinkChannels(); break;
    case SectionFiles:    sideTitle_->setText(QString::fromLatin1("FILE KIND")); fillFileKinds();   break;
    case SectionSettings: sideTitle_->setText(QString::fromLatin1("SETTINGS")); fillSettings();     break;
    default: break;
    }

    if (side_->count() > 0 && !side_->currentItem())
        side_->setCurrentRow(0);
}

void ServerWindow::fillMembers()
{
    const QList<Server::Member> members = model_.membersByRank();
    QString lastRole;

    for (int i = 0; i < members.size(); ++i) {
        const Server::Member &member = members.at(i);
        const Server::Role role = model_.role(member.roleId);

        if (role.name != lastRole) {
            QListWidgetItem *header = new QListWidgetItem(
                (role.name.isEmpty() ? QString::fromLatin1("Members") : role.name).toUpper(), side_);
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(QColor(0x9d, 0x8b, 0xa5));
            header->setSizeHint(QSize(0, 22));
            lastRole = role.name;
        }

        const bool self = member.identityId == profile_.identityId;
        const bool online = self || (node_ && node_->isOnline(member.identityId));
        const int state = online ? Presence::stateFromKey(profile_.presence) : Presence::Invisible;

        // Drawn rather than listed: picture, halo in the colour of the state,
        // name in the colour of the role, and what they are up to underneath.
        QWidget *row = new QWidget(side_);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(2, 3, 2, 3);
        rowLayout->setSpacing(9);

        AvatarFrame *avatar = new AvatarFrame(row);
        avatar->setTileSize(30);
        avatar->setInitials(MeeruPaint::initialsFor(
            member.nickname.isEmpty() ? member.displayName : member.nickname));
        avatar->setPresenceColor(Presence::color(state), online);

        const QString directory = ImageStore::peerDirectory(paths_, profile_.identityId,
                                                            member.identityId);
        ImageStore picture(directory, QString::fromLatin1("avatar"));
        if (self)
            picture = ImageStore(paths_, profile_.identityId, QString::fromLatin1("avatar"));
        avatar->setImage(picture);
        rowLayout->addWidget(avatar, 0, Qt::AlignVCenter);

        QWidget *column = new QWidget(row);
        QVBoxLayout *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(1);

        QLabel *name = new QLabel(member.nickname.isEmpty() ? member.displayName : member.nickname,
                                  column);
        name->setObjectName(QString::fromLatin1("memberName"));
        if (!role.colour.isEmpty())
            name->setStyleSheet(QString::fromLatin1("color: %1;").arg(role.colour));

        QLabel *status = new QLabel(online ? Presence::label(state)
                                           : QString::fromLatin1("Offline"), column);
        status->setObjectName(QString::fromLatin1("memberStatus"));

        columnLayout->addWidget(name);
        columnLayout->addWidget(status);
        rowLayout->addWidget(column, 1);

        QListWidgetItem *item = new QListWidgetItem(side_);
        item->setData(Qt::UserRole, member.identityId);
        item->setSizeHint(QSize(0, 44));
        side_->setItemWidget(item, row);
    }
}

void ServerWindow::fillChannels()
{
    QString lastCategory;
    const int kinds[] = { Server::ChannelText, Server::ChannelVoice,
                          Server::ChannelPost, Server::ChannelPoll };

    for (int k = 0; k < 4; ++k) {
        const QList<Server::Channel> channels = model_.channelsOfKind(kinds[k]);
        for (int i = 0; i < channels.size(); ++i) {
            const Server::Channel &channel = channels.at(i);
            if (!channelVisible(channel))
                continue;

            const QList<Server::Category> categories = model_.categories();
            QString categoryName;
            for (int c = 0; c < categories.size(); ++c) {
                if (categories.at(c).id == channel.categoryId)
                    categoryName = categories.at(c).name;
            }
            if (categoryName != lastCategory && !categoryName.isEmpty()) {
                QListWidgetItem *header = new QListWidgetItem(categoryName.toUpper(), side_);
                header->setFlags(Qt::NoItemFlags);
                header->setForeground(QColor(0x9d, 0x8b, 0xa5));
                lastCategory = categoryName;
            }

            QString prefix;
            switch (channel.kind) {
            case Server::ChannelVoice: prefix = QString::fromUtf8("\342\231\252 "); break;
            case Server::ChannelPost:  prefix = QString::fromUtf8("\342\226\244 "); break;
            case Server::ChannelPoll:  prefix = QString::fromUtf8("\342\230\221 "); break;
            default:                   prefix = QString::fromLatin1("# "); break;
            }

            QListWidgetItem *item = new QListWidgetItem(
                prefix + channel.name + (channel.adultOnly ? QString::fromLatin1("  18+") : QString()), side_);
            item->setData(Qt::UserRole, channel.id);
            if (channel.id == channelId_)
                side_->setCurrentItem(item);
        }
    }
}

void ServerWindow::fillMediaKinds()
{
    const char *labels[] = { "Images", "Videos", "GIFs", "Music" };
    const int kinds[] = { Chat::MediaImage, Chat::MediaVideo, Chat::MediaAnimation, Chat::MediaAudio };
    for (int i = 0; i < 4; ++i) {
        QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1(labels[i]), side_);
        item->setData(Qt::UserRole, kinds[i]);
        if (kinds[i] == mediaKind_)
            side_->setCurrentItem(item);
    }

    // The channel the gallery draws from, with everything as the default.
    scopeBox_->blockSignals(true);
    scopeBox_->clear();
    scopeBox_->addItem(QString::fromLatin1("All channels"), QString());
    const QList<Server::Channel> text = model_.channelsOfKind(Server::ChannelText);
    for (int i = 0; i < text.size(); ++i) {
        if (channelVisible(text.at(i)))
            scopeBox_->addItem(QString::fromLatin1("# ") + text.at(i).name, text.at(i).id);
    }
    const QList<Server::Channel> threads = model_.threadsOf(QString());
    for (int i = 0; i < threads.size(); ++i)
        scopeBox_->addItem(QString::fromLatin1("> ") + threads.at(i).name, threads.at(i).id);
    scopeBox_->blockSignals(false);
    scopeBox_->show();
}

void ServerWindow::fillThreads()
{
    const QList<Server::Channel> threads = model_.threadsOf(QString());
    for (int i = 0; i < threads.size(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1("> ") + threads.at(i).name, side_);
        item->setData(Qt::UserRole, threads.at(i).id);
    }
    if (threads.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem(QString::fromLatin1("No threads yet"), side_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void ServerWindow::fillPinned()
{
    // Pinned messages live in the store like any other; what marks them is
    // being listed here, so the left column is the index and the right column
    // is the conversation opened at that point.
    const QList<Server::Channel> channels = model_.channelsOfKind(Server::ChannelText);
    for (int c = 0; c < channels.size(); ++c) {
        if (!channelVisible(channels.at(c)))
            continue;
        const QList<Chat::Message> messages = messagesOf(channels.at(c).id);
        for (int i = 0; i < messages.size(); ++i) {
            if (!messages.at(i).text.startsWith(QLatin1String("[pinned]")))
                continue;
            QListWidgetItem *item = new QListWidgetItem(messages.at(i).text.mid(8).simplified().left(48), side_);
            item->setData(Qt::UserRole, channels.at(c).id);
            item->setData(Qt::UserRole + 1, messages.at(i).id);
        }
    }
    if (side_->count() == 0) {
        QListWidgetItem *empty = new QListWidgetItem(QString::fromLatin1("Nothing pinned"), side_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void ServerWindow::fillMentions()
{
    const QString handle = QLatin1Char('@') + profile_.displayName;
    const QList<Server::Channel> channels = model_.channelsOfKind(Server::ChannelText);

    for (int c = 0; c < channels.size(); ++c) {
        if (!channelVisible(channels.at(c)))
            continue;
        const QList<Chat::Message> messages = messagesOf(channels.at(c).id);
        for (int i = 0; i < messages.size(); ++i) {
            const Chat::Message &message = messages.at(i);
            if (message.isMine())
                continue;
            const bool named = message.text.contains(handle, Qt::CaseInsensitive)
                            || message.text.contains(QLatin1Char('@') + profile_.identityId.left(12));
            if (!named)
                continue;
            QListWidgetItem *item = new QListWidgetItem(
                message.authorName + QString::fromLatin1(": ") + message.text.simplified().left(40), side_);
            item->setData(Qt::UserRole, channels.at(c).id);
            item->setData(Qt::UserRole + 1, message.id);
        }
    }
    if (side_->count() == 0) {
        QListWidgetItem *empty = new QListWidgetItem(QString::fromLatin1("Nobody has mentioned you"), side_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void ServerWindow::fillLinkChannels()
{
    const QList<Server::Channel> channels = model_.channelsOfKind(Server::ChannelText);
    QListWidgetItem *all = new QListWidgetItem(QString::fromLatin1("All channels"), side_);
    all->setData(Qt::UserRole, QString());
    for (int i = 0; i < channels.size(); ++i) {
        if (!channelVisible(channels.at(i)))
            continue;
        QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1("# ") + channels.at(i).name, side_);
        item->setData(Qt::UserRole, channels.at(i).id);
    }
}

void ServerWindow::fillFileKinds()
{
    const char *labels[] = { "Documents", "Audio", "Everything else" };
    const int kinds[] = { Chat::MediaDocument, Chat::MediaAudio, Chat::MediaOther };
    for (int i = 0; i < 3; ++i) {
        QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1(labels[i]), side_);
        item->setData(Qt::UserRole, kinds[i]);
    }
}

void ServerWindow::fillSettings()
{
    for (int page = ServerSettings::PageProfile; page <= ServerSettings::PageAudit; ++page) {
        QListWidgetItem *item = new QListWidgetItem(ServerSettings::pageName(page), side_);
        item->setData(Qt::UserRole, page);
    }

    // Not a page of its own: one switch that belongs with the rest.
    QListWidgetItem *adult = new QListWidgetItem(QString::fromLatin1("Adult channels"), side_);
    adult->setData(Qt::UserRole, -1);
}

// -------------------------------------------------------------------- content

bool ServerWindow::channelVisible(const Server::Channel &channel) const
{
    // An adult channel stays out of sight, and out of every filter, until the
    // reader has said in Settings that they are one.
    return !channel.adultOnly || adultAllowed_;
}

QString ServerWindow::currentConversationId() const
{
    if (group_)
        return serverId_;
    const Server::Channel channel = model_.channel(channelId_);
    return channel.isValid() ? channel.conversationId(serverId_) : QString();
}

QList<Chat::Message> ServerWindow::messagesOf(const QString &channelId) const
{
    if (!messages_)
        return QList<Chat::Message>();
    if (group_)
        return messages_->history(serverId_);
    const Server::Channel channel = model_.channel(channelId);
    if (!channel.isValid() || !channelVisible(channel))
        return QList<Chat::Message>();
    return messages_->history(channel.conversationId(serverId_));
}

QString ServerWindow::renderMessages(const QList<Chat::Message> &messages, const QString &highlightId) const
{
    QString html = QString::fromLatin1(
        "<style>"
        "p { margin: 0 0 2px 0; }"
        ".who { margin-top: 12px; font-size: 12px; }"
        ".time { color: #9d8ba5; font-size: 10px; }"
        ".body { color: #F1E6F5; font-size: 13px; margin-left: 2px; }"
        ".mark { color: #9d8ba5; font-size: 10px; }"
        ".hit { background: #4a3454; color: #ffffff; font-size: 13px; }"
        "</style>");

    QString lastAuthor;
    for (int i = 0; i < messages.size(); ++i) {
        const Chat::Message &message = messages.at(i);
        const QString who = message.isMine() ? profile_.displayName : message.authorName;

        if (who != lastAuthor) {
            // Name in the colour of the writer's role, then the time, the way
            // the mockup lays it out.
            QString colour = QString::fromLatin1("#DFB2F4");
            const QList<Server::Member> members = model_.members();
            for (int m = 0; m < members.size(); ++m) {
                if (members.at(m).displayName == who || members.at(m).identityId == message.authorId) {
                    const Server::Role role = model_.role(members.at(m).roleId);
                    if (!role.colour.isEmpty())
                        colour = role.colour;
                    break;
                }
            }

            html += QString::fromLatin1(
                        "<p class=\"who\"><span style=\"color:%1;\"><b>%2</b></span>"
                        "&nbsp;&nbsp;<span class=\"time\">%3</span></p>")
                        .arg(colour)
                        .arg(escape(who))
                        .arg(message.sentAtUtc.toLocalTime().toString(QString::fromLatin1("h:mm AP")));
            lastAuthor = who;
        }

        QString body = escape(message.text);
        if (message.kind == Chat::KindFile && message.attachment.isValid()) {
            const Chat::Attachment &file = message.attachment;
            const QString size = file.fileSize > 1024 * 1024
                ? QString::fromLatin1("%1 MB").arg(file.fileSize / (1024.0 * 1024.0), 0, 'f', 1)
                : QString::fromLatin1("%1 KB").arg(qMax(qint64(1), file.fileSize / 1024));

            // Percent encoded, so nothing in the identifier can be mistaken for
            // part of the URL itself.
            const QString where = QString::fromLatin1(
                QUrl::toPercentEncoding(message.conversationId));

            body = QString::fromLatin1("%1 <span class=\"mark\">%2</span>")
                       .arg(escape(file.fileName)).arg(size);

            if (file.transfer == Chat::TransferComplete) {
                // A voice note plays where it sits; anything else opens.
                if (file.media == Chat::MediaAudio) {
                    body += QString::fromLatin1(" <a href=\"meeru:play/%1/%2\">Play</a>")
                                .arg(where).arg(message.id);
                }
                body += QString::fromLatin1(" <a href=\"meeru:view/%1/%2\">Open</a>")
                            .arg(where).arg(message.id);
            } else if (!message.isMine()) {
                body += QString::fromLatin1(" <a href=\"meeru:receive/%1/%2\">Receive</a>")
                            .arg(where).arg(message.id);
            } else {
                body += QString::fromLatin1(" <span class=\"mark\">offered</span>");
            }
        }

        html += QString::fromLatin1("<p class=\"%1\">%2</p>")
                    .arg(message.id == highlightId ? QString::fromLatin1("hit") : QString::fromLatin1("body"))
                    .arg(body);
    }
    return html;
}

void ServerWindow::rebuildContent()
{
    // The hero and the channel header always describe what is on screen.
    int online = 0;
    const QList<Server::Member> allMembers = model_.members();
    for (int i = 0; i < allMembers.size(); ++i) {
        if (node_ && node_->isOnline(allMembers.at(i).identityId))
            ++online;
    }

    // The description is what the server says about itself, the way a person
    // has a status line; the topic is only the first channel's subject.
    QString lead = model_.description().trimmed();
    if (lead.isEmpty())
        lead = model_.topic().trimmed();

    heroSubtitle_->setText(lead.isEmpty()
        ? QString::fromLatin1("%1 members, %2 online").arg(allMembers.size()).arg(online)
        : QString::fromLatin1("%1  -  %2 members, %3 online")
              .arg(lead).arg(allMembers.size()).arg(online));

    const Server::Channel current = model_.channel(channelId_);
    channelTitle_->setText(group_ ? model_.name()
                                  : (current.isValid() ? QString::fromLatin1("# ") + current.name
                                                       : model_.name()));
    channelMeta_->setText(QString::fromLatin1("%1 online").arg(online));

    if (compose_) {
        compose_->setPlaceholderText(group_
            ? QString::fromLatin1("Write in %1...").arg(model_.name())
            : QString::fromLatin1("Write in %1...").arg(current.isValid() ? current.name
                                                                         : model_.name()));
    }

    const bool chatty = (section_ == SectionMembers || section_ == SectionChannels
                         || section_ == SectionThreads);
    composeWrap_->setVisible(chatty);
    if (section_ == SectionMedia)
        contentStack_->setCurrentWidget(gallery_);
    else if (section_ != SectionSettings)
        contentStack_->setCurrentWidget(content_);

    if (section_ == SectionMedia) {
        showGallery();
        return;
    }
    if (section_ == SectionSettings) {
        contentStack_->setCurrentWidget(settings_);
        onSideChoice();
        return;
    }
    if (chatty) {
        showChat(channelId_);
        return;
    }
    onSideChoice();
}

QString ServerWindow::pictureDirectory() const
{
    return paths_.identityDirectory(profile_.identityId) + QLatin1String("/servers/")
         + serverId_ + QLatin1String("-pictures");
}

void ServerWindow::refreshPresence()
{
    rebuildSide();
    rebuildContent();
}

void ServerWindow::showChat(const QString &channelId)
{
    const QList<Chat::Message> messages = group_ ? messagesOf(QString()) : messagesOf(channelId);
    content_->setHtml(renderMessages(messages, QString()));
    content_->verticalScrollBar()->setValue(content_->verticalScrollBar()->maximum());
}

void ServerWindow::showAnchoredHistory(const QString &channelId, const QString &messageId)
{
    const QList<Chat::Message> messages = messagesOf(channelId);
    content_->setHtml(renderMessages(messages, messageId));
}

void ServerWindow::showFiltered(int mediaKind, bool linksOnly)
{
    QList<Chat::Message> hits;
    QList<Server::Channel> channels = model_.channelsOfKind(Server::ChannelText);
    if (group_)
        channels.clear();

    QStringList scope;
    if (group_) {
        scope.append(QString());
    } else {
        for (int i = 0; i < channels.size(); ++i) {
            if (channelVisible(channels.at(i)))
                scope.append(channels.at(i).id);
        }
    }

    for (int c = 0; c < scope.size(); ++c) {
        const QList<Chat::Message> messages = messagesOf(scope.at(c));
        for (int i = 0; i < messages.size(); ++i) {
            const Chat::Message &message = messages.at(i);
            if (linksOnly) {
                if (message.kind == Chat::KindText && hasLink(message.text))
                    hits.append(message);
                continue;
            }
            if (message.kind == Chat::KindFile && message.attachment.media == mediaKind)
                hits.append(message);
        }
    }

    content_->setHtml(renderMessages(hits, QString()));
}

void ServerWindow::showGallery()
{
    gallery_->clear();

    const QString scoped = scopeBox_->itemData(scopeBox_->currentIndex()).toString();
    QStringList channels;
    if (group_) {
        channels.append(QString());
    } else if (!scoped.isEmpty()) {
        channels.append(scoped);
    } else {
        const QList<Server::Channel> text = model_.channelsOfKind(Server::ChannelText);
        for (int i = 0; i < text.size(); ++i) {
            if (channelVisible(text.at(i)))
                channels.append(text.at(i).id);
        }
        const QList<Server::Channel> threads = model_.threadsOf(QString());
        for (int i = 0; i < threads.size(); ++i)
            channels.append(threads.at(i).id);
    }

    for (int c = 0; c < channels.size(); ++c) {
        const QList<Chat::Message> messages = messagesOf(channels.at(c));
        for (int i = 0; i < messages.size(); ++i) {
            const Chat::Message &message = messages.at(i);
            if (message.kind != Chat::KindFile || message.attachment.media != mediaKind_)
                continue;

            QListWidgetItem *item = new QListWidgetItem(
                message.attachment.fileName.left(18), gallery_);
            item->setData(Qt::UserRole, message.conversationId);
            item->setData(Qt::UserRole + 1, message.id);
            item->setToolTip(message.attachment.fileName);

            // Only what has actually been received can show a thumbnail; the
            // rest is listed so it can be asked for.
            QPixmap thumb;
            if (message.attachment.transfer == Chat::TransferComplete
                && !message.attachment.localPath.isEmpty()) {
                thumb = QPixmap(message.attachment.localPath);
            }
            item->setIcon(thumb.isNull()
                ? QIcon(MeeruPaint::initialsTile(QString::fromLatin1("?"), QSize(96, 96), 8))
                : QIcon(thumb.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }

    if (gallery_->count() == 0) {
        QListWidgetItem *empty = new QListWidgetItem(QString::fromLatin1("Nothing here yet"), gallery_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

// --------------------------------------------------------------------- events

void ServerWindow::onSideChoice()
{
    QListWidgetItem *item = side_->currentItem();
    if (!item)
        return;

    switch (section_) {
    case SectionChannels:
    case SectionThreads: {
        channelId_ = item->data(Qt::UserRole).toString();
        const Server::Channel chosen = model_.channel(channelId_);

        // Joining a voice channel is a call with everybody in the server.
        if (chosen.kind == Server::ChannelVoice) {
            QStringList participants;
            const QList<Server::Member> members = model_.members();
            for (int i = 0; i < members.size(); ++i) {
                if (members.at(i).identityId != profile_.identityId)
                    participants.append(members.at(i).identityId);
            }
            emit callRequested(chosen.conversationId(serverId_), participants,
                               model_.name() + QString::fromLatin1(" - ") + chosen.name, true);
            return;
        }

        showChat(channelId_);
        break;
    }

    case SectionMembers:
        showChat(channelId_);
        break;

    case SectionMedia:
        mediaKind_ = item->data(Qt::UserRole).toInt();
        showGallery();
        break;

    case SectionPinned:
    case SectionMentions:
        showAnchoredHistory(item->data(Qt::UserRole).toString(),
                            item->data(Qt::UserRole + 1).toString());
        break;

    case SectionLinks:
        channelId_ = item->data(Qt::UserRole).toString();
        showFiltered(0, true);
        break;

    case SectionFiles:
        showFiltered(item->data(Qt::UserRole).toInt(), false);
        break;

    case SectionSettings: {
        const int page = item->data(Qt::UserRole).toInt();

        if (page < 0) {
            // Adult channels: asked once, then applied everywhere at once.
            AppSettings values = SettingsStore(paths_).load();
            const bool now = !values.adultAllowed;
            if (now && !MeeruDialog::confirm(this, QString::fromLatin1("Adult channels"),
                    QString::fromLatin1("Channels marked 18+ are hidden until you confirm you are an "
                                        "adult. Turning this on shows them everywhere in this window, "
                                        "including in the media and file lists."),
                    QString::fromLatin1("I am an adult"))) {
                return;
            }
            values.adultAllowed = now;
            SettingsStore(paths_).save(values, 0);
            adultAllowed_ = now;
            model_.note(profile_.displayName, now ? QString::fromLatin1("Enabled adult channels")
                                                  : QString::fromLatin1("Disabled adult channels"));
            model_.save(0);
            rebuildSide();
            rebuildContent();
            return;
        }

        contentStack_->setCurrentWidget(settings_);
        settings_->showPage(page);
        break;
    }

    default:
        break;
    }
}

void ServerWindow::onChannelMenu(const QPoint &where)
{
    QListWidgetItem *item = side_->itemAt(where);

    if (section_ == SectionMembers) {
        onMemberMenu(where);
        return;
    }
    if (section_ != SectionChannels && section_ != SectionThreads)
        return;

    const bool mayManage = model_.may(profile_.identityId, Server::PermManageChannels);
    const QString targetId = item ? item->data(Qt::UserRole).toString() : QString();

    QMenu menu(this);
    QAction *create = menu.addAction(QString::fromLatin1("New channel"));
    QAction *edit = menu.addAction(QString::fromLatin1("Rename"));
    QAction *duplicate = menu.addAction(QString::fromLatin1("Duplicate"));
    QAction *mute = menu.addAction(QString::fromLatin1("Mute this channel"));
    menu.addSeparator();
    QAction *invite = menu.addAction(QString::fromLatin1("Invite somebody here"));
    menu.addSeparator();
    QAction *remove = menu.addAction(QString::fromLatin1("Delete"));

    create->setEnabled(mayManage);
    edit->setEnabled(mayManage && !targetId.isEmpty());
    duplicate->setEnabled(mayManage && !targetId.isEmpty());
    remove->setEnabled(mayManage && !targetId.isEmpty());
    invite->setEnabled(model_.may(profile_.identityId, Server::PermCreateInvites));
    mute->setEnabled(!targetId.isEmpty());

    QAction *chosen = menu.exec(side_->viewport()->mapToGlobal(where));
    if (!chosen)
        return;

    const Server::Channel target = model_.channel(targetId);

    if (chosen == create) {
        QString name = QString::fromLatin1("new-channel");
        if (!MeeruDialog::promptText(this, QString::fromLatin1("New channel"),
                                     QString::fromLatin1("What is it called?"), &name))
            return;
        Server::Channel channel;
        channel.name = name;
        channel.kind = Server::ChannelText;
        channel.categoryId = target.categoryId;
        model_.addChannel(channel);
        model_.note(profile_.displayName, QString::fromLatin1("Created channel ") + name);
    } else if (chosen == edit) {
        QString name = target.name;
        if (!MeeruDialog::promptText(this, QString::fromLatin1("Rename channel"),
                                     QString::fromLatin1("New name"), &name))
            return;
        model_.removeChannel(targetId);
        Server::Channel renamed = target;
        renamed.name = name;
        model_.addChannel(renamed);
        model_.note(profile_.displayName, QString::fromLatin1("Renamed a channel to ") + name);
    } else if (chosen == duplicate) {
        Server::Channel copy = target;
        copy.id.clear();
        copy.name = target.name + QString::fromLatin1("-copy");
        model_.addChannel(copy);
        model_.note(profile_.displayName, QString::fromLatin1("Duplicated ") + target.name);
    } else if (chosen == remove) {
        if (!MeeruDialog::confirm(this, QString::fromLatin1("Delete channel"),
                                  QString::fromLatin1("Delete %1? Everything written in it stays on "
                                                      "the machines that already have it, but it "
                                                      "disappears from this server.").arg(target.name),
                                  QString::fromLatin1("Delete")))
            return;
        model_.removeChannel(targetId);
        model_.note(profile_.displayName, QString::fromLatin1("Deleted ") + target.name);
    } else if (chosen == mute) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Muted"),
                                 QString::fromLatin1("%1 will not raise a notification.").arg(target.name));
        return;
    } else if (chosen == invite) {
        Server::Invite entry;
        entry.createdBy = profile_.displayName;
        entry.expiresAtUtc = QDateTime::currentDateTimeUtc().addDays(7);
        model_.addInvite(entry);
        model_.save(0);
        MeeruDialog::showMessage(this, QString::fromLatin1("Invite"),
                                 QString::fromLatin1("Invite created. Find it under Settings, Invites."));
        return;
    }

    model_.save(0);
    rebuildSide();
    rebuildContent();
}

void ServerWindow::onMemberMenu(const QPoint &where)
{
    QListWidgetItem *item = side_->itemAt(where);
    if (!item)
        return;
    const QString identityId = item->data(Qt::UserRole).toString();
    if (identityId.isEmpty() || identityId == profile_.identityId)
        return;

    QMenu menu(this);
    QAction *dm = menu.addAction(QString::fromLatin1("Send a message"));
    QAction *befriend = menu.addAction(QString::fromLatin1("Send a friend request"));
    QAction *nickname = menu.addAction(QString::fromLatin1("Give them a nickname here"));
    menu.addSeparator();
    QAction *suspend = menu.addAction(QString::fromLatin1("Suspend"));
    QAction *kick = menu.addAction(QString::fromLatin1("Remove from the server"));
    QAction *ban = menu.addAction(QString::fromLatin1("Ban"));

    // Each of these appears only where the role actually allows it.
    suspend->setEnabled(model_.may(profile_.identityId, Server::PermSuspendMembers));
    kick->setEnabled(model_.may(profile_.identityId, Server::PermKickMembers));
    ban->setEnabled(model_.may(profile_.identityId, Server::PermBanMembers));
    nickname->setEnabled(model_.may(profile_.identityId, Server::PermManageNicknames));

    QAction *chosen = menu.exec(side_->viewport()->mapToGlobal(where));
    if (!chosen)
        return;

    if (chosen == nickname) {
        QString name;
        if (!MeeruDialog::promptText(this, QString::fromLatin1("Nickname"),
                                     QString::fromLatin1("Shown only inside this server"), &name))
            return;
        QList<Server::Member> members = model_.members();
        for (int i = 0; i < members.size(); ++i) {
            if (members.at(i).identityId == identityId) {
                model_.setMemberNickname(identityId, name);
                break;
            }
        }
        model_.note(profile_.displayName, QString::fromLatin1("Set a nickname"));
    } else if (chosen == kick || chosen == ban) {
        model_.removeMember(identityId);
        model_.note(profile_.displayName, chosen == ban ? QString::fromLatin1("Banned a member")
                                                        : QString::fromLatin1("Removed a member"));
    } else if (chosen == suspend) {
        model_.setMemberSuspended(identityId, true);
        model_.note(profile_.displayName, QString::fromLatin1("Suspended a member"));
    } else if (chosen == dm || chosen == befriend) {
        emit memberActionRequested(identityId, chosen == dm ? QString::fromLatin1("message")
                                                            : QString::fromLatin1("befriend"));
        return;
    }

    model_.save(0);
    rebuildSide();
}

void ServerWindow::onEmoji()
{
    EmojiDialog dialog(paths_, profile_.identityId, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.chosen().isEmpty()) {
        compose_->insert(QLatin1Char(':') + dialog.chosen() + QLatin1Char(':'));
        compose_->setFocus();
    }
}

void ServerWindow::onVoiceNote()
{
    if (!voice_) {
        voice_ = new VoiceRecorder(this);
    }

    if (voice_->isRecording()) {
        const QString finished = voice_->stop();
        voiceButton_->setText(QString::fromUtf8("\342\231\252"));
        if (!finished.isEmpty())
            sendAttachment(finished);
        return;
    }

    const QString directory = messages_ ? messages_->attachmentDirectory() : QString();
    if (directory.isEmpty() || !QDir().mkpath(directory))
        return;

    QString error;
    const QString target = directory + QLatin1String("/voice-")
                         + QDateTime::currentDateTimeUtc().toString(QString::fromLatin1("yyyyMMdd-HHmmss"))
                         + QLatin1String(".wav");
    if (!voice_->start(target, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Voice note"), error);
        return;
    }
    voiceButton_->setText(QString::fromUtf8("\342\226\240"));
}

void ServerWindow::onPlus()
{
    QMenu menu(this);
    QAction *file = menu.addAction(QString::fromLatin1("Send a file"));
    QAction *picture = menu.addAction(QString::fromLatin1("Send a picture or video"));
    QAction *camera = menu.addAction(QString::fromLatin1("Take one with the camera"));
    menu.addSeparator();
    QAction *thread = menu.addAction(QString::fromLatin1("Start a thread"));
    QAction *poll = menu.addAction(QString::fromLatin1("Create a poll"));

    camera->setEnabled(CameraSource::isAvailable());
    thread->setEnabled(!group_ && model_.may(profile_.identityId, Server::PermCreateThreads));
    poll->setEnabled(model_.may(profile_.identityId, Server::PermSendPolls));

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;

    if (chosen == file || chosen == picture) {
        const QString filter = chosen == picture
            ? QString::fromLatin1("Pictures and video (*.png *.jpg *.jpeg *.gif *.bmp *.mp4 *.avi *.mkv)")
            : QString::fromLatin1("All files (*)");
        const QString path = QFileDialog::getOpenFileName(this, QString::fromLatin1("Send"),
                                                          QDir::homePath(), filter);
        if (!path.isEmpty())
            sendAttachment(path);
        return;
    }

    if (chosen == camera) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Camera"),
                                 QString::fromLatin1("Taking a still from the camera arrives with the "
                                                     "next update; the camera itself already works in "
                                                     "calls."));
        return;
    }

    if (chosen == thread) {
        QString name = QString::fromLatin1("new thread");
        if (!MeeruDialog::promptText(this, QString::fromLatin1("New thread"),
                                     QString::fromLatin1("What is it about?"), &name))
            return;
        Server::Channel entry;
        entry.name = name;
        entry.kind = Server::ChannelThread;
        entry.parentId = channelId_;
        model_.addChannel(entry);
        model_.note(profile_.displayName, QString::fromLatin1("Started thread ") + name);
        model_.save(0);
        rebuildSide();
        return;
    }

    if (chosen == poll) {
        PollDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const Chat::Poll built = dialog.poll();
        if (!built.isValid() || !messages_)
            return;

        Chat::Message message;
        message.conversationId = currentConversationId();
        message.kind = Chat::KindPoll;
        message.delivery = Chat::DeliveryWaiting;
        message.sentAtUtc = QDateTime::currentDateTimeUtc();
        message.authorName = profile_.displayName;
        message.text = built.question;
        message.poll = built;

        const Chat::Message stored = messages_->append(message);
        if (stored.isValid())
            showChat(channelId_);
    }
}

void ServerWindow::onMediaFilter(int index)
{
    Q_UNUSED(index);
    showGallery();
}

void ServerWindow::onChannelFilter(int index)
{
    Q_UNUSED(index);
    if (section_ == SectionMedia)
        showGallery();
}

void ServerWindow::sendAttachment(const QString &path)
{
    QFileInfo info(path);
    const QString conversationId = currentConversationId();
    if (!info.exists() || !messages_ || conversationId.isEmpty())
        return;
    if (!model_.may(profile_.identityId, Server::PermAttachFiles)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Attach"),
                                 QString::fromLatin1("Your role here does not allow attaching files."));
        return;
    }

    Chat::Message message;
    message.conversationId = conversationId;
    message.kind = Chat::KindFile;
    message.delivery = Chat::DeliveryWaiting;
    message.sentAtUtc = QDateTime::currentDateTimeUtc();
    message.authorName = profile_.displayName;
    message.text = info.fileName();
    message.attachment.fileId = Chat::newMessageId();
    message.attachment.fileName = info.fileName();
    message.attachment.fileSize = info.size();
    message.attachment.media = Chat::Attachment::mediaForName(info.fileName());
    message.attachment.localPath = path;

    const Chat::Message stored = messages_->append(message);
    if (!stored.isValid())
        return;

    if (node_ && node_->transfers())
        node_->transfers()->registerOutgoing(stored.attachment.fileId, path);
    if (node_) {
        const QList<Server::Member> members = model_.members();
        for (int i = 0; i < members.size(); ++i) {
            if (members.at(i).identityId != profile_.identityId)
                node_->sendMessage(members.at(i).identityId, conversationId, stored);
        }
    }
    showChat(channelId_);
}

void ServerWindow::onSend()
{
    const QString text = compose_->text().trimmed();
    const QString conversationId = currentConversationId();
    if (text.isEmpty() || conversationId.isEmpty() || !messages_)
        return;

    Chat::Message message;
    message.conversationId = conversationId;
    message.text = text;
    message.kind = Chat::KindText;
    message.delivery = Chat::DeliveryWaiting;
    message.sentAtUtc = QDateTime::currentDateTimeUtc();
    message.authorName = profile_.displayName;

    const Chat::Message stored = messages_->append(message);
    if (!stored.isValid())
        return;

    compose_->clear();
    if (node_) {
        const QList<Server::Member> members = model_.members();
        for (int i = 0; i < members.size(); ++i) {
            if (members.at(i).identityId != profile_.identityId)
                node_->sendMessage(members.at(i).identityId, conversationId, stored);
        }
    }
    showChat(channelId_);
}

void ServerWindow::appendMessage(const Chat::Message &message)
{
    if (message.conversationId == currentConversationId())
        showChat(channelId_);
}

void ServerWindow::onHistoryLink(const QUrl &url)
{
    const QString path = url.toString();
    if (!path.startsWith(QLatin1String("meeru:")))
        return;

    const QStringList parts = path.mid(6).split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() != 3)
        return;

    const QString conversationId = QUrl::fromPercentEncoding(parts.at(1).toUtf8());
    const QString messageId = parts.at(2);
    const Chat::Message message = messages_->message(conversationId, messageId);

    if (parts.first() == QLatin1String("receive")) {
        if (!node_ || message.authorId.isEmpty()
            || !node_->receiveAttachment(message.authorId, conversationId, messageId)) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Receive"),
                                     QString::fromLatin1("That file can only be fetched while the "
                                                         "person who sent it is online."));
        }
        return;
    }

    if (parts.first() == QLatin1String("play") && message.attachment.isValid()) {
        QString error;
        if (!VoicePlayer::instance()->play(message.attachment.localPath, &error))
            MeeruDialog::showMessage(this, QString::fromLatin1("Play"), error);
        return;
    }

    if (parts.first() == QLatin1String("view") && message.attachment.isValid()) {
        if (viewers_.contains(messageId)) {
            viewers_.value(messageId)->raise();
            return;
        }
        MediaWindow *viewer = new MediaWindow(profile_, paths_, message, anchor_, 0);
        connect(viewer, SIGNAL(closed()), viewer, SLOT(deleteLater()));
        viewers_.insert(messageId, viewer);
        viewer->show();
    }
}

void ServerWindow::onSettingsChanged()
{
    // The name, description and halo all appear elsewhere in this window.
    heroName_->setText(model_.name());
    titleBar_->setTitle(model_.name());
    setWindowTitle(model_.name());
    heroAvatar_->setInitials(MeeruPaint::initialsFor(model_.name()));

    // Reloaded from disk, so a new icon or banner shows the moment it is saved.
    ImageStore icon(pictureDirectory(), QString::fromLatin1("avatar"));
    ImageStore banner(pictureDirectory(), QString::fromLatin1("banner"));
    heroAvatar_->setImage(icon);
    hero_->setImage(banner);

    const QColor halo(model_.haloColour());
    if (halo.isValid())
        heroAvatar_->setPresenceColor(halo, true);

    rebuildContent();
}

void ServerWindow::onAdultToggled(bool allowed)
{
    adultAllowed_ = allowed;
    rebuildSide();
    rebuildContent();
}

void ServerWindow::onPinToggled(bool pinned)
{
    pinned_ = pinned;
    if (pinned_)
        followAnchor();
}

void ServerWindow::followAnchor()
{
    if (!pinned_ || !anchor_ || !anchor_->isVisible())
        return;
    const QRect frame = anchor_->frameGeometry();
    move(frame.right() + kDockGap, frame.top());
}

void ServerWindow::closeEvent(QCloseEvent *event)
{
    const QList<MediaWindow *> viewers = viewers_.values();
    viewers_.clear();
    for (int i = 0; i < viewers.size(); ++i)
        viewers.at(i)->close();

    emit closed(serverId_);
    event->accept();
}
