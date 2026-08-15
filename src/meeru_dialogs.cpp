#include "meeru_dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegExp>
#include <QStringList>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "crop_dialog.h"
#include "invite_code.h"
#include "meeru_paint.h"
#include "meeru_style.h"
#include "presence.h"

namespace {

QLabel *fieldLabel(const QString &text, QWidget *parent = 0)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1("sectionLabel"));
    return label;
}

QLabel *bodyLabel(const QString &text, QWidget *parent = 0)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1("dialogLabel"));
    label->setWordWrap(true);
    return label;
}

QLineEdit *makeEdit(const QString &placeholder, QWidget *parent = 0)
{
    QLineEdit *edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    edit->setFixedHeight(30);
    return edit;
}

}

// --------------------------------------------------------------- PictureDialog

PictureDialog::PictureDialog(ImageStore *store, const QString &initials, const QColor &presence, QWidget *parent)
    : MeeruDialog(store && store->kind() == QLatin1String("banner")
                      ? QString::fromLatin1("Header banner")
                      : QString::fromLatin1("Profile picture"), parent),
      store_(store), initials_(initials), presence_(presence), changed_(false),
      avatarPreview_(0), bannerPreview_(0), caption_(0), removeButton_(0)
{
    setDialogWidth(340);

    if (isBanner()) {
        bannerPreview_ = new BannerFrame(this);
        bannerPreview_->setFixedSize(300, 100);
        bannerPreview_->setCursor(Qt::ArrowCursor);
        bannerPreview_->setToolTip(QString());
        contentLayout()->addWidget(bannerPreview_, 0, Qt::AlignHCenter);
    } else {
        avatarPreview_ = new AvatarFrame(this);
        avatarPreview_->setTileSize(104);
        avatarPreview_->setInitials(initials_);
        avatarPreview_->setPresenceColor(presence_, false);
        contentLayout()->addWidget(avatarPreview_, 0, Qt::AlignHCenter);
    }

    caption_ = bodyLabel(QString(), this);
    caption_->setAlignment(Qt::AlignCenter);
    contentLayout()->addWidget(caption_);

    const QString what = isBanner() ? QString::fromLatin1("Banner") : QString::fromLatin1("Profile Picture");

    QPushButton *change = new QPushButton(QString::fromLatin1("Change ") + what, this);
    change->setObjectName(QString::fromLatin1("primaryButton"));
    change->setFixedHeight(32);

    removeButton_ = new QPushButton(QString::fromLatin1("Remove ") + what, this);
    removeButton_->setFixedHeight(32);

    QPushButton *close = new QPushButton(QString::fromLatin1("Close"), this);
    close->setFixedHeight(32);

    contentLayout()->addWidget(change);
    contentLayout()->addWidget(removeButton_);
    contentLayout()->addWidget(close);

    connect(change, SIGNAL(clicked()), this, SLOT(onChange()));
    connect(removeButton_, SIGNAL(clicked()), this, SLOT(onRemove()));
    connect(close, SIGNAL(clicked()), this, SLOT(accept()));

    refresh();
}

bool PictureDialog::isBanner() const
{
    return store_ && store_->kind() == QLatin1String("banner");
}

void PictureDialog::refresh()
{
    if (avatarPreview_) {
        avatarPreview_->setInitials(initials_);
        avatarPreview_->setImage(*store_);
    }
    if (bannerPreview_)
        bannerPreview_->setImage(*store_);

    removeButton_->setEnabled(store_->hasImage());

    if (store_->hasImage()) {
        caption_->setText(store_->isAnimated()
            ? QString::fromLatin1("An animation is being used. Every frame is cropped as it plays.")
            : QString::fromLatin1("Stored inside your Meeru identity folder."));
    } else if (isBanner()) {
        caption_->setText(QString::fromLatin1("No banner yet, so Meeru paints its own gradient."));
    } else {
        caption_->setText(QString::fromLatin1("No picture yet, so Meeru shows your initials."));
    }
}

void PictureDialog::onChange()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QString::fromLatin1("Choose a picture or animation"),
        QDir::homePath(),
        QString::fromLatin1("Pictures and animations (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"));
    if (path.isEmpty())
        return;

    const QString title = isBanner() ? QString::fromLatin1("Crop your banner")
                                     : QString::fromLatin1("Crop your picture");
    CropDialog crop(path, store_->aspect(), title, this);
    if (!crop.isReady()) {
        crop.exec();
        return;
    }
    if (crop.exec() != QDialog::Accepted)
        return;

    QString error;
    bool ok = false;
    if (crop.isAnimated())
        ok = store_->saveAnimated(path, crop.cropRect(), &error);
    else
        ok = store_->saveStill(crop.croppedStill(), &error);

    if (!ok) {
        MeeruDialog::showMessage(this, windowTitle(),
                                 QString::fromLatin1("Meeru could not save that picture.\n\n") + error);
        return;
    }

    changed_ = true;
    refresh();
}

void PictureDialog::onRemove()
{
    const QString what = isBanner() ? QString::fromLatin1("banner") : QString::fromLatin1("profile picture");
    if (!MeeruDialog::confirm(this, windowTitle(),
                              QString::fromLatin1("Remove your ") + what + QString::fromLatin1("?"),
                              QString::fromLatin1("Remove"))) {
        return;
    }

    QString error;
    if (!store_->removeImage(&error)) {
        MeeruDialog::showMessage(this, windowTitle(),
                                 QString::fromLatin1("Meeru could not remove it.\n\n") + error);
        return;
    }
    changed_ = true;
    refresh();
}

// ------------------------------------------------------------ PassphraseDialog

PassphraseDialog::PassphraseDialog(bool confirming, const QString &message, QWidget *parent)
    : MeeruDialog(QString::fromLatin1("Backup passphrase"), parent),
      confirming_(confirming), first_(0), second_(0), feedback_(0), acceptButton_(0)
{
    setDialogWidth(400);

    contentLayout()->addWidget(bodyLabel(message, this));

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("PASSPHRASE"), this));
    first_ = makeEdit(QString::fromLatin1("At least 8 characters"), this);
    first_->setEchoMode(QLineEdit::Password);
    contentLayout()->addWidget(first_);

    if (confirming_) {
        contentLayout()->addWidget(fieldLabel(QString::fromLatin1("REPEAT PASSPHRASE"), this));
        second_ = makeEdit(QString::fromLatin1("Type it again"), this);
        second_->setEchoMode(QLineEdit::Password);
        contentLayout()->addWidget(second_);
    }

    feedback_ = new QLabel(this);
    feedback_->setObjectName(QString::fromLatin1("dialogHint"));
    feedback_->setWordWrap(true);
    contentLayout()->addWidget(feedback_);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    acceptButton_ = new QPushButton(QString::fromLatin1("Continue"), this);
    acceptButton_->setObjectName(QString::fromLatin1("primaryButton"));
    acceptButton_->setEnabled(false);
    acceptButton_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(acceptButton_);
    contentLayout()->addLayout(buttons);

    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(acceptButton_, SIGNAL(clicked()), this, SLOT(accept()));
    connect(first_, SIGNAL(textChanged(QString)), this, SLOT(validate()));
    if (second_)
        connect(second_, SIGNAL(textChanged(QString)), this, SLOT(validate()));
    validate();
}

void PassphraseDialog::validate()
{
    const QString value = first_->text();
    if (value.size() < 8) {
        feedback_->setText(value.isEmpty()
            ? QString::fromLatin1("Meeru cannot recover this passphrase for you. Write it down.")
            : QString::fromLatin1("Use at least 8 characters."));
        acceptButton_->setEnabled(false);
        return;
    }
    if (confirming_ && second_ && second_->text() != value) {
        feedback_->setText(QString::fromLatin1("The two passphrases do not match yet."));
        acceptButton_->setEnabled(false);
        return;
    }
    feedback_->setText(QString::fromLatin1("Meeru cannot recover this passphrase for you. Write it down."));
    acceptButton_->setEnabled(true);
}

QString PassphraseDialog::passphrase() const
{
    return first_->text();
}

// --------------------------------------------------------------- LogOutDialog

LogOutDialog::LogOutDialog(const QString &displayName, bool backedUp, QWidget *parent)
    : MeeruDialog(QString::fromLatin1("Log out"), parent), choice_(Cancelled)
{
    setDialogWidth(430);

    QLabel *warning = bodyLabel(
        QString::fromLatin1("Logging out of \"") + displayName
        + QString::fromLatin1("\" does not just close a session.\n\n"
                              "Meeru has no server holding your account. Everything that proves this identity is "
                              "yours lives only on this computer, so logging out erases it: the keys, the profile, "
                              "the contacts and the conversations.\n\n"
                              "If you have no backup file, this identity becomes unreachable forever. Nobody, "
                              "including us, can bring it back. Only continue if that is what you want."), this);
    contentLayout()->addWidget(warning);

    QLabel *state = new QLabel(this);
    state->setObjectName(QString::fromLatin1("dialogHint"));
    state->setWordWrap(true);
    state->setText(backedUp
        ? QString::fromLatin1("You exported a backup of this identity from this computer at least once.")
        : QString::fromLatin1("Meeru has no record of you ever exporting a backup of this identity."));
    contentLayout()->addWidget(state);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *backup = new QPushButton(QString::fromLatin1("Backup"), this);
    backup->setObjectName(QString::fromLatin1("primaryButton"));
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    QPushButton *logOut = new QPushButton(QString::fromLatin1("Log Out"), this);
    buttons->addWidget(backup);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(logOut);
    contentLayout()->addLayout(buttons);

    connect(backup, SIGNAL(clicked()), this, SLOT(chooseBackup()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(logOut, SIGNAL(clicked()), this, SLOT(chooseLogOut()));
}

void LogOutDialog::chooseLogOut()
{
    choice_ = LogOut;
    accept();
}

void LogOutDialog::chooseBackup()
{
    choice_ = Backup;
    accept();
}

// ------------------------------------------------------------ AddContactDialog

AddContactDialog::AddContactDialog(const LocalProfile &profile,
                                   const IdentityMaterial &material,
                                   const QStringList &localEndpoints,
                                   qint64 inviteLifetime,
                                   QWidget *parent)
    : MeeruDialog(QString::fromLatin1("Add a contact"), parent),
      profile_(profile), material_(material), localEndpoints_(localEndpoints),
      idEdit_(0), nameEdit_(0), addressEdit_(0), lifetimeBox_(0),
      lifetimeWarning_(0), feedback_(0), acceptButton_(0)
{
    setDialogWidth(430);

    contentLayout()->addWidget(bodyLabel(QString::fromLatin1(
        "Paste what your friend sent you. An invite code carries their addresses as well as their "
        "identity, so it reaches them anywhere; a bare Meeru ID only works on your own network or "
        "with an address you fill in below."), this));

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("INVITE CODE OR MEERU ID"), this));
    idEdit_ = makeEdit(QString::fromLatin1("meeru-invite:... or the plain 64 character ID"), this);
    contentLayout()->addWidget(idEdit_);

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("NAME FOR YOUR LIST (OPTIONAL)"), this));
    nameEdit_ = makeEdit(QString::fromLatin1("How you want to see them"), this);
    contentLayout()->addWidget(nameEdit_);

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("ADDRESS (ONLY FOR A BARE ID FROM OUTSIDE YOUR NETWORK)"), this));
    addressEdit_ = makeEdit(QString::fromLatin1("192.168.1.20:47441 or host.example:47441"), this);
    contentLayout()->addWidget(addressEdit_);

    feedback_ = new QLabel(this);
    feedback_->setObjectName(QString::fromLatin1("dialogHint"));
    feedback_->setWordWrap(true);
    contentLayout()->addWidget(feedback_);

    // --- your own code, and how long it should stay usable
    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("YOUR OWN INVITE CODE"), this));

    QHBoxLayout *ownRow = new QHBoxLayout();
    ownRow->setSpacing(8);
    lifetimeBox_ = new QComboBox(this);
    lifetimeBox_->setFixedHeight(30);

    const QList<qint64> lifetimes = Invite::offeredLifetimes();
    int selected = 0;
    for (int i = 0; i < lifetimes.size(); ++i) {
        lifetimeBox_->addItem(Invite::lifetimeLabel(lifetimes.at(i)),
                              static_cast<double>(lifetimes.at(i)));
        if (lifetimes.at(i) == inviteLifetime)
            selected = i;
    }
    lifetimeBox_->setCurrentIndex(selected);

    QPushButton *copy = new QPushButton(QString::fromLatin1("Copy my invite code"), this);
    copy->setObjectName(QString::fromLatin1("primaryButton"));
    ownRow->addWidget(lifetimeBox_, 1);
    ownRow->addWidget(copy);
    contentLayout()->addLayout(ownRow);

    lifetimeWarning_ = new QLabel(this);
    lifetimeWarning_->setObjectName(QString::fromLatin1("dialogHint"));
    lifetimeWarning_->setWordWrap(true);
    contentLayout()->addWidget(lifetimeWarning_);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    acceptButton_ = new QPushButton(QString::fromLatin1("Send request"), this);
    acceptButton_->setObjectName(QString::fromLatin1("primaryButton"));
    acceptButton_->setEnabled(false);
    acceptButton_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(acceptButton_);
    contentLayout()->addLayout(buttons);

    connect(copy, SIGNAL(clicked()), this, SLOT(onCopyOwnCode()));
    connect(lifetimeBox_, SIGNAL(currentIndexChanged(int)), this, SLOT(onLifetimeChanged(int)));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(acceptButton_, SIGNAL(clicked()), this, SLOT(accept()));
    connect(idEdit_, SIGNAL(textChanged(QString)), this, SLOT(validate()));

    onLifetimeChanged(lifetimeBox_->currentIndex());
    validate();
}

qint64 AddContactDialog::inviteLifetime() const
{
    return static_cast<qint64>(lifetimeBox_->itemData(lifetimeBox_->currentIndex()).toDouble());
}

void AddContactDialog::onLifetimeChanged(int index)
{
    Q_UNUSED(index);
    lifetimeWarning_->setText(Invite::lifetimeWarning(inviteLifetime()));
}

void AddContactDialog::onCopyOwnCode()
{
    if (localEndpoints_.isEmpty()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Invite code"),
                                 QString::fromLatin1("Meeru does not know any address for this device yet. "
                                                     "Give it a moment to look at your network, then try again."));
        return;
    }

    const QString code = Invite::build(profile_, material_, localEndpoints_, inviteLifetime());
    if (code.isEmpty()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Invite code"),
                                 QString::fromLatin1("Meeru could not sign an invite code with this identity."));
        return;
    }

    QApplication::clipboard()->setText(code);

    QString message = QString::fromLatin1("Your invite code was copied. Send it however you like: "
                                          "it is signed, so nobody can alter the addresses inside it.\n\n");
    if (inviteLifetime() <= 0) {
        message += QString::fromLatin1("It never expires, so it will keep working until this device "
                                       "changes address.");
    } else {
        message += QString::fromLatin1("It stops working in ") + Invite::lifetimeLabel(inviteLifetime())
                 + QString::fromLatin1(", after which you can copy a fresh one.");
    }
    MeeruDialog::showMessage(this, QString::fromLatin1("Invite code"), message);
}

void AddContactDialog::validate()
{
    pastedId_.clear();
    pastedEndpoints_.clear();

    const QString raw = idEdit_->text().trimmed();
    if (raw.isEmpty()) {
        feedback_->setText(QString());
        acceptButton_->setEnabled(false);
        return;
    }

    if (Invite::looksLikeCode(raw)) {
        Invite::Card card;
        QString error;
        if (!Invite::decode(raw, &card, &error)) {
            feedback_->setText(error);
            acceptButton_->setEnabled(false);
            return;
        }
        if (card.identityId == profile_.identityId) {
            feedback_->setText(QString::fromLatin1("That is your own invite code."));
            acceptButton_->setEnabled(false);
            return;
        }

        pastedId_ = card.identityId;
        pastedEndpoints_ = card.endpoints;
        if (nameEdit_->text().trimmed().isEmpty() && !card.displayName.trimmed().isEmpty())
            nameEdit_->setText(card.displayName.trimmed());

        // An expired code is still worth trying: the identity in it is real and
        // the addresses may not have moved. The user just deserves to know.
        if (card.isExpired()) {
            feedback_->setText(QString::fromLatin1(
                "This code expired, so the addresses in it may be stale. Meeru will still try them, "
                "and will find them anyway if you share a network."));
        } else if (card.neverExpires()) {
            feedback_->setText(QString::fromLatin1("Valid code from %1, with no expiry.")
                                   .arg(card.displayName.isEmpty()
                                            ? card.identityId.left(12)
                                            : card.displayName));
        } else {
            feedback_->setText(QString::fromLatin1("Valid code from %1, good until %2.")
                                   .arg(card.displayName.isEmpty()
                                            ? card.identityId.left(12)
                                            : card.displayName)
                                   .arg(card.expiresAtUtc().toLocalTime()
                                            .toString(QString::fromLatin1("d MMM, h:mm AP"))));
        }
        acceptButton_->setEnabled(true);
        return;
    }

    const QString normalised = Roster::normaliseIdentityId(raw);
    if (normalised.isEmpty()) {
        feedback_->setText(QString::fromLatin1(
            "A Meeru ID is 64 hexadecimal characters, and an invite code starts with meeru-invite:."));
        acceptButton_->setEnabled(false);
        return;
    }
    if (normalised == profile_.identityId) {
        feedback_->setText(QString::fromLatin1("That is your own ID."));
        acceptButton_->setEnabled(false);
        return;
    }

    pastedId_ = normalised;
    feedback_->setText(QString::fromLatin1(
        "Valid ID. Meeru will find them on your own network; from further away it needs the address "
        "below, or an invite code instead."));
    acceptButton_->setEnabled(true);
}

QString AddContactDialog::contactId() const
{
    return pastedId_;
}

QString AddContactDialog::contactName() const
{
    return nameEdit_->text().trimmed();
}

QString AddContactDialog::endpointHint() const
{
    QStringList hints = pastedEndpoints_;
    const QString typed = addressEdit_->text().trimmed();
    if (!typed.isEmpty() && !hints.contains(typed))
        hints.append(typed);
    return hints.join(QString::fromLatin1(","));
}

// ------------------------------------------------------- NewConversationDialog

NewConversationDialog::NewConversationDialog(const QList<Roster::Contact> &contacts, QWidget *parent)
    : MeeruDialog(QString::fromLatin1("New conversation"), parent),
      list_(0), titleEdit_(0), summary_(0), acceptButton_(0)
{
    setDialogWidth(380);

    contentLayout()->addWidget(bodyLabel(QString::fromLatin1(
        "Pick one contact for a direct conversation, or several to open a group."), this));

    list_ = new QListWidget(this);
    list_->setFixedHeight(190);
    list_->setStyleSheet(QString::fromLatin1(
        "QListWidget { background: #19121f; border: 1px solid #634A70; border-radius: 6px; padding: 4px; }"
        "QListWidget::item { padding: 6px; border-radius: 4px; }"
        "QListWidget::item:hover { background: #2f2139; }"));

    for (int i = 0; i < contacts.size(); ++i) {
        const Roster::Contact &contact = contacts.at(i);
        QListWidgetItem *item = new QListWidgetItem(contact.bestName(), list_);
        item->setData(Qt::UserRole, contact.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setIcon(QIcon(MeeruPaint::initialsTile(MeeruPaint::initialsFor(contact.bestName()), QSize(22, 22), 5)));
    }
    contentLayout()->addWidget(list_);

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("GROUP NAME (OPTIONAL)"), this));
    titleEdit_ = makeEdit(QString::fromLatin1("Named later if you leave this empty"), this);
    titleEdit_->setEnabled(false);
    contentLayout()->addWidget(titleEdit_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QString::fromLatin1("dialogHint"));
    summary_->setWordWrap(true);
    contentLayout()->addWidget(summary_);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    acceptButton_ = new QPushButton(QString::fromLatin1("Create"), this);
    acceptButton_->setObjectName(QString::fromLatin1("primaryButton"));
    acceptButton_->setEnabled(false);
    acceptButton_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(acceptButton_);
    contentLayout()->addLayout(buttons);

    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(acceptButton_, SIGNAL(clicked()), this, SLOT(accept()));
    connect(list_, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(onSelectionChanged()));

    if (contacts.isEmpty()) {
        summary_->setText(QString::fromLatin1(
            "You have no accepted contacts yet. Add someone from the Contacts tab first."));
    }
    onSelectionChanged();
}

void NewConversationDialog::onSelectionChanged()
{
    selected_.clear();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem *item = list_->item(i);
        if (item->checkState() == Qt::Checked)
            selected_.append(item->data(Qt::UserRole).toString());
    }

    const bool group = selected_.size() > 1;
    titleEdit_->setEnabled(group);
    acceptButton_->setEnabled(!selected_.isEmpty());

    if (selected_.isEmpty()) {
        if (list_->count() > 0)
            summary_->setText(QString::fromLatin1("Nobody selected yet."));
    } else if (group) {
        summary_->setText(QString::fromLatin1("A group conversation with %1 people plus you.")
                              .arg(selected_.size()));
    } else {
        summary_->setText(QString::fromLatin1("A direct conversation with one contact."));
    }
}

QString NewConversationDialog::groupTitle() const
{
    return selected_.size() > 1 ? titleEdit_->text().trimmed() : QString();
}

// ---------------------------------------------------------------- ServerDialog

ServerDialog::ServerDialog(QWidget *parent)
    : MeeruDialog(QString::fromLatin1("Servers"), parent),
      pages_(0), nameEdit_(0), topicEdit_(0), inviteEdit_(0), joinFeedback_(0), joining_(false)
{
    setDialogWidth(380);

    pages_ = new QStackedWidget(this);
    contentLayout()->addWidget(pages_);

    // Page 0: choose what to do.
    QWidget *choice = new QWidget(pages_);
    QVBoxLayout *choiceLayout = new QVBoxLayout(choice);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    choiceLayout->setSpacing(10);
    choiceLayout->addWidget(bodyLabel(QString::fromLatin1(
        "A server is a shared space with its own channels and members."), choice));

    QPushButton *create = new QPushButton(QString::fromLatin1("Create a new server"), choice);
    create->setObjectName(QString::fromLatin1("primaryButton"));
    create->setFixedHeight(36);
    QPushButton *join = new QPushButton(QString::fromLatin1("Join an existing server"), choice);
    join->setFixedHeight(36);
    choiceLayout->addWidget(create);
    choiceLayout->addWidget(join);
    choiceLayout->addStretch();
    pages_->addWidget(choice);

    // Page 1: create.
    QWidget *createPage = new QWidget(pages_);
    QVBoxLayout *createLayout = new QVBoxLayout(createPage);
    createLayout->setContentsMargins(0, 0, 0, 0);
    createLayout->setSpacing(6);
    createLayout->addWidget(fieldLabel(QString::fromLatin1("SERVER NAME"), createPage));
    nameEdit_ = makeEdit(QString::fromLatin1("Meeru Test Server"), createPage);
    createLayout->addWidget(nameEdit_);
    createLayout->addSpacing(6);
    createLayout->addWidget(fieldLabel(QString::fromLatin1("TOPIC (OPTIONAL)"), createPage));
    topicEdit_ = makeEdit(QString::fromLatin1("Friday Night Games"), createPage);
    createLayout->addWidget(topicEdit_);
    createLayout->addSpacing(8);
    createLayout->addWidget(bodyLabel(QString::fromLatin1(
        "You become the owner. Meeru generates an invite code you can share."), createPage));
    createLayout->addStretch();

    QHBoxLayout *createButtons = new QHBoxLayout();
    createButtons->addStretch();
    QPushButton *createCancel = new QPushButton(QString::fromLatin1("Cancel"), createPage);
    QPushButton *createAccept = new QPushButton(QString::fromLatin1("Create server"), createPage);
    createAccept->setObjectName(QString::fromLatin1("primaryButton"));
    createButtons->addWidget(createCancel);
    createButtons->addWidget(createAccept);
    createLayout->addLayout(createButtons);
    pages_->addWidget(createPage);

    // Page 2: join.
    QWidget *joinPage = new QWidget(pages_);
    QVBoxLayout *joinLayout = new QVBoxLayout(joinPage);
    joinLayout->setContentsMargins(0, 0, 0, 0);
    joinLayout->setSpacing(6);
    joinLayout->addWidget(fieldLabel(QString::fromLatin1("INVITE CODE"), joinPage));
    inviteEdit_ = makeEdit(QString::fromLatin1("meeru:server:0a1b2c..."), joinPage);
    joinLayout->addWidget(inviteEdit_);
    joinFeedback_ = new QLabel(joinPage);
    joinFeedback_->setObjectName(QString::fromLatin1("dialogHint"));
    joinFeedback_->setWordWrap(true);
    joinLayout->addWidget(joinFeedback_);
    joinLayout->addWidget(bodyLabel(QString::fromLatin1(
        "The server stays marked as waiting until the invite is confirmed."), joinPage));
    joinLayout->addStretch();

    QHBoxLayout *joinButtons = new QHBoxLayout();
    joinButtons->addStretch();
    QPushButton *joinCancel = new QPushButton(QString::fromLatin1("Cancel"), joinPage);
    QPushButton *joinAccept = new QPushButton(QString::fromLatin1("Join server"), joinPage);
    joinAccept->setObjectName(QString::fromLatin1("primaryButton"));
    joinButtons->addWidget(joinCancel);
    joinButtons->addWidget(joinAccept);
    joinLayout->addLayout(joinButtons);
    pages_->addWidget(joinPage);

    connect(create, SIGNAL(clicked()), this, SLOT(chooseCreate()));
    connect(join, SIGNAL(clicked()), this, SLOT(chooseJoin()));
    connect(createCancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(joinCancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(createAccept, SIGNAL(clicked()), this, SLOT(submitCreate()));
    connect(joinAccept, SIGNAL(clicked()), this, SLOT(submitJoin()));

    showPage(0);
}

void ServerDialog::showPage(int index)
{
    pages_->setCurrentIndex(index);
    pages_->setFixedHeight(index == 0 ? 170 : 230);
    adjustSize();
}

void ServerDialog::chooseCreate()
{
    joining_ = false;
    showPage(1);
    nameEdit_->setFocus();
}

void ServerDialog::chooseJoin()
{
    joining_ = true;
    showPage(2);
    inviteEdit_->setFocus();
}

void ServerDialog::submitCreate()
{
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        MeeruDialog::showMessage(this, QString::fromLatin1("Create server"),
                                 QString::fromLatin1("Please give the server a name."));
        nameEdit_->setFocus();
        return;
    }

    server_ = Roster::Server();
    server_.id = Roster::newLocalId();
    server_.name = name;
    server_.topic = topicEdit_->text().trimmed();
    server_.owner = true;
    server_.state = Roster::ServerJoined;
    server_.joinedAtUtc = QDateTime::currentDateTimeUtc();
    joining_ = false;
    accept();
}

void ServerDialog::submitJoin()
{
    QString code = inviteEdit_->text().trimmed().toLower();
    if (code.startsWith(QLatin1String("meeru:server:")))
        code = code.mid(13);
    code.remove(QLatin1Char('/'));
    code = code.trimmed();

    bool valid = code.size() == 32 || code.size() == 64;
    for (int i = 0; valid && i < code.size(); ++i) {
        const QChar character = code.at(i);
        const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool lower = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        if (!digit && !lower)
            valid = false;
    }

    if (!valid) {
        joinFeedback_->setText(QString::fromLatin1(
            "An invite code is 32 or 64 hexadecimal characters, with or without the meeru:server: prefix."));
        inviteEdit_->setFocus();
        return;
    }

    server_ = Roster::Server();
    server_.id = code;
    server_.name = QString::fromLatin1("Server ") + code.left(8);
    server_.topic = QString::fromLatin1("Waiting for the invite to be confirmed");
    server_.owner = false;
    server_.state = Roster::ServerPending;
    server_.joinedAtUtc = QDateTime::currentDateTimeUtc();
    joining_ = true;
    accept();
}

// -------------------------------------------------------------- SettingsDialog

SettingsDialog::SettingsDialog(const QString &displayName,
                               const QString &identityId,
                               const QString &folder,
                               bool startWithWindows,
                               const QStringList &rendezvousHosts,
                               const QString &reachability,
                               bool useUpnp,
                               int listenPort,
                               const QString &publicAddress,
                               QWidget *parent)
    : MeeruDialog(QString::fromLatin1("Settings"), parent),
      identityId_(identityId), folder_(folder), nameEdit_(0), startupBox_(0),
      rendezvousEdit_(0), upnpBox_(0), portEdit_(0), publicEdit_(0)
{
    setDialogWidth(400);

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("DISPLAY NAME"), this));
    nameEdit_ = makeEdit(QString::fromLatin1("Your name"), this);
    nameEdit_->setText(displayName);
    contentLayout()->addWidget(nameEdit_);
    contentLayout()->addWidget(bodyLabel(QString::fromLatin1(
        "Changing this re-signs your local profile with your identity key."), this));

    startupBox_ = new QCheckBox(QString::fromLatin1("Start Meeru automatically when Windows starts"), this);
    startupBox_->setChecked(startWithWindows);
    contentLayout()->addWidget(startupBox_);

    // --- how this device is reached from outside
    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("HOW PEOPLE REACH YOU"), this));
    contentLayout()->addWidget(bodyLabel(QString::fromLatin1("Status: ")
                                         + (reachability.isEmpty()
                                                ? QString::fromLatin1("unknown")
                                                : reachability), this));

    upnpBox_ = new QCheckBox(QString::fromLatin1("Let Meeru ask the router to open a port (UPnP)"), this);
    upnpBox_->setChecked(useUpnp);
    contentLayout()->addWidget(upnpBox_);

    QHBoxLayout *portRow = new QHBoxLayout();
    portRow->setSpacing(8);

    QWidget *portColumn = new QWidget(this);
    QVBoxLayout *portColumnLayout = new QVBoxLayout(portColumn);
    portColumnLayout->setContentsMargins(0, 0, 0, 0);
    portColumnLayout->setSpacing(4);
    portColumnLayout->addWidget(fieldLabel(QString::fromLatin1("PORT TO LISTEN ON"), portColumn));
    portEdit_ = makeEdit(QString::fromLatin1("Automatic"), portColumn);
    if (listenPort > 0)
        portEdit_->setText(QString::number(listenPort));
    portColumnLayout->addWidget(portEdit_);

    QWidget *publicColumn = new QWidget(this);
    QVBoxLayout *publicColumnLayout = new QVBoxLayout(publicColumn);
    publicColumnLayout->setContentsMargins(0, 0, 0, 0);
    publicColumnLayout->setSpacing(4);
    publicColumnLayout->addWidget(fieldLabel(QString::fromLatin1("YOUR PUBLIC ADDRESS"), publicColumn));
    publicEdit_ = makeEdit(QString::fromLatin1("203.0.113.9:47441"), publicColumn);
    publicEdit_->setText(publicAddress);
    publicColumnLayout->addWidget(publicEdit_);

    portRow->addWidget(portColumn, 1);
    portRow->addWidget(publicColumn, 1);
    contentLayout()->addLayout(portRow);

    contentLayout()->addWidget(bodyLabel(QString::fromLatin1(
        "If your router does not answer UPnP, forward a port to this computer yourself, put the same "
        "number on the left, and on the right the address people outside would use. Meeru then puts "
        "that address into your invite codes and contacts connect straight to you.\n\n"
        "A port change takes effect the next time Meeru starts."), this));

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("RENDEZVOUS NODES"), this));
    rendezvousEdit_ = makeEdit(QString::fromLatin1("meeru.example.org:47450, 203.0.113.9"), this);
    rendezvousEdit_->setText(rendezvousHosts.join(QString::fromLatin1(", ")));
    contentLayout()->addWidget(rendezvousEdit_);
    contentLayout()->addWidget(bodyLabel(QString::fromLatin1(
        "A last resort for when neither side can be reached directly, which happens when both "
        "connections sit behind a provider's own NAT. A node introduces the two of you and carries "
        "the sealed traffic; it cannot read anything passing through.\n\n"
        "Host your own with MeeruServer on any machine with a public address, and put it here."), this));

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("YOUR MEERU ID"), this));
    QHBoxLayout *idRow = new QHBoxLayout();
    QLabel *idLabel = new QLabel(identityId_.left(32) + QString::fromLatin1("..."), this);
    idLabel->setObjectName(QString::fromLatin1("dialogHint"));
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *copy = new QPushButton(QString::fromLatin1("Copy"), this);
    idRow->addWidget(idLabel, 1);
    idRow->addWidget(copy);
    contentLayout()->addLayout(idRow);

    contentLayout()->addWidget(fieldLabel(QString::fromLatin1("DATA FOLDER"), this));
    QHBoxLayout *folderRow = new QHBoxLayout();
    QLabel *folderLabel = new QLabel(folder_, this);
    folderLabel->setObjectName(QString::fromLatin1("dialogHint"));
    folderLabel->setWordWrap(true);
    QPushButton *open = new QPushButton(QString::fromLatin1("Open"), this);
    folderRow->addWidget(folderLabel, 1);
    folderRow->addWidget(open);
    contentLayout()->addLayout(folderRow);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(QString::fromLatin1("Cancel"), this);
    QPushButton *save = new QPushButton(QString::fromLatin1("Save"), this);
    save->setObjectName(QString::fromLatin1("primaryButton"));
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    contentLayout()->addLayout(buttons);

    connect(copy, SIGNAL(clicked()), this, SLOT(onCopyId()));
    connect(open, SIGNAL(clicked()), this, SLOT(onOpenFolder()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(save, SIGNAL(clicked()), this, SLOT(accept()));
}

QString SettingsDialog::displayName() const
{
    return nameEdit_->text().trimmed();
}

bool SettingsDialog::startWithWindows() const
{
    return startupBox_->isChecked();
}

bool SettingsDialog::useUpnp() const
{
    return upnpBox_->isChecked();
}

int SettingsDialog::listenPort() const
{
    bool ok = false;
    const int value = portEdit_->text().trimmed().toInt(&ok);
    if (!ok || value <= 0 || value > 65535)
        return 0;
    return value;
}

QString SettingsDialog::publicAddress() const
{
    return publicEdit_->text().trimmed();
}

QStringList SettingsDialog::rendezvousHosts() const
{
    QStringList hosts;
    const QStringList parts = rendezvousEdit_->text().split(QRegExp(QString::fromLatin1("[,;\\s]+")),
                                                            QString::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString host = parts.at(i).trimmed();
        if (!host.isEmpty())
            hosts.append(host);
    }
    return hosts;
}

void SettingsDialog::onCopyId()
{
    QApplication::clipboard()->setText(QString::fromLatin1("meeru:") + identityId_);
}

void SettingsDialog::onOpenFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_));
}
