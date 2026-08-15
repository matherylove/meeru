#include "contact_card.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "meeru_paint.h"
#include "meeru_style.h"
#include "presence.h"

namespace {
const int kCardWidth = 288;
const int kBannerHeight = 96;
}

ContactCard::ContactCard(QWidget *parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint),
      banner_(0), avatar_(0), nameLabel_(0), stateLabel_(0),
      presenceDot_(0), statusLabel_(0), footerLabel_(0)
{
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(MeeruStyle::sheet());
    setFixedWidth(kCardWidth);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *root = new QWidget(this);
    root->setObjectName(QString::fromLatin1("dialogRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(root);

    QVBoxLayout *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    banner_ = new BannerFrame(root);
    banner_->setFixedHeight(kBannerHeight);
    banner_->setCursor(Qt::ArrowCursor);
    banner_->setToolTip(QString());

    avatar_ = new AvatarFrame(banner_);
    avatar_->setTileSize(52);
    const int inset = avatar_->pictureInset();

    QHBoxLayout *bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(12 - inset, 0, 12, 0);
    bannerLayout->setSpacing(qMax(0, 10 - inset));
    bannerLayout->addWidget(avatar_, 0, Qt::AlignVCenter);

    QWidget *column = new QWidget(banner_);
    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);

    nameLabel_ = new QLabel(column);
    nameLabel_->setObjectName(QString::fromLatin1("profileName"));
    nameLabel_->setFixedHeight(22);

    presenceDot_ = new QLabel(column);
    presenceDot_->setFixedSize(10, 15);
    presenceDot_->setAlignment(Qt::AlignCenter);

    stateLabel_ = new QLabel(column);
    stateLabel_->setObjectName(QString::fromLatin1("profileState"));
    stateLabel_->setFixedHeight(15);

    QHBoxLayout *stateRow = new QHBoxLayout();
    stateRow->setContentsMargins(0, 0, 0, 0);
    stateRow->setSpacing(6);
    stateRow->addWidget(presenceDot_);
    stateRow->addWidget(stateLabel_, 1);

    statusLabel_ = new QLabel(column);
    statusLabel_->setObjectName(QString::fromLatin1("personalMessage"));
    statusLabel_->setFixedHeight(20);
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    columnLayout->addWidget(nameLabel_);
    columnLayout->addSpacing(4);
    columnLayout->addLayout(stateRow);
    columnLayout->addSpacing(10);
    columnLayout->addWidget(statusLabel_);

    bannerLayout->addWidget(column, 1, Qt::AlignVCenter);
    rootLayout->addWidget(banner_);

    footerLabel_ = new QLabel(root);
    footerLabel_->setObjectName(QString::fromLatin1("footerText"));
    footerLabel_->setContentsMargins(12, 6, 12, 7);
    footerLabel_->setWordWrap(true);
    rootLayout->addWidget(footerLabel_);
}

void ContactCard::showFor(const Roster::Contact &contact,
                          const MeeruPaths &paths,
                          const QString &ownerId,
                          bool online,
                          const QPoint &globalPosition)
{
    const QString directory = ImageStore::peerDirectory(paths, ownerId, contact.id);
    ImageStore avatarStore(directory, QString::fromLatin1("avatar"));
    ImageStore bannerStore(directory, QString::fromLatin1("banner"));

    const int state = online ? Presence::stateFromKey(contact.presence) : Presence::Invisible;

    avatar_->setInitials(MeeruPaint::initialsFor(contact.bestName()));
    avatar_->setImage(avatarStore);
    avatar_->setPresenceColor(Presence::color(state), false);
    banner_->setImage(bannerStore);

    nameLabel_->setText(contact.bestName());
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(Presence::color(state), 10, 9));

    QString stateText;
    switch (contact.state) {
    case Roster::ContactPendingOutgoing: stateText = QString::fromLatin1("Waiting for them to accept"); break;
    case Roster::ContactPendingIncoming: stateText = QString::fromLatin1("Wants to add you"); break;
    case Roster::ContactBlocked:         stateText = QString::fromLatin1("Blocked"); break;
    default:
        stateText = online ? Presence::label(state) : QString::fromLatin1("Offline");
        break;
    }
    stateLabel_->setText(stateText);

    statusLabel_->setText(contact.statusText.trimmed().isEmpty()
                              ? QString::fromLatin1("No personal message")
                              : contact.statusText.trimmed());

    QString footer = QString::fromLatin1("meeru:") + contact.id.left(24) + QString::fromLatin1("...");
    if (contact.lastSeenUtc.isValid() && !online) {
        footer += QString::fromLatin1("\nLast seen ")
                + contact.lastSeenUtc.toLocalTime().toString(QString::fromLatin1("d MMM, h:mm AP"));
    }
    footerLabel_->setText(footer);

    adjustSize();

    // Keep the card on screen whichever edge the pointer is near.
    QPoint target = globalPosition + QPoint(16, 12);
    const QRect available = QApplication::desktop()->availableGeometry(globalPosition);
    if (target.x() + width() > available.right())
        target.setX(globalPosition.x() - width() - 12);
    if (target.y() + height() > available.bottom())
        target.setY(available.bottom() - height() - 4);
    if (target.y() < available.top())
        target.setY(available.top() + 4);
    move(target);

    show();
    raise();
}

void ContactCard::hideCard()
{
    hide();
    avatar_->clearImage();
    banner_->clearImage();
}
