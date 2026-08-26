#include "dm_window.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "avatar.h"
#include "meeru_dialogs.h"
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
      node_(node), anchor_(anchor), group_(false), voice_(0), voiceButton_(0),
      pinned_(true), online_(false),
      titleBar_(0), header_(0), avatar_(0), nameLabel_(0), presenceDot_(0),
      stateLabel_(0), statusLabel_(0), history_(0), typingLabel_(0),
      compose_(0), footerLabel_(0), sizeHint_(0), typingTimer_(0)
{
    conversationId_ = PeerNode::directConversationId(profile_.identityId, contact_.id);

    buildUi();
    setContact(contact_);
    loadHistory();
    followAnchor();
}

DmWindow::DmWindow(const LocalProfile &profile,
                   const Roster::Conversation &conversation,
                   const QList<Roster::Contact> &members,
                   const MeeruPaths &paths,
                   MessageStore *messages,
                   PeerNode *node,
                   QWidget *anchor,
                   QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      profile_(profile), members_(members), paths_(paths), messages_(messages),
      node_(node), anchor_(anchor), group_(true), voice_(0), voiceButton_(0),
      pinned_(true), online_(false),
      titleBar_(0), header_(0), avatar_(0), nameLabel_(0), presenceDot_(0),
      stateLabel_(0), statusLabel_(0), history_(0), typingLabel_(0),
      compose_(0), footerLabel_(0), sizeHint_(0), typingTimer_(0)
{
    conversationId_ = conversation.id;
    groupTitle_ = conversation.title.trimmed();
    if (groupTitle_.isEmpty()) {
        QStringList names;
        for (int i = 0; i < members_.size() && names.size() < 3; ++i)
            names.append(members_.at(i).bestName());
        groupTitle_ = names.join(QString::fromLatin1(", "));
    }

    buildUi();

    nameLabel_->setText(escape(groupTitle_));
    stateLabel_->setText(QString::fromLatin1("%1 members").arg(members_.size() + 1));
    statusLabel_->setText(QString::fromLatin1("Group conversation"));
    avatar_->setInitials(MeeruPaint::initialsFor(groupTitle_));
    avatar_->setPresenceColor(Presence::color(Presence::Available), false);
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(Presence::color(Presence::Available), 10, 9));
    titleBar_->setTitle(groupTitle_);
    setWindowTitle(groupTitle_);
    footerLabel_->setText(QString::fromLatin1("Members catch up from whoever has the most"));

    loadHistory();
    followAnchor();
}

void DmWindow::setMemberOnline(const QString &peerId, bool online)
{
    Q_UNUSED(peerId);
    Q_UNUSED(online);
    if (!group_ || !node_)
        return;

    int connected = 0;
    for (int i = 0; i < members_.size(); ++i) {
        if (node_->isOnline(members_.at(i).id))
            ++connected;
    }
    stateLabel_->setText(QString::fromLatin1("%1 of %2 members online")
                             .arg(connected).arg(members_.size()));
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

    // Calling lives in the header, beside the person being called.
    QPushButton *call = new QPushButton(QString::fromUtf8("\342\230\216"), header_);
    call->setObjectName(QString::fromLatin1("glassButton"));
    call->setFixedSize(26, 26);
    call->setToolTip(QString::fromLatin1("Call"));

    QPushButton *videoCall = new QPushButton(QString::fromUtf8("\342\226\266"), header_);
    videoCall->setObjectName(QString::fromLatin1("glassButton"));
    videoCall->setFixedSize(26, 26);
    videoCall->setToolTip(QString::fromLatin1("Call and share a screen"));

    headerLayout->addWidget(column, 1, Qt::AlignVCenter);
    headerLayout->addWidget(call, 0, Qt::AlignVCenter);
    headerLayout->addWidget(videoCall, 0, Qt::AlignVCenter);
    layout->addWidget(header_);

    connect(call, SIGNAL(clicked()), this, SLOT(onCall()));
    connect(videoCall, SIGNAL(clicked()), this, SLOT(onVideoCall()));

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

    // The tools, in the order the mockup shows them.
    QHBoxLayout *toolRow = new QHBoxLayout();
    toolRow->setSpacing(6);

    QPushButton *emoji = new QPushButton(QString::fromUtf8("\342\230\272"), composeWrap);
    emoji->setObjectName(QString::fromLatin1("glassButton"));
    emoji->setFixedSize(26, 26);
    emoji->setToolTip(QString::fromLatin1("Emoji"));

    QPushButton *attach = new QPushButton(QString::fromUtf8("\360\237\223\216"), composeWrap);
    attach->setObjectName(QString::fromLatin1("glassButton"));
    attach->setFixedSize(26, 26);
    attach->setToolTip(QString::fromLatin1("Attach a file, picture or video"));

    voiceButton_ = new QPushButton(QString::fromUtf8("\342\231\252"), composeWrap);
    voiceButton_->setObjectName(QString::fromLatin1("glassButton"));
    voiceButton_->setFixedSize(26, 26);
    voiceButton_->setToolTip(VoiceRecorder::isSupported()
        ? QString::fromLatin1("Record a voice note")
        : QString::fromLatin1("This build has no audio support"));
    voiceButton_->setEnabled(VoiceRecorder::isSupported());

    QPushButton *poll = new QPushButton(QString::fromUtf8("\342\226\244"), composeWrap);
    poll->setObjectName(QString::fromLatin1("glassButton"));
    poll->setFixedSize(26, 26);
    poll->setToolTip(QString::fromLatin1("Poll"));

    sizeHint_ = new QLabel(composeWrap);
    sizeHint_->setObjectName(QString::fromLatin1("footerText"));
    sizeHint_->setText(QString::fromLatin1("Files are offered, not pushed"));

    toolRow->addWidget(emoji);
    toolRow->addWidget(attach);
    toolRow->addWidget(voiceButton_);
    toolRow->addWidget(poll);
    toolRow->addWidget(sizeHint_, 1);
    composeLayout->addLayout(toolRow);

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

    connect(emoji, SIGNAL(clicked()), this, SLOT(onEmoji()));
    connect(attach, SIGNAL(clicked()), this, SLOT(onAttach()));
    connect(poll, SIGNAL(clicked()), this, SLOT(onPoll()));
    connect(voiceButton_, SIGNAL(clicked()), this, SLOT(onVoice()));

    layout->addWidget(composeWrap);

    connect(send, SIGNAL(clicked()), this, SLOT(onSend()));
    connect(compose_, SIGNAL(returnPressed()), this, SLOT(onSend()));
    connect(compose_, SIGNAL(textChanged(QString)), this, SLOT(onComposeChanged(QString)));
    connect(history_, SIGNAL(anchorClicked(QUrl)), this, SLOT(onHistoryLink(QUrl)));
    history_->setOpenLinks(false);   // links inside the history are commands, not the web

    if (messages_) {
        connect(messages_, SIGNAL(transferChanged(QString,QString)),
                this, SLOT(onTransferChanged(QString,QString)));
    }

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

    if (message.kind == Chat::KindFile && message.attachment.isValid()) {
        const Chat::Attachment &file = message.attachment;
        const QString size = file.fileSize > 1024 * 1024
            ? QString::fromLatin1("%1 MB").arg(file.fileSize / (1024.0 * 1024.0), 0, 'f', 1)
            : QString::fromLatin1("%1 KB").arg(qMax(qint64(1), file.fileSize / 1024));

        QString state;
        switch (file.transfer) {
        case Chat::TransferOffered:
            // Nothing has been downloaded: the choice is the receiver's.
            state = message.isMine()
                ? QString::fromLatin1("<span class=\"mark\">offered</span>")
                : QString::fromLatin1("<a href=\"meeru:receive/%1\">Receive</a>").arg(message.id);
            break;
        case Chat::TransferRunning: {
            const int percent = file.fileSize > 0
                ? static_cast<int>((file.received * 100) / file.fileSize) : 0;
            state = QString::fromLatin1("<span class=\"mark\">receiving %1%</span>").arg(percent);
            break;
        }
        case Chat::TransferComplete:
            state = QString::fromLatin1("<a href=\"meeru:open/%1\">Open</a>").arg(message.id);
            break;
        default:
            state = QString::fromLatin1("<span class=\"mark\">failed</span>");
            break;
        }

        out += QString::fromLatin1("<p class=\"body\">%1 <span class=\"mark\">%2</span> %3%4</p>")
                   .arg(escape(file.fileName)).arg(size).arg(state).arg(mark);
        return out;
    }

    if (message.kind == Chat::KindPoll && message.poll.isValid()) {
        out += QString::fromLatin1("<p class=\"body\"><b>%1</b></p>").arg(escape(message.poll.question));
        int total = 0;
        for (int i = 0; i < message.poll.options.size(); ++i)
            total += message.poll.options.at(i).votes;

        for (int i = 0; i < message.poll.options.size(); ++i) {
            const Chat::PollOption &option = message.poll.options.at(i);
            const int share = total > 0 ? (option.votes * 100) / total : 0;
            const bool mine = message.poll.myVote == i;
            const QString label = QString::fromLatin1("%1 &nbsp;<span class=\"mark\">%2%  (%3)</span>")
                                      .arg(escape(option.text)).arg(share).arg(option.votes);
            if (message.poll.isClosed() || mine) {
                out += QString::fromLatin1("<p class=\"body\">%1%2</p>")
                           .arg(mine ? QString::fromLatin1("&#9679; ") : QString::fromLatin1("&#9675; "))
                           .arg(label);
            } else {
                out += QString::fromLatin1("<p class=\"body\">&#9675; <a href=\"meeru:vote/%1/%2\">%3</a></p>")
                           .arg(message.id).arg(i).arg(label);
            }
        }
        if (message.poll.closesAtUtc.isValid()) {
            out += QString::fromLatin1("<p class=\"mark\">%1</p>")
                       .arg(message.poll.isClosed()
                                ? QString::fromLatin1("Closed")
                                : QString::fromLatin1("Closes ")
                                      + message.poll.closesAtUtc.toLocalTime()
                                            .toString(QString::fromLatin1("d MMM h:mm AP")));
        }
        return out;
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
    if (node_) {
        if (group_) {
            for (int i = 0; i < members_.size(); ++i)
                node_->sendMessage(members_.at(i).id, conversationId_, stored);
        } else {
            node_->sendMessage(contact_.id, conversationId_, stored);
        }
    }

    shown_ = messages_->history(conversationId_);
    renderHistory();
}

void DmWindow::sendAttachment(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !messages_)
        return;
    if (info.size() > TransferManager::maximumFileSize()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Attach"),
                                 QString::fromLatin1("That file is larger than Meeru will carry."));
        return;
    }

    Chat::Message message;
    message.conversationId = conversationId_;
    message.kind = Chat::KindFile;
    message.delivery = Chat::DeliveryWaiting;
    message.sentAtUtc = QDateTime::currentDateTimeUtc();
    message.authorName = profile_.displayName;
    message.text = info.fileName();
    message.attachment.fileId = Chat::newMessageId();
    message.attachment.fileName = info.fileName();
    message.attachment.fileSize = info.size();
    message.attachment.media = Chat::Attachment::mediaForName(info.fileName());
    message.attachment.transfer = Chat::TransferOffered;
    message.attachment.localPath = path;   // the original stays where it is

    const Chat::Message stored = messages_->append(message);
    if (!stored.isValid())
        return;

    // The file itself does not move yet: only the offer does.
    if (node_ && node_->transfers())
        node_->transfers()->registerOutgoing(stored.attachment.fileId, path);
    if (node_) {
        if (group_) {
            for (int i = 0; i < members_.size(); ++i)
                node_->sendMessage(members_.at(i).id, conversationId_, stored);
        } else {
            node_->sendMessage(contact_.id, conversationId_, stored);
        }
    }

    shown_ = messages_->history(conversationId_);
    renderHistory();
}

void DmWindow::onAttach()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromLatin1("Send a file"), QDir::homePath(),
        QString::fromLatin1("All files (*)"));
    if (!path.isEmpty())
        sendAttachment(path);
}

void DmWindow::onEmoji()
{
    EmojiDialog dialog(paths_, profile_.identityId, this);
    if (dialog.exec() != QDialog::Accepted || dialog.chosen().isEmpty())
        return;
    compose_->insert(QLatin1Char(':') + dialog.chosen() + QLatin1Char(':'));
    compose_->setFocus();
}

void DmWindow::onPoll()
{
    PollDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Chat::Poll poll = dialog.poll();
    if (!poll.isValid() || !messages_)
        return;

    Chat::Message message;
    message.conversationId = conversationId_;
    message.kind = Chat::KindPoll;
    message.delivery = Chat::DeliveryWaiting;
    message.sentAtUtc = QDateTime::currentDateTimeUtc();
    message.authorName = profile_.displayName;
    message.text = poll.question;
    message.poll = poll;

    const Chat::Message stored = messages_->append(message);
    if (!stored.isValid())
        return;
    if (node_) {
        if (group_) {
            for (int i = 0; i < members_.size(); ++i)
                node_->sendMessage(members_.at(i).id, conversationId_, stored);
        } else {
            node_->sendMessage(contact_.id, conversationId_, stored);
        }
    }

    shown_ = messages_->history(conversationId_);
    renderHistory();
}

void DmWindow::onHistoryLink(const QUrl &url)
{
    // Links inside the conversation are commands of our own, never the web.
    const QString path = url.toString();
    if (!path.startsWith(QLatin1String("meeru:")))
        return;

    const QStringList parts = path.mid(6).split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.isEmpty())
        return;

    if (parts.first() == QLatin1String("receive") && parts.size() == 2) {
        // In a group the file is asked of whoever offered it, not of everybody.
        const Chat::Message offer = messages_->message(conversationId_, parts.at(1));
        const QString from = group_ ? offer.authorId : contact_.id;
        if (from.isEmpty() || !node_ || !node_->receiveAttachment(from, conversationId_, parts.at(1))) {
            MeeruDialog::showMessage(this, QString::fromLatin1("Receive"),
                                     QString::fromLatin1("A file only travels while you are both "
                                                         "connected. Try again when they are online."));
        }
        return;
    }

    if (parts.first() == QLatin1String("open") && parts.size() == 2) {
        const Chat::Message message = messages_->message(conversationId_, parts.at(1));
        if (!message.attachment.localPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(message.attachment.localPath));
        return;
    }

    if (parts.first() == QLatin1String("vote") && parts.size() == 3) {
        messages_->setVote(conversationId_, parts.at(1), parts.at(2).toInt());
        refreshDelivery();
        return;
    }
}

void DmWindow::onCall()
{
    QStringList participants;
    if (group_) {
        for (int i = 0; i < members_.size(); ++i)
            participants.append(members_.at(i).id);
    } else {
        participants.append(contact_.id);
    }
    emit callRequested(conversationId_, participants,
                       group_ ? groupTitle_ : contact_.bestName(), false);
}

void DmWindow::onVideoCall()
{
    QStringList participants;
    if (group_) {
        for (int i = 0; i < members_.size(); ++i)
            participants.append(members_.at(i).id);
    } else {
        participants.append(contact_.id);
    }
    emit callRequested(conversationId_, participants,
                       group_ ? groupTitle_ : contact_.bestName(), true);
}

void DmWindow::onVoice()
{
    if (!voice_) {
        voice_ = new VoiceRecorder(this);
        connect(voice_, SIGNAL(levelChanged(int)), this, SLOT(onVoiceTick(int)));
    }

    if (voice_->isRecording()) {
        const QString finished = voice_->stop();
        voiceButton_->setText(QString::fromUtf8("\342\231\252"));
        sizeHint_->setText(QString::fromLatin1("Files are offered, not pushed"));
        if (!finished.isEmpty())
            sendAttachment(finished);
        return;
    }

    // Recorded into the attachments folder, then sent like any other file, so
    // it follows the same offer-and-accept rule as everything else.
    const QString directory = messages_ ? messages_->attachmentDirectory() : QString();
    if (directory.isEmpty() || !QDir().mkpath(directory))
        return;

    const QString target = directory + QLatin1String("/voice-")
                         + QDateTime::currentDateTimeUtc().toString(QString::fromLatin1("yyyyMMdd-HHmmss"))
                         + QLatin1String(".wav");

    QString error;
    if (!voice_->start(target, &error)) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Voice note"), error);
        return;
    }

    voiceButton_->setText(QString::fromUtf8("\342\226\240"));
    sizeHint_->setText(QString::fromLatin1("Recording... press again to send"));
}

void DmWindow::onVoiceTick(int seconds)
{
    sizeHint_->setText(QString::fromLatin1("Recording %1:%2 - press again to send")
                           .arg(seconds / 60)
                           .arg(seconds % 60, 2, 10, QLatin1Char('0')));
}

void DmWindow::onTransferChanged(const QString &conversationId, const QString &messageId)
{
    Q_UNUSED(messageId);
    if (conversationId == conversationId_)
        refreshDelivery();
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
