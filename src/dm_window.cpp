#include "dm_window.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "avatar.h"
#include "meeru_paint.h"
#include "meeru_style.h"
#include "meeru_window.h"
#include "peer_node.h"
#include "presence.h"

namespace {

const int kWindowWidth = 385;
const int kWindowHeight = 520;
const int kHeaderHeight = 72;
const int kDockGap = 6;
const int kTypingIdleMs = 2500;

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

}

DmWindow::DmWindow(const LocalProfile &profile,
                   const Roster::Contact &contact,
                   const MeeruPaths &paths,
                   MessageStore *messages,
                   PeerNode *node,
                   QWidget *anchor,
                   QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), contact_(contact), paths_(paths), messages_(messages),
      node_(node), anchor_(anchor), pinned_(true), online_(false),
      titleBar_(0), header_(0), avatar_(0), nameLabel_(0), presenceDot_(0),
      stateLabel_(0), statusLabel_(0), history_(0), typingLabel_(0),
      compose_(0), footerLabel_(0), typingTimer_(0)
{
    conversationId_ = PeerNode::directConversationId(profile_.identityId, contact_.id);

    buildUi();
    setContact(contact_);
    loadHistory();
    followAnchor();
}

void DmWindow::buildUi()
{
    setWindowTitle(QString::fromLatin1("Chat with ") + contact_.bestName());
    setStyleSheet(MeeruStyle::sheet());
    setFixedWidth(kWindowWidth);
    resize(kWindowWidth, kWindowHeight);
    setMinimumHeight(360);

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

    titleBar_ = new MeeruTitleBar(QString::fromLatin1("Chat with ") + contact_.bestName(),
                                  true, false, root);
    titleBar_->addPinButton(true);
    connect(titleBar_, SIGNAL(pinToggled(bool)), this, SLOT(onPinToggled(bool)));
    layout->addWidget(titleBar_);

    // --- the person, drawn exactly like the header of the main window
    header_ = new BannerFrame(root);
    header_->setFixedHeight(kHeaderHeight);
    header_->setCursor(Qt::ArrowCursor);
    header_->setToolTip(QString());

    avatar_ = new AvatarFrame(header_);
    avatar_->setTileSize(46);
    const int inset = avatar_->pictureInset();

    QHBoxLayout *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(12 - inset, 0, 12, 0);
    headerLayout->setSpacing(qMax(0, 10 - inset));
    headerLayout->addWidget(avatar_, 0, Qt::AlignVCenter);

    QWidget *column = new QWidget(header_);
    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);

    nameLabel_ = new QLabel(column);
    nameLabel_->setObjectName(QString::fromLatin1("profileName"));
    nameLabel_->setFixedHeight(20);

    presenceDot_ = new QLabel(column);
    presenceDot_->setFixedSize(10, 14);
    presenceDot_->setAlignment(Qt::AlignCenter);

    stateLabel_ = new QLabel(column);
    stateLabel_->setObjectName(QString::fromLatin1("profileState"));
    stateLabel_->setFixedHeight(14);

    QHBoxLayout *stateRow = new QHBoxLayout();
    stateRow->setContentsMargins(0, 0, 0, 0);
    stateRow->setSpacing(6);
    stateRow->addWidget(presenceDot_);
    stateRow->addWidget(stateLabel_, 1);

    statusLabel_ = new QLabel(column);
    statusLabel_->setObjectName(QString::fromLatin1("personalMessage"));
    statusLabel_->setFixedHeight(16);

    columnLayout->addWidget(nameLabel_);
    columnLayout->addSpacing(3);
    columnLayout->addLayout(stateRow);
    columnLayout->addSpacing(4);
    columnLayout->addWidget(statusLabel_);

    headerLayout->addWidget(column, 1, Qt::AlignVCenter);
    layout->addWidget(header_);

    // --- the conversation
    history_ = new QTextBrowser(root);
    history_->setObjectName(QString::fromLatin1("dmHistory"));
    history_->setOpenExternalLinks(true);
    history_->setFrameShape(QFrame::NoFrame);
    layout->addWidget(history_, 1);

    typingLabel_ = new QLabel(root);
    typingLabel_->setObjectName(QString::fromLatin1("dmTyping"));
    typingLabel_->setFixedHeight(20);
    typingLabel_->setContentsMargins(12, 0, 12, 0);
    typingLabel_->hide();
    layout->addWidget(typingLabel_);

    // --- writing
    QWidget *composeWrap = new QWidget(root);
    composeWrap->setObjectName(QString::fromLatin1("dmComposeWrap"));
    composeWrap->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *composeLayout = new QVBoxLayout(composeWrap);
    composeLayout->setContentsMargins(12, 8, 12, 8);
    composeLayout->setSpacing(6);

    compose_ = new QLineEdit(composeWrap);
    compose_->setObjectName(QString::fromLatin1("dmCompose"));
    compose_->setPlaceholderText(QString::fromLatin1("Write a message..."));
    compose_->setFixedHeight(30);
    compose_->setMaxLength(4000);
    composeLayout->addWidget(compose_);

    QHBoxLayout *footerRow = new QHBoxLayout();
    footerRow->setSpacing(8);
    footerLabel_ = new QLabel(composeWrap);
    footerLabel_->setObjectName(QString::fromLatin1("footerText"));
    QPushButton *send = new QPushButton(QString::fromLatin1("Send"), composeWrap);
    send->setObjectName(QString::fromLatin1("primaryButton"));
    send->setFixedHeight(26);
    footerRow->addWidget(footerLabel_, 1);
    footerRow->addWidget(send);
    composeLayout->addLayout(footerRow);

    layout->addWidget(composeWrap);

    connect(send, SIGNAL(clicked()), this, SLOT(onSend()));
    connect(compose_, SIGNAL(returnPressed()), this, SLOT(onSend()));
    connect(compose_, SIGNAL(textChanged(QString)), this, SLOT(onComposeChanged(QString)));

    typingTimer_ = new QTimer(this);
    typingTimer_->setSingleShot(true);
    typingTimer_->setInterval(kTypingIdleMs);
    connect(typingTimer_, SIGNAL(timeout()), this, SLOT(onTypingTimeout()));
}

void DmWindow::setContact(const Roster::Contact &contact)
{
    contact_ = contact;

    const QString directory = ImageStore::peerDirectory(paths_, profile_.identityId, contact_.id);
    ImageStore avatarStore(directory, QString::fromLatin1("avatar"));
    ImageStore bannerStore(directory, QString::fromLatin1("banner"));

    avatar_->setInitials(MeeruPaint::initialsFor(contact_.bestName()));
    avatar_->setImage(avatarStore);
    header_->setImage(bannerStore);

    nameLabel_->setText(escape(contact_.bestName()));
    statusLabel_->setText(contact_.statusText.trimmed().isEmpty()
                              ? QString::fromLatin1("No personal message")
                              : escape(contact_.statusText.trimmed()));

    titleBar_->setTitle(QString::fromLatin1("Chat with ") + contact_.bestName());
    setWindowTitle(titleBar_ ? QString::fromLatin1("Chat with ") + contact_.bestName() : windowTitle());
    setPeerOnline(online_);
}

void DmWindow::setPeerOnline(bool online)
{
    online_ = online;

    const int state = online ? Presence::stateFromKey(contact_.presence) : Presence::Invisible;
    avatar_->setPresenceColor(Presence::color(state), true);
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(Presence::color(state), 10, 9));
    stateLabel_->setText(online ? Presence::label(state) : QString::fromLatin1("Offline"));

    // Said plainly, because it changes what happens when you press Send.
    footerLabel_->setText(online
        ? QString::fromLatin1("Connected")
        : QString::fromLatin1("Offline: messages are kept and sent when they appear"));
}

void DmWindow::setPeerTyping(bool typing)
{
    if (!typing) {
        typingLabel_->hide();
        return;
    }
    typingLabel_->setText(contact_.bestName() + QString::fromLatin1(" is typing..."));
    typingLabel_->show();
}

void DmWindow::loadHistory()
{
    shown_ = messages_ ? messages_->history(conversationId_) : QList<Chat::Message>();
    renderHistory();
    if (messages_)
        messages_->markRead(conversationId_);
}

QString DmWindow::formatMessage(const Chat::Message &message, bool withHeader) const
{
    const QString who = message.isMine() ? profile_.displayName
                                         : (message.authorName.isEmpty() ? contact_.bestName()
                                                                         : message.authorName);
    QString out;
    if (message.kind == Chat::KindSystem) {
        out += QString::fromLatin1("<p class=\"system\">%1</p>").arg(escape(message.text));
        return out;
    }

    if (withHeader) {
        out += QString::fromLatin1("<p class=\"who\"><b>%1</b> <span class=\"time\">%2</span></p>")
                   .arg(escape(who))
                   .arg(message.sentAtUtc.toLocalTime().toString(QString::fromLatin1("h:mm AP")));
    }

    QString mark;
    if (message.isMine()) {
        // What happened to it, in the plainest words available.
        switch (message.delivery) {
        case Chat::DeliveryWaiting:      mark = QString::fromLatin1(" <span class=\"mark\">waiting</span>"); break;
        case Chat::DeliverySent:         mark = QString::fromLatin1(" <span class=\"mark\">sent</span>"); break;
        case Chat::DeliveryAcknowledged: mark = QString::fromLatin1(" <span class=\"mark\">delivered</span>"); break;
        default: break;
        }
    }

    out += QString::fromLatin1("<p class=\"body\">%1%2</p>").arg(escape(message.text)).arg(mark);
    return out;
}

void DmWindow::renderHistory()
{
    QString html = QString::fromLatin1(
        "<style>"
        "p { margin: 0 0 2px 0; }"
        ".who { margin-top: 8px; color: #DFB2F4; font-size: 11px; }"
        ".time { color: #9d8ba5; font-size: 10px; }"
        ".body { color: #F1E6F5; font-size: 12px; }"
        ".mark { color: #9d8ba5; font-size: 10px; }"
        ".system { color: #9d8ba5; font-size: 10px; font-style: italic; margin-top: 6px; }"
        ".day { color: #9d8ba5; font-size: 10px; }"
        "</style>");

    QString lastDay;
    QString lastAuthor;
    for (int i = 0; i < shown_.size(); ++i) {
        const Chat::Message &message = shown_.at(i);
        const QString day = message.sentAtUtc.toLocalTime().toString(QString::fromLatin1("d MMMM yyyy"));
        if (day != lastDay) {
            html += QString::fromLatin1("<p class=\"day\" align=\"center\">%1</p>").arg(escape(day));
            lastDay = day;
            lastAuthor.clear();
        }

        // Consecutive lines from the same person do not repeat the name.
        const QString author = message.isMine() ? QString() : message.authorId;
        html += formatMessage(message, author != lastAuthor);
        lastAuthor = author;
    }

    history_->setHtml(html);
    history_->verticalScrollBar()->setValue(history_->verticalScrollBar()->maximum());
}

void DmWindow::appendMessage(const Chat::Message &message)
{
    if (message.conversationId != conversationId_)
        return;
    for (int i = 0; i < shown_.size(); ++i) {
        if (shown_.at(i).id == message.id)
            return;
    }

    shown_.append(message);
    renderHistory();
    if (isVisible() && messages_)
        messages_->markRead(conversationId_);
}

void DmWindow::refreshDelivery()
{
    shown_ = messages_ ? messages_->history(conversationId_) : shown_;
    renderHistory();
}

void DmWindow::onSend()
{
    const QString text = compose_->text().trimmed();
    if (text.isEmpty() || !messages_)
        return;

    Chat::Message message;
    message.conversationId = conversationId_;
    message.text = text;
    message.kind = Chat::KindText;
    message.delivery = Chat::DeliveryWaiting;
    message.sentAtUtc = QDateTime::currentDateTimeUtc();
    message.authorName = profile_.displayName;

    const Chat::Message stored = messages_->append(message);
    if (!stored.isValid())
        return;

    compose_->clear();
    if (node_)
        node_->sendMessage(contact_.id, conversationId_, stored);

    shown_ = messages_->history(conversationId_);
    renderHistory();
}

void DmWindow::onComposeChanged(const QString &text)
{
    Q_UNUSED(text);
    typingTimer_->start();
}

void DmWindow::onTypingTimeout()
{
    // Nothing is sent about typing yet; the timer keeps the plumbing in place
    // for when it is, without pretending the other side is being told.
}

void DmWindow::onPinToggled(bool pinned)
{
    pinned_ = pinned;
    if (pinned_)
        followAnchor();
}

void DmWindow::followAnchor()
{
    if (!pinned_ || !anchor_ || !anchor_->isVisible())
        return;

    // Sits against the right edge of the main window and matches its height,
    // which is what "docked" means here.
    const QRect frame = anchor_->frameGeometry();
    move(frame.right() + kDockGap, frame.top());
    resize(kWindowWidth, qMax(minimumHeight(), frame.height()));
}

void DmWindow::closeEvent(QCloseEvent *event)
{
    emit closed(contact_.id);
    event->accept();
}
