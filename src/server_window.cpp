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

#include "app_settings.h"
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
const int kRailWidth = 104;
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
      content_(0), gallery_(0), contentStack_(0), compose_(0), composeWrap_(0)
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
      content_(0), gallery_(0), contentStack_(0), compose_(0), composeWrap_(0)
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
    setMinimumSize(620, 460);

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

    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // --- the rail of sections
    QWidget *rail = new QWidget(root);
    rail->setObjectName(QString::fromLatin1("serverRail"));
    rail->setAttribute(Qt::WA_StyledBackground, true);
    rail->setFixedWidth(kRailWidth);

    QVBoxLayout *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(6, 8, 6, 8);
    railLayout->setSpacing(2);

    rail_ = new QButtonGroup(this);
    rail_->setExclusive(true);
    for (int i = 0; i < 9; ++i) {
        const SectionInfo &info = kSections[i];
        if (group_ && (info.id == SectionChannels || info.id == SectionThreads))
            continue;   // a group has neither

        QPushButton *button = new QPushButton(QString::fromLatin1(info.label), rail);
        button->setObjectName(QString::fromLatin1("railButton"));
        button->setCheckable(true);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedHeight(28);
        if (info.id == SectionMembers)
            button->setChecked(true);
        rail_->addButton(button, info.id);
        railLayout->addWidget(button);
    }
    railLayout->addStretch();
    body->addWidget(rail);
    connect(rail_, SIGNAL(buttonClicked(int)), this, SLOT(onSectionChanged(int)));

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
        "QListWidget::item { padding: 5px 6px; border-radius: 4px; color: #E4D6EA; }"
        "QListWidget::item:hover { background: #35264048; }"
        "QListWidget::item:selected { background: #4a3454; color: #ffffff; }"));
    sideLayout->addWidget(side_, 1);
    body->addWidget(sidePane);
    connect(side_, SIGNAL(itemSelectionChanged()), this, SLOT(onSideChoice()));

    // --- the content
    QWidget *right = new QWidget(root);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

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

    rightLayout->addWidget(contentStack_, 1);

    composeWrap_ = new QWidget(right);
    composeWrap_->setObjectName(QString::fromLatin1("dmComposeWrap"));
    composeWrap_->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *composeLayout = new QHBoxLayout(composeWrap_);
    composeLayout->setContentsMargins(10, 8, 10, 8);
    composeLayout->setSpacing(8);

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

        // Grouped under their role, highest first, the way Discord does it.
        if (role.name != lastRole) {
            QListWidgetItem *header = new QListWidgetItem(
                (role.name.isEmpty() ? QString::fromLatin1("Members") : role.name).toUpper(), side_);
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(QColor(0x9d, 0x8b, 0xa5));
            lastRole = role.name;
        }

        const bool online = node_ && node_->isOnline(member.identityId);
        const int state = online ? Presence::Available : Presence::Invisible;

        QListWidgetItem *item = new QListWidgetItem(member.displayName, side_);
        item->setData(Qt::UserRole, member.identityId);

        const QString directory = ImageStore::peerDirectory(paths_, profile_.identityId, member.identityId);
        ImageStore avatar(directory, QString::fromLatin1("avatar"));
        QPixmap tile;
        if (avatar.hasImage())
            tile = MeeruPaint::roundedFromPixmap(QPixmap(avatar.filePath()), QSize(22, 22), 5);
        if (tile.isNull())
            tile = MeeruPaint::initialsTile(MeeruPaint::initialsFor(member.displayName), QSize(22, 22), 5);
        item->setIcon(QIcon(tile));

        if (!role.colour.isEmpty())
            item->setForeground(QColor(role.colour));
        item->setToolTip(member.displayName + QString::fromLatin1("\n")
                         + (online ? Presence::label(state) : QString::fromLatin1("Offline")));
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
    const char *labels[] = { "Channels", "Categories", "Roles", "Members",
                             "Picture and banner", "Adult channels", "Audit log", "Custom emoji" };
    for (int i = 0; i < 8; ++i) {
        QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1(labels[i]), side_);
        item->setData(Qt::UserRole, i);
    }
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
        ".who { margin-top: 8px; color: #DFB2F4; font-size: 11px; }"
        ".time { color: #9d8ba5; font-size: 10px; }"
        ".body { color: #F1E6F5; font-size: 12px; }"
        ".mark { color: #9d8ba5; font-size: 10px; }"
        ".hit { background: #4a3454; color: #ffffff; font-size: 12px; }"
        "</style>");

    QString lastAuthor;
    for (int i = 0; i < messages.size(); ++i) {
        const Chat::Message &message = messages.at(i);
        const QString who = message.isMine() ? profile_.displayName : message.authorName;

        if (who != lastAuthor) {
            html += QString::fromLatin1("<p class=\"who\"><b>%1</b> <span class=\"time\">%2</span></p>")
                        .arg(escape(who))
                        .arg(message.sentAtUtc.toLocalTime().toString(QString::fromLatin1("h:mm AP")));
            lastAuthor = who;
        }

        QString body = escape(message.text);
        if (message.kind == Chat::KindFile && message.attachment.isValid()) {
            body = escape(message.attachment.fileName);
            if (message.attachment.transfer == Chat::TransferComplete) {
                body += QString::fromLatin1(" <a href=\"meeru:view/%1/%2\">Open</a>")
                            .arg(message.conversationId).arg(message.id);
            } else if (!message.isMine()) {
                body += QString::fromLatin1(" <a href=\"meeru:receive/%1/%2\">Receive</a>")
                            .arg(message.conversationId).arg(message.id);
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
    const bool chatty = (section_ == SectionMembers || section_ == SectionChannels
                         || section_ == SectionThreads);
    composeWrap_->setVisible(chatty);
    contentStack_->setCurrentWidget(section_ == SectionMedia ? static_cast<QWidget *>(gallery_)
                                                             : static_cast<QWidget *>(content_));

    if (section_ == SectionMedia) {
        showGallery();
        return;
    }
    if (section_ == SectionSettings) {
        onSideChoice();
        return;
    }
    if (chatty) {
        showChat(channelId_);
        return;
    }
    onSideChoice();
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
        if (page == 5) {
            // Adult channels: a plain switch, and nothing is shown until it is on.
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
        }

        QString html;
        switch (page) {
        case 0: {
            html = QString::fromLatin1("<p class=\"who\"><b>Channels</b></p>");
            const QList<Server::Channel> channels = model_.channels();
            for (int i = 0; i < channels.size(); ++i) {
                html += QString::fromLatin1("<p class=\"body\">%1%2</p>")
                            .arg(escape(channels.at(i).name))
                            .arg(channels.at(i).adultOnly ? QString::fromLatin1("  (18+)") : QString());
            }
            break;
        }
        case 2: {
            html = QString::fromLatin1("<p class=\"who\"><b>Roles</b></p>");
            const QList<Server::Role> roles = model_.roles();
            for (int i = 0; i < roles.size(); ++i) {
                html += QString::fromLatin1("<p class=\"body\">%1 <span class=\"mark\">rank %2</span></p>")
                            .arg(escape(roles.at(i).name)).arg(roles.at(i).rank);
            }
            break;
        }
        case 3: {
            html = QString::fromLatin1("<p class=\"who\"><b>Members</b></p>");
            const QList<Server::Member> members = model_.membersByRank();
            for (int i = 0; i < members.size(); ++i) {
                html += QString::fromLatin1("<p class=\"body\">%1 <span class=\"mark\">%2</span></p>")
                            .arg(escape(members.at(i).displayName))
                            .arg(escape(model_.role(members.at(i).roleId).name));
            }
            break;
        }
        case 5:
            html = QString::fromLatin1("<p class=\"body\">Adult channels are currently %1.</p>")
                       .arg(adultAllowed_ ? QString::fromLatin1("shown") : QString::fromLatin1("hidden"));
            break;
        case 6: {
            html = QString::fromLatin1("<p class=\"who\"><b>Audit log</b></p>");
            const QList<Server::AuditEntry> audit = model_.audit();
            for (int i = audit.size() - 1; i >= 0; --i) {
                html += QString::fromLatin1("<p class=\"body\">%1 <span class=\"mark\">%2</span></p>")
                            .arg(escape(audit.at(i).actorName + QString::fromLatin1(": ")
                                        + audit.at(i).description))
                            .arg(audit.at(i).atUtc.toLocalTime()
                                     .toString(QString::fromLatin1("d MMM h:mm AP")));
            }
            break;
        }
        default:
            html = QString::fromLatin1("<p class=\"body\">This part of the settings is not "
                                       "editable from here yet.</p>");
            break;
        }
        content_->setHtml(QString::fromLatin1(
            "<style>.who{color:#DFB2F4;font-size:12px;}.body{color:#F1E6F5;font-size:12px;}"
            ".mark{color:#9d8ba5;font-size:10px;}</style>") + html);
        break;
    }

    default:
        break;
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

    const QString conversationId = parts.at(1);
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
