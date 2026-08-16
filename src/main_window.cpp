#include "main_window.h"

#include <QAction>
#include <QButtonGroup>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QCursor>
#include <QEvent>
#include <QHash>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <QTimer>

#include "contact_card.h"
#include "firewall_helper.h"
#include "meeru_dialogs.h"
#include "meeru_paint.h"
#include "meeru_style.h"
#include "meeru_window.h"
#include "presence.h"

namespace {

enum RowKind {
    RowHeader = 0,
    RowEntry = 1
};

enum RosterRole {
    KindRole = Qt::UserRole + 1,
    PictureRole,
    TitleRole,
    SubtitleRole,
    InitialsRole,
    DotColorRole,
    IdRole,
    BadgeRole,
    TrailingRole,
    FavoriteRole
};

const int kAvatarSide = 29;
const int kRowHeight = 43;
const int kHeaderHeight = 28;
const int kLeftPad = 12;

// Rows are painted rather than built from widgets: a Pentium III does not need
// dozens of live QLabels just to show a contact list.
class RosterDelegate : public QStyledItemDelegate
{
public:
    explicit RosterDelegate(QObject *parent = 0) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        Q_UNUSED(option);
        const int kind = index.data(KindRole).toInt();
        return QSize(0, kind == RowHeader ? kHeaderHeight : kRowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);

        const QRect rect = option.rect;
        const int kind = index.data(KindRole).toInt();

        if (kind == RowHeader) {
            QFont font(QString::fromLatin1("Segoe UI"));
            font.setPixelSize(11);
            painter->setFont(font);
            painter->setPen(MeeruStyle::muted());
            painter->drawText(rect.adjusted(kLeftPad, 0, -kLeftPad, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              index.data(TitleRole).toString());
            const QString trailing = index.data(TrailingRole).toString();
            if (!trailing.isEmpty()) {
                painter->drawText(rect.adjusted(kLeftPad, 0, -kLeftPad, 0),
                                  Qt::AlignVCenter | Qt::AlignRight, trailing);
            }
            painter->restore();
            return;
        }

        const bool selected = (option.state & QStyle::State_Selected) != 0;
        const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
        if (selected) {
            painter->fillRect(rect, QColor(223, 178, 244, 36));
            painter->fillRect(QRect(rect.left(), rect.top(), 2, rect.height()), MeeruStyle::lavender());
        } else if (hovered) {
            painter->fillRect(rect, QColor(223, 178, 244, 16));
        }

        const QRect tileRect(rect.left() + kLeftPad,
                             rect.top() + (rect.height() - kAvatarSide) / 2,
                             kAvatarSide, kAvatarSide);
        const QPixmap picture = qvariant_cast<QPixmap>(index.data(PictureRole));
        if (!picture.isNull())
            painter->drawPixmap(tileRect, picture);
        else
            painter->drawPixmap(tileRect, tile(index.data(InitialsRole).toString()));

        const int textLeft = tileRect.right() + 8;
        const int textRight = rect.right() - kLeftPad;

        QFont nameFont(QString::fromLatin1("Segoe UI"));
        nameFont.setPixelSize(12);
        nameFont.setBold(true);

        QFont subFont(QString::fromLatin1("Segoe UI"));
        subFont.setPixelSize(10);

        const QString badge = index.data(BadgeRole).toString();
        int available = textRight - textLeft;
        int badgeWidth = 0;
        if (!badge.isEmpty()) {
            QFontMetrics badgeMetrics(subFont);
            badgeWidth = badgeMetrics.width(badge) + 10;
            available -= badgeWidth;
        }

        const QColor dotColor = qvariant_cast<QColor>(index.data(DotColorRole));
        const int dotSpace = dotColor.isValid() ? 14 : 0;

        QFontMetrics nameMetrics(nameFont);
        const QString name = nameMetrics.elidedText(index.data(TitleRole).toString(),
                                                    Qt::ElideRight,
                                                    qMax(10, available - dotSpace));
        painter->setFont(nameFont);
        painter->setPen(MeeruStyle::text());
        const QRect nameRect(textLeft, rect.top() + 6, available, 15);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);

        if (dotColor.isValid()) {
            const int dotX = textLeft + nameMetrics.width(name) + 6;
            const int dotY = nameRect.center().y() - 3;
            painter->setPen(QPen(QColor(0x20, 0x16, 0x27), 1));
            painter->setBrush(dotColor);
            painter->drawEllipse(QRect(dotX, dotY, 8, 8));
        }

        painter->setFont(subFont);
        painter->setPen(MeeruStyle::muted());
        QFontMetrics subMetrics(subFont);
        const QString subtitle = subMetrics.elidedText(index.data(SubtitleRole).toString(),
                                                       Qt::ElideRight, qMax(10, available));
        painter->drawText(QRect(textLeft, nameRect.bottom() + 1, available, 13),
                          Qt::AlignLeft | Qt::AlignVCenter, subtitle);

        if (!badge.isEmpty()) {
            const QRect badgeRect(textRight - badgeWidth + 2,
                                  rect.top() + (rect.height() - 16) / 2,
                                  badgeWidth - 4, 16);
            QPainterPath path;
            path.addRoundedRect(QRectF(badgeRect), 8, 8);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(223, 178, 244, 38));
            painter->drawPath(path);
            painter->setPen(MeeruStyle::lavender());
            painter->setFont(subFont);
            painter->drawText(badgeRect, Qt::AlignCenter, badge);
        }

        if (index.data(FavoriteRole).toBool()) {
            painter->setPen(MeeruStyle::pink());
            painter->drawText(QRect(rect.left() + 2, rect.top(), 10, rect.height()),
                              Qt::AlignCenter, QString::fromUtf8("\342\230\205"));
        }

        painter->restore();
    }

private:
    const QPixmap &tile(const QString &initials) const
    {
        QHash<QString, QPixmap>::const_iterator it = cache_.constFind(initials);
        if (it != cache_.constEnd())
            return it.value();
        cache_.insert(initials, MeeruPaint::initialsTile(initials, QSize(kAvatarSide, kAvatarSide), 6));
        return cache_[initials];
    }

    mutable QHash<QString, QPixmap> cache_;
};

QString presenceSentence(int state)
{
    switch (state) {
    case Presence::Absent:       return QString::fromLatin1("Away from the keyboard");
    case Presence::DoNotDisturb: return QString::fromLatin1("Do not disturb");
    case Presence::Invisible:    return QString::fromLatin1("Appearing offline");
    default:                     return QString::fromLatin1("Online and ready to chat");
    }
}

bool promptText(QWidget *parent, const QString &title, const QString &label, QString *value)
{
    MeeruDialog dialog(title, parent);
    dialog.setDialogWidth(340);

    QLabel *caption = new QLabel(label);
    caption->setObjectName(QString::fromLatin1("dialogLabel"));
    caption->setWordWrap(true);
    dialog.contentLayout()->addWidget(caption);

    QLineEdit *edit = new QLineEdit();
    edit->setFixedHeight(30);
    edit->setText(*value);
    dialog.contentLayout()->addWidget(edit);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"));
    QPushButton *accept = new QPushButton(QString::fromLatin1("Save"));
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);

    QObject::connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    QObject::connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));
    QObject::connect(edit, SIGNAL(returnPressed()), &dialog, SLOT(accept()));

    edit->setFocus();
    edit->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return false;

    *value = edit->text().trimmed();
    return true;
}

}

MainWindow::MainWindow(const LocalProfile &profile, const MeeruPaths &paths, QWidget *parent)
    : QMainWindow(parent),
      profile_(profile),
      paths_(paths),
      settings_(paths),
      roster_(paths, profile.identityId),
      avatarStore_(paths, profile.identityId, QString::fromLatin1("avatar")),
      bannerStore_(paths, profile.identityId, QString::fromLatin1("banner")),
      presence_(Presence::Available),
      currentTab_(MessagesTab),
      committingStatus_(false),
      titleBar_(0), banner_(0), avatar_(0), nameLabel_(0), presenceDot_(0), stateLabel_(0), statusLabel_(0),
      statusEdit_(0), statusStack_(0), search_(0), tabs_(0), addButton_(0),
      list_(0), emptyLabel_(0), listStack_(0), newsLabel_(0), footerLabel_(0),
      node_(0), card_(0), hoverTimer_(0)
{
    const AppSettings saved = settings_.load();
    presence_ = Presence::stateFromKey(saved.presence.isEmpty() ? profile_.presence : saved.presence);
    statusText_ = saved.statusText;

    // An unreadable roster starts empty rather than blocking sign-in.
    roster_.load(0);

    buildUi();
    refreshProfile();
    refreshAvatar();
    refreshBanner();
    refreshList();
    refreshNews();
    startNetwork();
}

void MainWindow::buildUi()
{
    setWindowTitle(QString::fromLatin1("Meeru Messenger"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setStyleSheet(MeeruStyle::sheet());
    setMinimumSize(300, 470);
    resize(300, 560);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("meeruRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(root);

    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    titleBar_ = new MeeruTitleBar(QString::fromLatin1("Meeru Messenger"), true, true, root);
    layout->addWidget(titleBar_);
    layout->addWidget(buildProfileHeader(root));
    layout->addWidget(buildSearchRow(root));
    layout->addWidget(buildTabRow(root));

    listStack_ = new QStackedWidget(root);

    list_ = new QListWidget(listStack_);
    list_->setItemDelegate(new RosterDelegate(list_));
    list_->setMouseTracking(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(false);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    listStack_->addWidget(list_);

    emptyLabel_ = new QLabel(listStack_);
    emptyLabel_->setObjectName(QString::fromLatin1("emptyState"));
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setContentsMargins(24, 0, 24, 0);
    listStack_->addWidget(emptyLabel_);

    layout->addWidget(listStack_, 1);

    QWidget *news = new QWidget(root);
    news->setObjectName(QString::fromLatin1("news"));
    news->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *newsLayout = new QVBoxLayout(news);
    newsLayout->setContentsMargins(12, 8, 12, 8);
    newsLayout->setSpacing(4);
    QLabel *newsTitle = new QLabel(QString::fromLatin1("What's new"), news);
    newsTitle->setObjectName(QString::fromLatin1("newsTitle"));
    newsLabel_ = new QLabel(news);
    newsLabel_->setWordWrap(true);
    newsLayout->addWidget(newsTitle);
    newsLayout->addWidget(newsLabel_);
    layout->addWidget(news);

    layout->addWidget(buildFooter(root));

    connect(list_, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(onItemActivated(QListWidgetItem*)));
    connect(list_, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onItemActivated(QListWidgetItem*)));
    connect(list_, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onContextMenu(QPoint)));
    connect(list_, SIGNAL(itemEntered(QListWidgetItem*)), this, SLOT(onHoverItem(QListWidgetItem*)));

    card_ = new ContactCard(this);
    hoverTimer_ = new QTimer(this);
    hoverTimer_->setSingleShot(true);
    hoverTimer_->setInterval(1000);
    connect(hoverTimer_, SIGNAL(timeout()), this, SLOT(onHoverTimeout()));
    list_->viewport()->installEventFilter(this);
}

QWidget *MainWindow::buildProfileHeader(QWidget *parent)
{
    banner_ = new BannerFrame(parent);
    banner_->setFixedHeight(100);

    avatar_ = new AvatarFrame(banner_);
    avatar_->setTileSize(55);
    avatar_->setInteractive(true);
    avatar_->setToolTip(QString::fromLatin1("Change your profile picture"));

    // The avatar widget is wider than its picture because the halo needs room.
    // Pull the layout in by that much so the picture, not the glow, lines up
    // with the 12px padding of the mockup.
    const int inset = avatar_->pictureInset();

    QHBoxLayout *layout = new QHBoxLayout(banner_);
    layout->setContentsMargins(12 - inset, 0, 12, 0);
    layout->setSpacing(qMax(0, 10 - inset));
    layout->addWidget(avatar_, 0, Qt::AlignVCenter);

    QWidget *column = new QWidget(banner_);
    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);

    // Fixed line boxes, taken from the mockup: 16px name, 11px state, 10px
    // personal message, with 4px and 10px between them. Letting Qt derive the
    // heights from point sizes is what made the block drift away from the
    // picture.
    nameLabel_ = new QLabel(column);
    nameLabel_->setObjectName(QString::fromLatin1("profileName"));
    nameLabel_->setFixedHeight(22);

    // A status dot ahead of the line, the way every messenger of this shape
    // has done it. The dot is clickable as well, since it is the thing people
    // aim at when they want to change their state.
    presenceDot_ = new ClickableLabel(column);
    presenceDot_->setFixedSize(10, 15);
    presenceDot_->setAlignment(Qt::AlignCenter);
    presenceDot_->setToolTip(QString::fromLatin1("Click to change your status"));

    stateLabel_ = new ClickableLabel(column);
    stateLabel_->setObjectName(QString::fromLatin1("profileState"));
    stateLabel_->setFixedHeight(15);
    stateLabel_->setToolTip(QString::fromLatin1("Click to change your status"));

    QHBoxLayout *stateRow = new QHBoxLayout();
    stateRow->setContentsMargins(0, 0, 0, 0);
    stateRow->setSpacing(6);
    stateRow->addWidget(presenceDot_);
    stateRow->addWidget(stateLabel_, 1);

    statusStack_ = new QStackedWidget(column);
    statusStack_->setFixedHeight(20);

    statusLabel_ = new ClickableLabel(statusStack_);
    statusLabel_->setObjectName(QString::fromLatin1("personalMessage"));
    statusLabel_->setToolTip(QString::fromLatin1("Click to write a personal message"));
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusStack_->addWidget(statusLabel_);

    statusEdit_ = new QLineEdit(statusStack_);
    statusEdit_->setObjectName(QString::fromLatin1("personalEdit"));
    statusEdit_->setMaxLength(90);
    statusEdit_->setPlaceholderText(QString::fromLatin1("What are you up to?"));
    statusStack_->addWidget(statusEdit_);

    columnLayout->addWidget(nameLabel_);
    columnLayout->addSpacing(4);
    columnLayout->addLayout(stateRow);
    columnLayout->addSpacing(10);
    columnLayout->addWidget(statusStack_);

    // The column keeps its natural height so both it and the picture can be
    // centred against each other instead of hanging from the top.
    layout->addWidget(column, 1, Qt::AlignVCenter);

    connect(banner_, SIGNAL(clicked()), this, SLOT(onBannerClicked()));
    connect(avatar_, SIGNAL(clicked()), this, SLOT(onAvatarClicked()));
    connect(stateLabel_, SIGNAL(clicked()), this, SLOT(onPresenceClicked()));
    connect(presenceDot_, SIGNAL(clicked()), this, SLOT(onPresenceClicked()));
    connect(statusLabel_, SIGNAL(clicked()), this, SLOT(onStatusClicked()));
    connect(statusEdit_, SIGNAL(returnPressed()), this, SLOT(commitStatus()));
    connect(statusEdit_, SIGNAL(editingFinished()), this, SLOT(commitStatus()));

    return banner_;
}

QWidget *MainWindow::buildSearchRow(QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    row->setObjectName(QString::fromLatin1("searchRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    search_ = new QLineEdit(row);
    search_->setFixedHeight(30);
    search_->setPlaceholderText(QString::fromLatin1("Search your contacts, chats and servers..."));

    QPushButton *magnifier = new QPushButton(QString::fromUtf8("\342\214\225"), row);
    magnifier->setObjectName(QString::fromLatin1("glassButton"));
    magnifier->setFixedSize(30, 30);
    magnifier->setToolTip(QString::fromLatin1("Clear the search"));

    layout->addWidget(search_, 1);
    layout->addWidget(magnifier);

    connect(search_, SIGNAL(textChanged(QString)), this, SLOT(onSearchChanged(QString)));
    connect(magnifier, SIGNAL(clicked()), search_, SLOT(clear()));

    return row;
}

QWidget *MainWindow::buildTabRow(QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    row->setObjectName(QString::fromLatin1("tabRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 2, 6, 0);
    layout->setSpacing(0);

    tabs_ = new QButtonGroup(this);
    tabs_->setExclusive(true);

    const char *names[3] = { "Messages", "Servers", "Contacts" };
    for (int i = 0; i < 3; ++i) {
        QPushButton *button = new QPushButton(QString::fromLatin1(names[i]), row);
        button->setObjectName(QString::fromLatin1("tabButton"));
        button->setCheckable(true);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        if (i == currentTab_)
            button->setChecked(true);
        tabs_->addButton(button, i);
        layout->addWidget(button);
    }

    layout->addStretch();

    addButton_ = new QPushButton(QString::fromLatin1("+"), row);
    addButton_->setObjectName(QString::fromLatin1("glassButton"));
    addButton_->setFixedSize(24, 24);
    addButton_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(addButton_);

    connect(tabs_, SIGNAL(buttonClicked(int)), this, SLOT(onTabChanged(int)));
    connect(addButton_, SIGNAL(clicked()), this, SLOT(onAddClicked()));

    return row;
}

QWidget *MainWindow::buildFooter(QWidget *parent)
{
    QWidget *footer = new QWidget(parent);
    footer->setObjectName(QString::fromLatin1("footer"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    footer->setFixedHeight(31);

    QHBoxLayout *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(12, 0, 4, 0);
    layout->setSpacing(6);

    footerLabel_ = new QLabel(footer);
    footerLabel_->setObjectName(QString::fromLatin1("footerText"));

    QPushButton *settingsButton = new QPushButton(QString::fromUtf8("\342\232\231"), footer);
    settingsButton->setObjectName(QString::fromLatin1("settingsButton"));
    settingsButton->setToolTip(QString::fromLatin1("Settings"));
    settingsButton->setFixedSize(24, 24);
    settingsButton->setFocusPolicy(Qt::NoFocus);
    settingsButton->setCursor(Qt::PointingHandCursor);

    layout->addWidget(footerLabel_);
    layout->addStretch();
    layout->addWidget(settingsButton);
    layout->addWidget(new QSizeGrip(footer), 0, Qt::AlignBottom | Qt::AlignRight);

    connect(settingsButton, SIGNAL(clicked()), this, SLOT(onSettings()));

    return footer;
}

// ------------------------------------------------------------------- refreshing

void MainWindow::refreshProfile()
{
    nameLabel_->setText(profile_.displayName + QString::fromLatin1(" (")
                        + Presence::label(presence_) + QString::fromLatin1(")"));
    stateLabel_->setText(presenceSentence(presence_));
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(Presence::color(presence_), 10, 9));

    if (statusText_.trimmed().isEmpty())
        statusLabel_->setText(QString::fromLatin1("Set a personal message..."));
    else
        statusLabel_->setText(statusText_);

    footerLabel_->setText(QString::fromUtf8("Meeru Messenger \302\267 ") + profile_.shortId());
    setWindowTitle(QString::fromLatin1("Meeru Messenger - ") + profile_.displayName);
    if (titleBar_)
        titleBar_->setTitle(QString::fromLatin1("Meeru Messenger"));

    avatar_->setInitials(MeeruPaint::initialsFor(profile_.displayName));
    avatar_->setPresenceColor(Presence::color(presence_), true);
}

void MainWindow::refreshAvatar()
{
    avatar_->setInitials(MeeruPaint::initialsFor(profile_.displayName));
    avatar_->setImage(avatarStore_);
}

void MainWindow::refreshBanner()
{
    banner_->setImage(bannerStore_);
}

void MainWindow::applyPresence(int state, bool animate)
{
    presence_ = state;

    AppSettings values = settings_.load();
    values.activeIdentityId = profile_.identityId;
    values.displayName = profile_.displayName;
    values.presence = Presence::key(presence_);
    values.statusText = statusText_;
    settings_.save(values, 0);

    nameLabel_->setText(profile_.displayName + QString::fromLatin1(" (")
                        + Presence::label(presence_) + QString::fromLatin1(")"));
    stateLabel_->setText(presenceSentence(presence_));
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(Presence::color(presence_), 10, 9));
    avatar_->setPresenceColor(Presence::color(presence_), animate);
    publishProfile();
}

bool MainWindow::matchesFilter(const QString &title, const QString &subtitle) const
{
    if (filter_.isEmpty())
        return true;
    return title.contains(filter_, Qt::CaseInsensitive) || subtitle.contains(filter_, Qt::CaseInsensitive);
}

void MainWindow::appendHeader(const QString &title, const QString &trailing)
{
    QListWidgetItem *item = new QListWidgetItem(list_);
    item->setData(KindRole, RowHeader);
    item->setData(TitleRole, title);
    item->setData(TrailingRole, trailing);
    item->setFlags(Qt::NoItemFlags);
}

void MainWindow::appendContact(const Roster::Contact &contact)
{
    QString subtitle = contact.statusText;
    QString badge;
    QColor dot;

    switch (contact.state) {
    case Roster::ContactPendingOutgoing:
        subtitle = QString::fromLatin1("Waiting for them to accept");
        badge = QString::fromLatin1("Sent");
        break;
    case Roster::ContactPendingIncoming:
        subtitle = QString::fromLatin1("Wants to add you");
        badge = QString::fromLatin1("New");
        break;
    case Roster::ContactBlocked:
        subtitle = QString::fromLatin1("Blocked");
        break;
    default: {
        const bool online = node_ && node_->isOnline(contact.id);
        if (subtitle.isEmpty()) {
            subtitle = online ? Presence::labelForKey(contact.presence)
                              : QString::fromLatin1("Offline");
        }
        dot = online ? Presence::colorForKey(contact.presence)
                     : Presence::color(Presence::Invisible);
        break;
    }
    }

    if (!matchesFilter(contact.bestName(), subtitle))
        return;

    QListWidgetItem *item = new QListWidgetItem(list_);
    item->setData(KindRole, RowEntry);
    item->setData(TitleRole, contact.bestName());
    item->setData(SubtitleRole, subtitle);
    item->setData(InitialsRole, MeeruPaint::initialsFor(contact.bestName()));
    item->setData(PictureRole, contactTile(contact));
    item->setData(IdRole, contact.id);
    item->setData(BadgeRole, badge);
    item->setData(FavoriteRole, contact.favorite);
    if (dot.isValid())
        item->setData(DotColorRole, dot);
    item->setData(Qt::ToolTipRole, QString::fromLatin1("meeru:") + contact.id);
}

void MainWindow::appendConversation(const Roster::Conversation &conversation)
{
    const QString title = roster_.conversationTitle(conversation);
    QString subtitle = conversation.preview;
    if (subtitle.isEmpty()) {
        subtitle = conversation.group
            ? QString::fromLatin1("Group with %1 members").arg(conversation.members.size() + 1)
            : QString::fromLatin1("Direct conversation");
    }
    if (!matchesFilter(title, subtitle))
        return;

    QListWidgetItem *item = new QListWidgetItem(list_);
    item->setData(KindRole, RowEntry);
    item->setData(TitleRole, title);
    item->setData(SubtitleRole, subtitle);
    item->setData(InitialsRole, MeeruPaint::initialsFor(title));
    item->setData(IdRole, conversation.id);
    item->setData(FavoriteRole, conversation.favorite);
    if (conversation.group)
        item->setData(BadgeRole, QString::fromLatin1("Group"));
}

void MainWindow::appendServer(const Roster::Server &server)
{
    QString subtitle = server.topic;
    if (subtitle.isEmpty())
        subtitle = server.owner ? QString::fromLatin1("You own this server")
                                : QString::fromLatin1("Member");
    if (!matchesFilter(server.name, subtitle))
        return;

    QListWidgetItem *item = new QListWidgetItem(list_);
    item->setData(KindRole, RowEntry);
    item->setData(TitleRole, server.name);
    item->setData(SubtitleRole, subtitle);
    item->setData(InitialsRole, MeeruPaint::initialsFor(server.name));
    item->setData(IdRole, server.id);
    item->setData(FavoriteRole, server.favorite);
    if (server.state == Roster::ServerPending)
        item->setData(BadgeRole, QString::fromLatin1("Waiting"));
    else if (server.owner)
        item->setData(BadgeRole, QString::fromLatin1("Owner"));
}

void MainWindow::refreshList()
{
    list_->clear();

    if (currentTab_ == MessagesTab) {
        const QList<Roster::Conversation> all = roster_.conversations();
        QList<Roster::Conversation> favorites;
        QList<Roster::Conversation> others;
        for (int i = 0; i < all.size(); ++i) {
            if (all.at(i).favorite)
                favorites.append(all.at(i));
            else
                others.append(all.at(i));
        }
        if (!favorites.isEmpty()) {
            appendHeader(QString::fromUtf8("\342\226\276 \342\230\206 Favorites"),
                         QString::fromLatin1("(%1)").arg(favorites.size()));
            for (int i = 0; i < favorites.size(); ++i)
                appendConversation(favorites.at(i));
        }
        if (!others.isEmpty()) {
            appendHeader(QString::fromUtf8("\342\226\276 Conversations"), QString());
            for (int i = 0; i < others.size(); ++i)
                appendConversation(others.at(i));
        }
        emptyLabel_->setText(QString::fromLatin1(
            "No conversations yet.\n\nUse + to start one with a contact."));
    } else if (currentTab_ == ServersTab) {
        const QList<Roster::Server> all = roster_.servers();
        for (int i = 0; i < all.size(); ++i)
            appendServer(all.at(i));
        emptyLabel_->setText(QString::fromLatin1(
            "You have not joined a server.\n\nUse + to create one or join with an invite code."));
    } else {
        const QList<Roster::Contact> all = roster_.contacts();
        QList<Roster::Contact> favorites;
        QList<Roster::Contact> accepted;
        QList<Roster::Contact> pending;
        for (int i = 0; i < all.size(); ++i) {
            const Roster::Contact &contact = all.at(i);
            if (contact.state == Roster::ContactPendingIncoming || contact.state == Roster::ContactPendingOutgoing)
                pending.append(contact);
            else if (contact.favorite)
                favorites.append(contact);
            else
                accepted.append(contact);
        }
        if (!pending.isEmpty()) {
            appendHeader(QString::fromUtf8("\342\226\276 Requests"),
                         QString::fromLatin1("(%1)").arg(pending.size()));
            for (int i = 0; i < pending.size(); ++i)
                appendContact(pending.at(i));
        }
        if (!favorites.isEmpty()) {
            appendHeader(QString::fromUtf8("\342\226\276 \342\230\206 Favorites"),
                         QString::fromLatin1("(%1)").arg(favorites.size()));
            for (int i = 0; i < favorites.size(); ++i)
                appendContact(favorites.at(i));
        }
        if (!accepted.isEmpty()) {
            appendHeader(QString::fromUtf8("\342\226\276 Other Contacts"), QString());
            for (int i = 0; i < accepted.size(); ++i)
                appendContact(accepted.at(i));
        }
        emptyLabel_->setText(QString::fromLatin1(
            "No contacts yet.\n\nUse + to add someone with their Meeru ID."));
    }

    bool hasEntries = false;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(KindRole).toInt() == RowEntry) {
            hasEntries = true;
            break;
        }
    }

    if (!hasEntries && !filter_.isEmpty())
        emptyLabel_->setText(QString::fromLatin1("Nothing here matches \"%1\".").arg(filter_));

    listStack_->setCurrentIndex(hasEntries ? 0 : 1);
}

void MainWindow::refreshNews()
{
    const int pending = roster_.pendingRequestCount();
    const int conversations = roster_.conversations().size();
    const int servers = roster_.servers().size();

    QStringList parts;
    if (pending > 0) {
        parts.append(QString::fromLatin1("%1 contact request%2 waiting")
                         .arg(pending).arg(pending == 1 ? QString() : QString::fromLatin1("s")));
    }
    if (conversations > 0)
        parts.append(QString::fromLatin1("%1 conversation%2").arg(conversations)
                         .arg(conversations == 1 ? QString() : QString::fromLatin1("s")));
    if (servers > 0)
        parts.append(QString::fromLatin1("%1 server%2").arg(servers)
                         .arg(servers == 1 ? QString() : QString::fromLatin1("s")));

    if (parts.isEmpty()) {
        newsLabel_->setText(QString::fromLatin1(
            "Your identity lives on this device. Share your Meeru ID to be reached."));
    } else {
        newsLabel_->setText(parts.join(QString::fromUtf8(" \302\267 ")));
    }
}

// ----------------------------------------------------------------------- slots

void MainWindow::onAvatarClicked()
{
    PictureDialog dialog(&avatarStore_, MeeruPaint::initialsFor(profile_.displayName),
                         Presence::color(presence_), this);
    dialog.exec();
    if (dialog.wasChanged()) {
        refreshAvatar();
        publishPictures();
    }
}

void MainWindow::onBannerClicked()
{
    PictureDialog dialog(&bannerStore_, MeeruPaint::initialsFor(profile_.displayName),
                         Presence::color(presence_), this);
    dialog.exec();
    if (dialog.wasChanged()) {
        refreshBanner();
        publishPictures();
    }
}

void MainWindow::onPresenceClicked()
{
    QMenu menu(this);
    for (int state = Presence::Available; state <= Presence::Invisible; ++state) {
        QAction *action = menu.addAction(QIcon(MeeruPaint::presenceBadge(Presence::color(state), 16, 9)),
                                         Presence::label(state));
        action->setData(state);
        if (state == presence_) {
            // The state you are already in is shown in bold rather than with a
            // tick, which would fight the dot for the same slot.
            QFont current = menu.font();
            current.setBold(true);
            action->setFont(current);
        }
    }
    connect(&menu, SIGNAL(triggered(QAction*)), this, SLOT(onPresenceAction(QAction*)));
    menu.exec(stateLabel_->mapToGlobal(QPoint(0, stateLabel_->height() + 2)));
}

void MainWindow::onPresenceAction(QAction *action)
{
    if (!action)
        return;
    applyPresence(action->data().toInt(), true);
}

void MainWindow::onStatusClicked()
{
    statusEdit_->setText(statusText_);
    statusStack_->setCurrentIndex(1);
    statusEdit_->setFocus();
    statusEdit_->selectAll();
}

void MainWindow::commitStatus()
{
    if (committingStatus_ || statusStack_->currentIndex() != 1)
        return;

    committingStatus_ = true;
    statusText_ = statusEdit_->text().trimmed();
    statusStack_->setCurrentIndex(0);

    AppSettings values = settings_.load();
    values.activeIdentityId = profile_.identityId;
    values.displayName = profile_.displayName;
    values.presence = Presence::key(presence_);
    values.statusText = statusText_;
    settings_.save(values, 0);

    refreshProfile();
    publishProfile();
    committingStatus_ = false;
}

void MainWindow::onTabChanged(int tab)
{
    currentTab_ = tab;
    refreshList();
}

void MainWindow::onSearchChanged(const QString &text)
{
    filter_ = text.trimmed();
    refreshList();
}

void MainWindow::onAddClicked()
{
    if (currentTab_ == MessagesTab)
        addMessagesEntry();
    else if (currentTab_ == ServersTab)
        addServersEntry();
    else
        addContactsEntry();
}

void MainWindow::addContactsEntry()
{
    // The dialog needs the identity key to sign an invite code, and the
    // addresses this device answers on to put inside it.
    IdentityStore store(paths_);
    IdentityMaterial material;
    QString unlockError;
    if (!store.unlock(profile_.identityId, &material, &unlockError)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Add a contact"),
                                 QString::fromLatin1("Meeru could not unlock this identity, so it cannot "
                                                     "sign an invite code.\n\n") + unlockError);
        return;
    }

    const AppSettings current = settings_.load();
    AddContactDialog dialog(profile_, material,
                            node_ ? node_->localEndpoints() : QStringList(),
                            node_ ? node_->nearbyPeers() : QList<NearbyPeer>(),
                            current.inviteLifetimeSeconds, this);
    const int result = dialog.exec();
    material.clear();

    // Remember the lifetime even when the request itself is cancelled.
    if (dialog.inviteLifetime() != current.inviteLifetimeSeconds) {
        AppSettings values = settings_.load();
        values.inviteLifetimeSeconds = dialog.inviteLifetime();
        settings_.save(values, 0);
    }

    if (result != QDialog::Accepted)
        return;

    Roster::Contact contact;
    contact.id = dialog.contactId();
    contact.displayName = dialog.contactName();
    contact.endpointHint = dialog.endpointHint();
    contact.state = Roster::ContactPendingOutgoing;
    contact.addedAtUtc = QDateTime::currentDateTimeUtc();
    contact.presence = Presence::key(Presence::Invisible);

    QString error;
    if (!roster_.addContact(contact, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Add a contact"), error);
        return;
    }

    if (node_ && node_->isRunning()) {
        node_->setContacts(roster_.contacts());
        node_->requestContact(contact.id, contact.endpointHint,
                              QString::fromLatin1("%1 would like to add you on Meeru.")
                                  .arg(profile_.displayName), 0);
    } else {
        // The contact is saved either way, but saying "sent" when nothing left
        // this computer would be a lie.
        MeeruDialog::showMessage(this, QString::fromLatin1("Add a contact"),
                                 QString::fromLatin1("The contact was saved, but Meeru is not connected to "
                                                     "any network right now, so the request has not gone "
                                                     "anywhere yet. It will be delivered once a connection "
                                                     "is possible."));
    }

    currentTab_ = ContactsTab;
    if (tabs_->button(ContactsTab))
        tabs_->button(ContactsTab)->setChecked(true);
    refreshList();
    refreshNews();
}

void MainWindow::addMessagesEntry()
{
    const QList<Roster::Contact> accepted = roster_.acceptedContacts();
    NewConversationDialog dialog(accepted, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList members = dialog.selectedIds();
    if (members.isEmpty())
        return;

    const Roster::Conversation existing = roster_.conversationWithMembers(members);
    if (!existing.id.isEmpty()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("New conversation"),
                                 QString::fromLatin1("That conversation already exists in your list."));
        currentTab_ = MessagesTab;
        if (tabs_->button(MessagesTab))
            tabs_->button(MessagesTab)->setChecked(true);
        refreshList();
        return;
    }

    Roster::Conversation conversation;
    conversation.id = Roster::newLocalId();
    conversation.members = members;
    conversation.group = members.size() > 1;
    conversation.title = dialog.groupTitle();
    conversation.createdAtUtc = QDateTime::currentDateTimeUtc();
    conversation.updatedAtUtc = conversation.createdAtUtc;
    conversation.preview = conversation.group
        ? QString::fromLatin1("Group created on this device")
        : QString::fromLatin1("No messages yet");

    QString error;
    if (!roster_.addConversation(conversation, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("New conversation"), error);
        return;
    }

    currentTab_ = MessagesTab;
    if (tabs_->button(MessagesTab))
        tabs_->button(MessagesTab)->setChecked(true);
    refreshList();
    refreshNews();
}

void MainWindow::addServersEntry()
{
    ServerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!roster_.addServer(dialog.server(), &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Servers"), error);
        return;
    }

    currentTab_ = ServersTab;
    if (tabs_->button(ServersTab))
        tabs_->button(ServersTab)->setChecked(true);
    refreshList();
    refreshNews();

    if (!dialog.isJoining()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Server created"),
                                 QString::fromLatin1("Share this invite code so others can join:\n\nmeeru:server:")
                                     + dialog.server().id);
    }
}

void MainWindow::notYetAvailable(const QString &what)
{
    MeeruDialog::showMessage(this, what,
                             what + QString::fromLatin1(" open in their own window, which is the next part of "
                                                        "Meeru being built. Everything you create here is already "
                                                        "stored in your profile and will be waiting."));
}

void MainWindow::openContact(const QString &contactId)
{
    const Roster::Contact contact = roster_.contact(contactId);
    if (contact.id.isEmpty())
        return;

    if (contact.state != Roster::ContactAccepted) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Contact request"),
                                 QString::fromLatin1("This contact request is still pending, so there is no "
                                                     "conversation yet."));
        return;
    }

    QStringList members;
    members.append(contactId);
    Roster::Conversation conversation = roster_.conversationWithMembers(members);
    if (conversation.id.isEmpty()) {
        conversation.id = Roster::newLocalId();
        conversation.members = members;
        conversation.group = false;
        conversation.createdAtUtc = QDateTime::currentDateTimeUtc();
        conversation.updatedAtUtc = conversation.createdAtUtc;
        conversation.preview = QString::fromLatin1("No messages yet");
        QString error;
        if (!roster_.addConversation(conversation, &error)) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Conversation"), error);
            return;
        }
        refreshNews();
    }

    currentTab_ = MessagesTab;
    if (tabs_->button(MessagesTab))
        tabs_->button(MessagesTab)->setChecked(true);
    refreshList();
    notYetAvailable(QString::fromLatin1("Conversations"));
}

void MainWindow::openConversation(const QString &conversationId)
{
    Q_UNUSED(conversationId);
    notYetAvailable(QString::fromLatin1("Conversations"));
}

void MainWindow::openServer(const QString &serverId)
{
    Q_UNUSED(serverId);
    notYetAvailable(QString::fromLatin1("Servers"));
}

void MainWindow::onItemActivated(QListWidgetItem *item)
{
    if (!item || item->data(KindRole).toInt() != RowEntry)
        return;

    const QString id = item->data(IdRole).toString();
    if (currentTab_ == MessagesTab)
        openConversation(id);
    else if (currentTab_ == ServersTab)
        openServer(id);
    else
        openContact(id);
}

void MainWindow::onContextMenu(const QPoint &position)
{
    hideContactCard();

    QListWidgetItem *item = list_->itemAt(position);
    if (!item || item->data(KindRole).toInt() != RowEntry)
        return;

    const QString id = item->data(IdRole).toString();
    const bool favorite = item->data(FavoriteRole).toBool();
    QString error;

    QMenu menu(this);
    if (currentTab_ == ContactsTab) {
        const Roster::Contact contact = roster_.contact(id);
        QAction *open = menu.addAction(QString::fromLatin1("Open conversation"));
        open->setEnabled(contact.state == Roster::ContactAccepted);
        QAction *accept = 0;
        QAction *cancel = 0;
        if (contact.state == Roster::ContactPendingIncoming)
            accept = menu.addAction(QString::fromLatin1("Accept request"));
        if (contact.state == Roster::ContactPendingOutgoing)
            cancel = menu.addAction(QString::fromLatin1("Cancel request"));
        QAction *fav = menu.addAction(favorite ? QString::fromLatin1("Remove from favorites")
                                               : QString::fromLatin1("Add to favorites"));
        fav->setEnabled(contact.state == Roster::ContactAccepted);
        QAction *copy = menu.addAction(QString::fromLatin1("Copy Meeru ID"));
        menu.addSeparator();
        QAction *remove = menu.addAction(QString::fromLatin1("Remove contact"));

        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(position));
        if (!chosen)
            return;
        if (chosen == open) {
            openContact(id);
        } else if (accept && chosen == accept) {
            if (roster_.setContactState(id, Roster::ContactAccepted, &error) && node_) {
                node_->setContacts(roster_.contacts());
                node_->acceptContact(id);
            }
        } else if (cancel && chosen == cancel) {
            if (MeeruDialog::confirm(this, QString::fromLatin1("Cancel request"),
                                     QString::fromLatin1("Withdraw your request to this contact?"),
                                     QString::fromLatin1("Withdraw"))) {
                roster_.removeContact(id, &error);
                if (node_)
                    node_->forgetPeer(id);
            }
        } else if (chosen == fav) {
            roster_.setContactFavorite(id, !favorite, &error);
        } else if (chosen == copy) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Meeru ID"),
                                     QString::fromLatin1("meeru:") + id);
        } else if (chosen == remove) {
            if (MeeruDialog::confirm(this, QString::fromLatin1("Remove contact"),
                                     QString::fromLatin1("Remove this contact and any conversation that only "
                                                         "included them?"),
                                     QString::fromLatin1("Remove"))) {
                roster_.removeContact(id, &error);
                tileCache_.remove(id);
                if (node_)
                    node_->forgetPeer(id);
            }
        }
    } else if (currentTab_ == MessagesTab) {
        QAction *open = menu.addAction(QString::fromLatin1("Open"));
        QAction *rename = menu.addAction(QString::fromLatin1("Rename"));
        QAction *fav = menu.addAction(favorite ? QString::fromLatin1("Remove from favorites")
                                               : QString::fromLatin1("Add to favorites"));
        menu.addSeparator();
        QAction *leave = menu.addAction(QString::fromLatin1("Leave conversation"));

        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(position));
        if (!chosen)
            return;
        if (chosen == open) {
            openConversation(id);
        } else if (chosen == rename) {
            QString title = item->data(TitleRole).toString();
            if (promptText(this, QString::fromLatin1("Rename conversation"),
                           QString::fromLatin1("Name for this conversation:"), &title)) {
                roster_.renameConversation(id, title, &error);
            }
        } else if (chosen == fav) {
            roster_.setConversationFavorite(id, !favorite, &error);
        } else if (chosen == leave) {
            if (MeeruDialog::confirm(this, QString::fromLatin1("Leave conversation"),
                                     QString::fromLatin1("Remove this conversation from your device?"),
                                     QString::fromLatin1("Leave"))) {
                roster_.removeConversation(id, &error);
            }
        }
    } else {
        QAction *open = menu.addAction(QString::fromLatin1("Open"));
        QAction *invite = menu.addAction(QString::fromLatin1("Copy invite code"));
        QAction *fav = menu.addAction(favorite ? QString::fromLatin1("Remove from favorites")
                                               : QString::fromLatin1("Add to favorites"));
        menu.addSeparator();
        QAction *leave = menu.addAction(QString::fromLatin1("Leave server"));

        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(position));
        if (!chosen)
            return;
        if (chosen == open) {
            openServer(id);
        } else if (chosen == invite) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Invite code"),
                                     QString::fromLatin1("meeru:server:") + id);
        } else if (chosen == fav) {
            roster_.setServerFavorite(id, !favorite, &error);
        } else if (chosen == leave) {
            if (MeeruDialog::confirm(this, QString::fromLatin1("Leave server"),
                                     QString::fromLatin1("Remove this server from your list?"),
                                     QString::fromLatin1("Leave"))) {
                roster_.removeServer(id, &error);
            }
        }
    }

    if (!error.isEmpty())
        MeeruDialog::showMessage(this, QString::fromLatin1("Meeru"), error);

    refreshList();
    refreshNews();
}

void MainWindow::onSettings()
{
    const AppSettings current = settings_.load();
    SettingsDialog dialog(profile_.displayName, profile_.identityId, paths_.root(),
                          current.startWithWindows, current.rendezvousHosts,
                          node_ ? node_->reachability() : QString(),
                          node_ ? node_->diagnostics() : QString(),
                          current.useUpnp, current.useDht, current.dhtFallback, current.firewallProfiles, current.listenPort,
                          current.publicAddress, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newName = dialog.displayName();
    if (!newName.isEmpty() && newName != profile_.displayName) {
        IdentityStore store(paths_);
        LocalProfile updated;
        QString error;
        if (store.updateActive(newName, Presence::key(presence_), &updated, &error)) {
            profile_ = updated;
        } else {
            MeeruDialog::showMessage(this, QString::fromLatin1("Settings"),
                                     QString::fromLatin1("Meeru could not rename your identity.\n\n") + error);
        }
    }

    AppSettings values = settings_.load();
    values.activeIdentityId = profile_.identityId;
    values.displayName = profile_.displayName;
    values.presence = Presence::key(presence_);
    values.statusText = statusText_;
    values.startWithWindows = dialog.startWithWindows();
    values.rendezvousHosts = dialog.rendezvousHosts();
    values.useUpnp = dialog.useUpnp();
    values.useDht = dialog.useDht();
    values.dhtFallback = dialog.dhtFallback();
    values.firewallProfiles = dialog.firewallProfiles();
    values.listenPort = dialog.listenPort();
    values.publicAddress = dialog.publicAddress();
    settings_.save(values, 0);

    if (node_) {
        node_->setRendezvousHosts(values.rendezvousHosts);
        node_->setNetworkPreferences(values.listenPort, values.publicAddress, values.useUpnp);
        node_->setDhtEnabled(values.useDht);
        node_->setDhtFallbackAllowed(values.dhtFallback);
    }

    if (dialog.firewallRequested() && node_ && node_->isRunning()) {
        QString firewallError;
        if (FirewallHelper::installRules(node_->listenPort(), PeerNode::discoveryUdpPort(),
                                         values.firewallProfiles, &firewallError)) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Windows Firewall"),
                                     QString::fromLatin1("Meeru is now allowed through on: ")
                                         + values.firewallProfiles);
        } else {
            MeeruDialog::showMessage(this, QString::fromLatin1("Windows Firewall"),
                                     QString::fromLatin1("The rules were not added.\n\n") + firewallError);
        }
    }

    if (values.listenPort != current.listenPort) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Settings"),
                                 QString::fromLatin1("Meeru will listen on that port the next time it "
                                                     "starts. Until then it keeps the one it opened when "
                                                     "you signed in."));
    }

    QString startupError;
    if (!SettingsStore::setAutoStart(dialog.startWithWindows(), &startupError)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Settings"),
                                 QString::fromLatin1("Meeru could not change the Windows startup entry.\n\n")
                                     + startupError);
    }

    refreshProfile();
    refreshAvatar();
    refreshBanner();
    publishProfile();
}

// ------------------------------------------------------------------- network

void MainWindow::startNetwork()
{
    node_ = new PeerNode(paths_, this);

    connect(node_, SIGNAL(statusChanged(QString)), this, SLOT(onNetworkStatus(QString)));
    connect(node_, SIGNAL(peerConnected(QString)), this, SLOT(onPeerConnected(QString)));
    connect(node_, SIGNAL(peerDisconnected(QString)), this, SLOT(onPeerDisconnected(QString)));
    connect(node_, SIGNAL(trustRequestReceived(QString,QString,QString)),
            this, SLOT(onTrustRequest(QString,QString,QString)));
    connect(node_, SIGNAL(trustAccepted(QString)), this, SLOT(onTrustAccepted(QString)));
    connect(node_, SIGNAL(profileReceived(QString,QString,QString,QString)),
            this, SLOT(onPeerProfile(QString,QString,QString,QString)));
    connect(node_, SIGNAL(pictureReceived(QString,QString)), this, SLOT(onPeerPicture(QString,QString)));
    connect(node_, SIGNAL(dhtEngagedAutomatically()), this, SLOT(onDhtEngaged()));

    const AppSettings network = settings_.load();
    node_->setNetworkPreferences(network.listenPort, network.publicAddress, network.useUpnp);
    node_->setDhtEnabled(network.useDht);
    node_->setDhtFallbackAllowed(network.dhtFallback);
    node_->setRendezvousHosts(network.rendezvousHosts);

    // The private key has to be unsealed to prove this identity to peers.
    IdentityStore store(paths_);
    IdentityMaterial material;
    QString error;
    if (!store.unlock(profile_.identityId, &material, &error)) {
        networkStatus_ = QString::fromLatin1("Offline");
        refreshNews();
        MeeruDialog::showMessage(this, QString::fromLatin1("Network"),
                                 QString::fromLatin1("Meeru could not unlock this identity, so it cannot "
                                                     "prove who you are to other people. You can still use "
                                                     "the app, but nobody will be able to reach you.\n\n")
                                     + error);
        return;
    }

    const bool started = node_->start(profile_, material, &error);
    material.clear();

    if (!started) {
        networkStatus_ = QString::fromLatin1("Offline");
        refreshNews();
        MeeruDialog::showMessage(this, QString::fromLatin1("Network"),
                                 QString::fromLatin1("Meeru could not start listening for contacts.\n\n") + error);
        return;
    }

    node_->setContacts(roster_.contacts());
    publishProfile();
    publishPictures();

    // Being findable worldwide is on by default because it is what makes a
    // pasted ID work at all across the internet. Turning it on quietly would
    // still be wrong: it is said out loud, once.
    if (network.useDht && !network.dhtNoticeShown) {
        AppSettings values = settings_.load();
        values.dhtNoticeShown = true;
        settings_.save(values, 0);

        MeeruDialog::showMessage(
            this, QString::fromLatin1("Being findable"),
            QString::fromLatin1("So that somebody can reach you with nothing but your Meeru ID, Meeru "
                                "publishes where you are in the public network BitTorrent uses to find "
                                "peers. The entry is signed with your key, so nobody can forge or alter "
                                "it.\n\nWhat it costs: your public key sits next to your current IP "
                                "address on machines run by strangers, so anyone who has your ID can see "
                                "when you are online and roughly where. Settings has a switch to turn "
                                "this off and stay on your own network only."));
    }

    checkFirewall();
}

void MainWindow::checkFirewall()
{
    if (!FirewallHelper::isSupported() || !node_ || !node_->isRunning())
        return;

    const AppSettings values = settings_.load();

    // Checked on every start, not once: a rule can be removed, or left behind
    // pointing at a port Meeru no longer uses, and either way the program goes
    // quiet with no explanation. If everything is in order this costs nothing
    // and says nothing.
    if (FirewallHelper::rulesPresent(node_->listenPort(), PeerNode::discoveryUdpPort()))
        return;

    if (!MeeruDialog::confirm(
            this, QString::fromLatin1("Let contacts reach you"),
            QString::fromLatin1("Windows blocks incoming connections unless a program is allowed through "
                                "the firewall, and the prompt it shows on first run is easy to miss. "
                                "Meeru can add the two rules it needs: one for contacts connecting to you, "
                                "one for finding people on your own network.\n\n"
                                "Windows will ask for administrator permission. The rules apply to Meeru "
                                "alone, on the network types chosen in Settings (currently %1).")
                .arg(values.firewallProfiles),
            QString::fromLatin1("Add the rules"))) {
        return;
    }

    QString error;
    if (!FirewallHelper::installRules(node_->listenPort(), PeerNode::discoveryUdpPort(),
                                      values.firewallProfiles, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Firewall"),
                                 QString::fromLatin1("The rules were not added.\n\n") + error
                                 + QString::fromLatin1("\n\nYou can add them yourself later from "
                                                       "Settings if contacts cannot reach you."));
    }
}

void MainWindow::onDhtEngaged()
{
    MeeruDialog::showMessage(
        this, QString::fromLatin1("Looking further afield"),
        QString::fromLatin1("Your contact could not be reached on this network, so Meeru has started "
                            "looking for them through the public distributed network instead.\n\n"
                            "That means your key and your current address are now published there, where "
                            "anyone holding your Meeru ID can see when you are online. You can switch this "
                            "off in Settings if you would rather stay on your own network only."));
}

void MainWindow::publishProfile()
{
    if (node_ && node_->isRunning())
        node_->setLocalProfile(profile_.displayName, Presence::key(presence_), statusText_);
}

void MainWindow::publishPictures()
{
    if (!node_ || !node_->isRunning())
        return;
    node_->setLocalPictures(avatarStore_.hasImage() ? avatarStore_.filePath() : QString(),
                            bannerStore_.hasImage() ? bannerStore_.filePath() : QString());
}

void MainWindow::onNetworkStatus(const QString &summary)
{
    networkStatus_ = summary;
    refreshNews();
}

void MainWindow::onPeerConnected(const QString &peerId)
{
    roster_.touchContact(peerId, 0);
    refreshList();
    refreshNews();
}

void MainWindow::onPeerDisconnected(const QString &peerId)
{
    Q_UNUSED(peerId);
    refreshList();
    refreshNews();
}

void MainWindow::onTrustRequest(const QString &peerId, const QString &displayName, const QString &message)
{
    QString error;

    if (roster_.hasContact(peerId)) {
        const Roster::Contact existing = roster_.contact(peerId);
        if (existing.state == Roster::ContactPendingOutgoing) {
            // Both sides asked each other: that is a match, so it is settled.
            roster_.setContactState(peerId, Roster::ContactAccepted, &error);
            node_->acceptContact(peerId);
            refreshList();
            refreshNews();
            return;
        }
        if (existing.state == Roster::ContactAccepted)
            return;
    } else {
        Roster::Contact contact;
        contact.id = peerId;
        contact.displayName = displayName;
        contact.statusText = message;
        contact.state = Roster::ContactPendingIncoming;
        contact.addedAtUtc = QDateTime::currentDateTimeUtc();
        contact.presence = Presence::key(Presence::Invisible);
        roster_.addContact(contact, &error);
    }

    node_->setContacts(roster_.contacts());
    refreshList();
    refreshNews();
}

void MainWindow::onTrustAccepted(const QString &peerId)
{
    QString error;
    if (roster_.hasContact(peerId))
        roster_.setContactState(peerId, Roster::ContactAccepted, &error);
    node_->setContacts(roster_.contacts());
    refreshList();
    refreshNews();
}

void MainWindow::onPeerProfile(const QString &peerId, const QString &displayName,
                               const QString &presence, const QString &statusText)
{
    if (!roster_.hasContact(peerId))
        return;
    roster_.updateContactProfile(peerId, displayName, presence, statusText, 0);
    refreshList();
}

void MainWindow::onPeerPicture(const QString &peerId, const QString &kind)
{
    Q_UNUSED(kind);
    tileCache_.remove(peerId);
    refreshList();
}

QPixmap MainWindow::contactTile(const Roster::Contact &contact) const
{
    QHash<QString, QPixmap>::const_iterator cached = tileCache_.constFind(contact.id);
    if (cached != tileCache_.constEnd())
        return cached.value();

    const QString directory = ImageStore::peerDirectory(paths_, profile_.identityId, contact.id);
    ImageStore store(directory, QString::fromLatin1("avatar"));

    QPixmap tile;
    if (store.hasImage()) {
        // The list shows a still frame even for animated pictures: dozens of
        // running animations is not what this machine class wants.
        QImageReader reader(store.filePath());
        reader.setAutoTransform(true);
        const QImage frame = reader.read();
        if (!frame.isNull()) {
            QPixmap source = QPixmap::fromImage(frame);
            if (store.isAnimated() && store.cropRect().isValid()) {
                const QRect area = store.cropRect().intersected(QRect(QPoint(0, 0), source.size()));
                if (area.isValid())
                    source = source.copy(area);
            }
            tile = MeeruPaint::roundedFromPixmap(source, QSize(29, 29), 6);
        }
    }

    tileCache_.insert(contact.id, tile);
    return tile;
}

// --------------------------------------------------------------- hover card

void MainWindow::onHoverItem(QListWidgetItem *item)
{
    if (!item || item->data(KindRole).toInt() != RowEntry || currentTab_ != ContactsTab) {
        hideContactCard();
        return;
    }

    const QString id = item->data(IdRole).toString();
    if (id == hoverContactId_ && card_->isVisible())
        return;

    hoverContactId_ = id;
    card_->hide();
    hoverTimer_->start();
}

void MainWindow::onHoverTimeout()
{
    if (hoverContactId_.isEmpty() || currentTab_ != ContactsTab)
        return;

    const Roster::Contact contact = roster_.contact(hoverContactId_);
    if (contact.id.isEmpty())
        return;

    card_->showFor(contact, paths_, profile_.identityId,
                   node_ && node_->isOnline(contact.id), QCursor::pos());
}

void MainWindow::hideContactCard()
{
    hoverContactId_.clear();
    if (hoverTimer_)
        hoverTimer_->stop();
    if (card_)
        card_->hideCard();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (list_ && watched == list_->viewport()) {
        if (event->type() == QEvent::Leave || event->type() == QEvent::Wheel
            || event->type() == QEvent::MouseButtonPress) {
            hideContactCard();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
