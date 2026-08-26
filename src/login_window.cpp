#include "login_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSizePolicy>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "avatar.h"
#include "identity_backup.h"
#include "identity_store.h"
#include "main_window.h"
#include "meeru_dialogs.h"
#include "meeru_paint.h"
#include "meeru_style.h"
#include "meeru_window.h"
#include "presence.h"

namespace {
const QColor kAvailable = Presence::color(Presence::Available);
const QColor kAbsent = Presence::color(Presence::Absent);
const QColor kDnd = Presence::color(Presence::DoNotDisturb);
const QColor kInvisible = Presence::color(Presence::Invisible);

const qreal kFrameMargin = 4.0;
const qreal kFrameRadius = 22.0;
const qreal kStrokeWidth = 5.0;

class SidePanel final : public QWidget {
public:
    explicit SidePanel(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(215);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(43, 27, 54));
        QLinearGradient glow(0, 0, width(), height());
        glow.setColorAt(0.0, QColor(83, 48, 93, 115));
        glow.setColorAt(1.0, QColor(31, 21, 38, 0));
        painter.fillRect(rect(), glow);
        painter.setPen(QPen(QColor(121, 78, 99, 120), 30));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRect(-74, 315, 292, 292));
    }
};

// Sizes are in pixels on purpose. Point sizes are resolved against the logical
// DPI of whichever screen the window happens to be on, so the same layout drew
// different text after dragging the window to a second monitor.
QLabel *makeLabel(const QString &text, QWidget *parent, int pixelSize, const QColor &color, bool bold = false) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    QFont font("Segoe UI");
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    label->setFont(font);
    label->setStyleSheet(QString("color: %1; background: transparent;").arg(color.name()));
    return label;
}

QLabel *makeImageLabel(const QString &resourcePath, const QSize &size, QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("background: transparent;");

    const QPixmap pixmap(resourcePath);
    if (!pixmap.isNull()) {
        label->setPixmap(pixmap.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    return label;
}

QPixmap makeDotIcon(const QColor &color, int diameter = 10) {
    return MeeruPaint::presenceBadge(color, 16, diameter);
}

QRectF computeFrameRect(const QSize &size) {
    return QRectF(kFrameMargin, kFrameMargin, size.width() - 2 * kFrameMargin, size.height() - 2 * kFrameMargin);
}

QRectF computeInnerRect(const QSize &size) {
    const QRectF frameRect = computeFrameRect(size);
    const qreal halfStroke = kStrokeWidth * 0.5;
    return frameRect.adjusted(halfStroke, halfStroke, -halfStroke, -halfStroke);
}

qreal computeInnerRadius() {
    const qreal halfStroke = kStrokeWidth * 0.5;
    return qMax(0.0, kFrameRadius - halfStroke);
}
}

PresenceFrame::PresenceFrame(QWidget *parent)
    : QWidget(parent), color_(kAvailable), inner_(nullptr), movie_(nullptr), animation_(nullptr) {
    setFixedSize(134, 134);
    animation_ = new QPropertyAnimation(this, "presenceColor", this);
    animation_->setDuration(400);
    animation_->setEasingCurve(QEasingCurve::InOutQuad);
}

void PresenceFrame::setInnerWidget(QWidget *widget) {
    inner_ = widget;
    inner_->setParent(this);
    if (QLabel *label = qobject_cast<QLabel *>(inner_)) {
        if (label->pixmap())
            original_ = *label->pixmap();
    }
    updateClip();
}

void PresenceFrame::setPicture(const QPixmap &pixmap) {
    clearAnimation();
    applyPicture(pixmap);
}

void PresenceFrame::applyPicture(const QPixmap &pixmap) {
    original_ = pixmap;
    updateClip();
    update();
}

void PresenceFrame::clearAnimation() {
    if (movie_) {
        movie_->stop();
        delete movie_;
        movie_ = nullptr;
    }
    crop_ = QRect();
}

void PresenceFrame::setAnimation(const QString &path, const QRect &crop) {
    clearAnimation();

    movie_ = MeeruImage::bufferedMovie(path, this);
    if (!movie_) {
        return;
    }

    crop_ = crop;
    movie_->setCacheMode(QMovie::CacheNone);
    connect(movie_, SIGNAL(frameChanged(int)), this, SLOT(onMovieFrame()));
    movie_->start();
}

void PresenceFrame::onMovieFrame() {
    if (!movie_) {
        return;
    }

    const QPixmap frame = movie_->currentPixmap();
    if (frame.isNull()) {
        return;
    }

    QRect area = crop_.isValid() ? crop_ : QRect(QPoint(0, 0), frame.size());
    area = area.intersected(QRect(QPoint(0, 0), frame.size()));
    applyPicture(area.isValid() ? frame.copy(area) : frame);
}

void PresenceFrame::animateToColor(const QColor &color) {
    animation_->stop();
    animation_->setStartValue(color_);
    animation_->setEndValue(color);
    animation_->start();
}

void PresenceFrame::setPresenceColorImmediate(const QColor &color) {
    color_ = color;
    update();
}

void PresenceFrame::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateClip();
}

void PresenceFrame::updateClip() {
    if (!inner_) {
        return;
    }

    const QRectF innerRect = computeInnerRect(size());
    const QSize targetSize = innerRect.size().toSize();
    inner_->setGeometry(innerRect.toAlignedRect());

    if (auto *label = qobject_cast<QLabel *>(inner_)) {
        const QPixmap source = original_;
        if (!source.isNull() && !targetSize.isEmpty()) {
            QPixmap scaledSource = source;
            if (source.size() != targetSize) {
                scaledSource = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            }

            QPixmap rounded(targetSize);
            rounded.fill(Qt::transparent);
            QPainter roundedPainter(&rounded);
            roundedPainter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(targetSize)), computeInnerRadius(), computeInnerRadius());
            roundedPainter.setClipPath(path);
            const QRect sourceRect((scaledSource.width() - targetSize.width()) / 2,
                                    (scaledSource.height() - targetSize.height()) / 2,
                                    targetSize.width(), targetSize.height());
            roundedPainter.drawPixmap(QRectF(QPointF(0, 0), QSizeF(targetSize)), scaledSource, QRectF(sourceRect));
            label->setPixmap(rounded);
        }
    }
}

void PresenceFrame::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    MeeruPaint::drawPresenceHalo(painter,
                                 QRectF(0, 0, width(), height()),
                                 computeFrameRect(size()),
                                 color_,
                                 kFrameRadius,
                                 kStrokeWidth);
}

LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent), root_(nullptr), displayName_(nullptr), presenceSelector_(nullptr),
      autoStart_(nullptr), createIdentity_(nullptr), importBackup_(nullptr),
      newIdentityButton_(nullptr), logOutButton_(nullptr), settingsButton_(nullptr),
      previousButton_(nullptr), nextButton_(nullptr), headline_(nullptr), subhead_(nullptr),
      cardTitle_(nullptr), cardBody_(nullptr), newIdentityHint_(nullptr), logOutHint_(nullptr),
      logoFrame_(nullptr), cardAvatar_(nullptr), cardLogo_(nullptr),
      presenceDot_(nullptr), presenceText_(nullptr), titleBar_(nullptr),
      pathsReady_(false), currentIndex_(0), creatingNew_(false) {
    buildUi();

    QString pathError;
    pathsReady_ = paths_.initialize(&pathError);
    if (!pathsReady_) {
        QMessageBox::warning(this, "Meeru Messenger",
                             "Meeru could not prepare its folder in your profile.\n\n" + pathError);
    } else {
        SettingsStore(paths_).recordLaunch();
    }

    reloadIdentities();
}

bool LoginWindow::hasStoredIdentity() const {
    return !creatingNew_ && !identities_.isEmpty();
}

LocalProfile LoginWindow::currentIdentity() const {
    if (currentIndex_ >= 0 && currentIndex_ < identities_.size())
        return identities_.at(currentIndex_);
    return LocalProfile();
}

void LoginWindow::reloadIdentities() {
    identities_.clear();
    currentIndex_ = 0;

    if (pathsReady_) {
        IdentityStore store(paths_);

        // Identities created before a Meeru ID became the public key itself
        // carry an ID that no longer matches their key, which breaks every
        // connection without saying so. Repair them before listing.
        store.migrateLegacyIdentities();

        identities_ = store.listIdentities();

        const QString active = store.activeIdentityId();
        for (int i = 0; i < identities_.size(); ++i) {
            if (identities_.at(i).identityId == active) {
                currentIndex_ = i;
                break;
            }
        }
    }

    if (identities_.isEmpty())
        creatingNew_ = false;

    showIdentity(currentIndex_);
}

void LoginWindow::showIdentity(int index) {
    const AppSettings saved = pathsReady_ ? SettingsStore(paths_).load() : AppSettings();

    if (hasStoredIdentity()) {
        currentIndex_ = qBound(0, index, identities_.size() - 1);
        const LocalProfile profile = currentIdentity();
        displayName_->setText(profile.displayName);
        const int state = Presence::stateFromKey(
            profile.identityId == saved.activeIdentityId && !saved.presence.isEmpty()
                ? saved.presence : profile.presence);
        presenceSelector_->setCurrentIndex(state);
        onPresenceChanged(state);
    } else {
        if (!creatingNew_ && !saved.displayName.isEmpty())
            displayName_->setText(saved.displayName);
        else if (creatingNew_)
            displayName_->clear();
        const int state = Presence::stateFromKey(saved.presence);
        presenceSelector_->setCurrentIndex(state);
        onPresenceChanged(state);
    }

#ifdef Q_OS_WIN
    autoStart_->setChecked(SettingsStore::autoStartEnabled() || saved.startWithWindows);
#else
    autoStart_->setChecked(saved.startWithWindows);
#endif

    applyModeTexts();
    refreshIdentityArtwork();

    displayName_->setFocus();
    displayName_->selectAll();
}

void LoginWindow::applyModeTexts() {
    const bool stored = hasStoredIdentity();
    const int count = identities_.size();

    if (stored) {
        const LocalProfile profile = currentIdentity();
        headline_->setText("Welcome back, " + profile.displayName + "!");
        subhead_->setText("Meeru seems to have detected you have one or multiple accounts already stored "
                          "in this computer, please select the account you want to use with the arrows.");
        createIdentity_->setText("Continue");
        createIdentity_->setToolTip("Local identity " + profile.shortId());
        cardTitle_->setText("Signing in as " + profile.displayName);
        cardBody_->setText("Identity " + profile.shortId()
                           + QString::fromUtf8(" \342\200\224 stored on this device only. ")
                           + "Your name and status can change at any time.");
    } else {
        headline_->setText("Start using Meeru");
        subhead_->setText("Meeru creates a private local identity on this device. No email\n"
                          "address, username, or password is required.");
        createIdentity_->setText("Create local identity");
        createIdentity_->setToolTip(QString());
        cardTitle_->setText("Your identity stays on this device");
        cardBody_->setText("It can be backed up or moved later. Your name and status can change "
                           "at any time.");
    }

    const bool showArrows = stored && count > 1;
    previousButton_->setVisible(showArrows);
    nextButton_->setVisible(showArrows);

    const bool returning = creatingNew_ && count > 0;
    newIdentityButton_->setVisible(stored || returning);
    newIdentityHint_->setVisible(stored || returning);
    logOutButton_->setVisible(stored);
    logOutHint_->setVisible(stored);

    if (returning) {
        newIdentityButton_->setText("Back to my identities");
        newIdentityHint_->setText("Go back to the identities already stored here.");
    } else {
        newIdentityButton_->setText("Create new Identity");
        newIdentityHint_->setText("Keeps the identities already stored here and starts an extra one.");
    }

    if (stored && count > 1) {
        previousButton_->setToolTip("Previous identity (" + QString::number(currentIndex_ + 1)
                                    + " of " + QString::number(count) + ")");
        nextButton_->setToolTip(previousButton_->toolTip());
    }
}

void LoginWindow::refreshIdentityArtwork() {
    if (hasStoredIdentity()) {
        const LocalProfile profile = currentIdentity();
        ImageStore avatar(paths_, profile.identityId, QStringLiteral("avatar"));

        cardLogo_->hide();
        cardAvatar_->show();
        cardAvatar_->setInitials(MeeruPaint::initialsFor(profile.displayName));
        cardAvatar_->setImage(avatar);
        cardAvatar_->setPresenceColor(Presence::color(presenceSelector_->currentIndex()), false);

        if (avatar.hasImage() && avatar.isAnimated()) {
            // Animated avatars play in the big frame too, not just as a still
            // first frame.
            logoFrame_->setAnimation(avatar.filePath(), avatar.cropRect());
            return;
        }

        QPixmap picture;
        if (avatar.hasImage()) {
            QImageReader reader(avatar.filePath());
            reader.setAutoTransform(true);
            const QImage frame = reader.read();
            if (!frame.isNull())
                picture = QPixmap::fromImage(frame);
        }
        if (picture.isNull()) {
            picture = MeeruPaint::initialsTile(MeeruPaint::initialsFor(profile.displayName),
                                               QSize(256, 256), 46);
        }
        logoFrame_->setPicture(picture);
        return;
    }

    cardAvatar_->hide();
    cardLogo_->show();
    logoFrame_->setPicture(QPixmap(QStringLiteral(":/assets/Meeru Full.jpg")));
}

void LoginWindow::onPreviousIdentity() {
    if (identities_.size() < 2)
        return;
    showIdentity((currentIndex_ + identities_.size() - 1) % identities_.size());
}

void LoginWindow::onNextIdentity() {
    if (identities_.size() < 2)
        return;
    showIdentity((currentIndex_ + 1) % identities_.size());
}

void LoginWindow::onPresenceChanged(int index) {
    QColor color = kAvailable;
    QString text = "Online — ready to create a local Meeru identity.";
    switch (index) {
    case 0: color = kAvailable; text = "Online — ready to create a local Meeru identity."; break;
    case 1: color = kAbsent; text = "Away — ready to create a local Meeru identity."; break;
    case 2: color = kDnd; text = "Do Not Disturb — ready to create a local Meeru identity."; break;
    case 3: color = kInvisible; text = "Offline — ready to create a local Meeru identity."; break;
    default: break;
    }

    if (logoFrame_) {
        logoFrame_->animateToColor(color);
    }
    if (cardAvatar_) {
        cardAvatar_->setPresenceColor(color, true);
    }
    if (presenceDot_) {
        presenceDot_->setPixmap(MeeruPaint::presenceBadge(color, 12, 9));
    }
    if (presenceText_) {
        presenceText_->setText(text);
    }
}

void LoginWindow::buildUi() {
    setWindowTitle("Meeru Messenger");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(684, 664);
    resize(684, 676);

    root_ = new QWidget(this);
    root_->setObjectName("root");
    setCentralWidget(root_);

    auto *windowLayout = new QVBoxLayout(root_);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);

    titleBar_ = new MeeruTitleBar("Meeru Messenger", true, true, root_);
    windowLayout->addWidget(titleBar_);

    auto *body = new QWidget(root_);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *side = new SidePanel(body);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(14, 42, 14, 22);
    sideLayout->setSpacing(0);

    // The artwork sits between two arrows that swap stored identities.
    auto *logoRow = new QHBoxLayout();
    logoRow->setSpacing(4);

    previousButton_ = new QPushButton(QString::fromUtf8("\342\200\271"), side);
    previousButton_->setObjectName("carouselButton");
    previousButton_->setFixedSize(26, 46);
    previousButton_->setFocusPolicy(Qt::NoFocus);
    previousButton_->setCursor(Qt::PointingHandCursor);

    nextButton_ = new QPushButton(QString::fromUtf8("\342\200\272"), side);
    nextButton_->setObjectName("carouselButton");
    nextButton_->setFixedSize(26, 46);
    nextButton_->setFocusPolicy(Qt::NoFocus);
    nextButton_->setCursor(Qt::PointingHandCursor);

    logoFrame_ = new PresenceFrame(side);
    auto *logoImage = makeImageLabel(QStringLiteral(":/assets/Meeru Full.jpg"), QSize(126, 126), logoFrame_);
    logoFrame_->setInnerWidget(logoImage);

    logoRow->addStretch();
    logoRow->addWidget(previousButton_, 0, Qt::AlignVCenter);
    logoRow->addWidget(logoFrame_);
    logoRow->addWidget(nextButton_, 0, Qt::AlignVCenter);
    logoRow->addStretch();
    sideLayout->addLayout(logoRow);
    sideLayout->addSpacing(24);

    auto *sideTitle = makeLabel("Meeru\nMessenger", side, 19, QColor(255, 255, 255), true);
    sideTitle->setAlignment(Qt::AlignCenter);
    sideLayout->addWidget(sideTitle);
    sideLayout->addSpacing(10);

    auto *sideCopy = makeLabel("Private messaging without\naccounts, passwords, or a\ncentral identity provider.", side, 12, QColor(222, 207, 229));
    sideCopy->setAlignment(Qt::AlignCenter);
    sideLayout->addWidget(sideCopy);
    sideLayout->addStretch();
    bodyLayout->addWidget(side);

    auto *main = new QWidget(body);
    main->setObjectName("mainPanel");
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(42, 34, 42, 16);
    mainLayout->setSpacing(0);

    headline_ = makeLabel("Start using Meeru", main, 28, QColor(255, 255, 255), true);
    mainLayout->addWidget(headline_);
    mainLayout->addSpacing(11);
    subhead_ = makeLabel("Meeru creates a private local identity on this device. No email\naddress, username, or password is required.", main, 13, QColor(214, 197, 224));
    mainLayout->addWidget(subhead_);
    mainLayout->addSpacing(20);

    auto *identityCard = new QFrame(main);
    identityCard->setObjectName("identityCard");
    // A minimum, not a fixed height: the copy inside changes with the mode and
    // with the length of the display name, and a fixed box simply clipped it.
    identityCard->setMinimumHeight(76);
    identityCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *cardLayout = new QHBoxLayout(identityCard);
    cardLayout->setContentsMargins(12, 11, 12, 11);
    cardLayout->setSpacing(11);

    cardLogo_ = makeImageLabel(QStringLiteral(":/assets/Meeru Trans.png"), QSize(40, 40), identityCard);
    cardLogo_->setFixedSize(40, 40);
    cardLayout->addWidget(cardLogo_);

    // Same widget the main window uses, so the small logo becomes a live
    // replica of the profile picture with its own presence halo.
    cardAvatar_ = new AvatarFrame(identityCard);
    cardAvatar_->setTileSize(40);
    cardAvatar_->hide();
    cardLayout->addWidget(cardAvatar_);

    auto *cardCopy = new QWidget(identityCard);
    auto *cardCopyLayout = new QVBoxLayout(cardCopy);
    cardCopyLayout->setContentsMargins(0, 0, 0, 0);
    cardCopyLayout->setSpacing(2);
    cardTitle_ = makeLabel("Your identity stays on this device", cardCopy, 13, QColor(255, 255, 255), true);
    cardBody_ = makeLabel("It can be backed up or moved later. Your name and status can change "
                          "at any time.", cardCopy, 11, QColor(221, 206, 229));
    cardCopyLayout->addWidget(cardTitle_);
    cardCopyLayout->addWidget(cardBody_);
    cardLayout->addWidget(cardCopy);
    mainLayout->addWidget(identityCard);
    mainLayout->addSpacing(16);

    mainLayout->addWidget(makeLabel("Display name", main, 12, QColor(221, 206, 229), true));
    mainLayout->addSpacing(6);
    displayName_ = new QLineEdit("Mathery", main);
    displayName_->setFixedHeight(34);
    mainLayout->addWidget(displayName_);
    mainLayout->addSpacing(12);

    mainLayout->addWidget(makeLabel("Start as", main, 12, QColor(221, 206, 229), true));
    mainLayout->addSpacing(6);
    presenceSelector_ = new QComboBox(main);
    presenceSelector_->addItem(makeDotIcon(kAvailable), "Available");
    presenceSelector_->addItem(makeDotIcon(kAbsent), "Absent");
    presenceSelector_->addItem(makeDotIcon(kDnd), "Do Not Disturb");
    presenceSelector_->addItem(makeDotIcon(kInvisible), "Invisible");
    presenceSelector_->setFixedHeight(34);
    presenceSelector_->setIconSize(QSize(16, 16));
    mainLayout->addWidget(presenceSelector_);
    mainLayout->addSpacing(11);

    connect(presenceSelector_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &LoginWindow::onPresenceChanged);

    autoStart_ = new QCheckBox("Start Meeru automatically when Windows starts", main);
    autoStart_->setChecked(true);
    mainLayout->addWidget(autoStart_);
    mainLayout->addSpacing(14);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    createIdentity_ = new QPushButton("Create local identity", main);
    createIdentity_->setObjectName("primaryButton");
    createIdentity_->setFixedHeight(32);
    importBackup_ = new QPushButton("Import backup", main);
    importBackup_->setObjectName("secondaryButton");
    importBackup_->setFixedHeight(32);
    buttonRow->addWidget(createIdentity_);
    buttonRow->addWidget(importBackup_);
    buttonRow->addStretch();
    mainLayout->addLayout(buttonRow);
    mainLayout->addSpacing(12);

    // Managing the identities already on this computer.
    newIdentityButton_ = new QPushButton("Create new Identity", main);
    newIdentityButton_->setObjectName("secondaryButton");
    newIdentityButton_->setFixedHeight(28);
    newIdentityHint_ = makeLabel("Keeps the identities already stored here and starts an extra one.",
                                 main, 9, QColor(186, 170, 196));

    logOutButton_ = new QPushButton("Log out", main);
    logOutButton_->setObjectName("dangerButton");
    logOutButton_->setFixedHeight(28);
    logOutHint_ = makeLabel("Erases this identity from this computer. Without a backup, forever.",
                            main, 9, QColor(224, 160, 170));

    auto *manageRow = new QHBoxLayout();
    manageRow->setSpacing(8);

    auto *newColumn = new QVBoxLayout();
    newColumn->setSpacing(3);
    newColumn->addWidget(newIdentityButton_);
    newColumn->addWidget(newIdentityHint_);

    auto *logOutColumn = new QVBoxLayout();
    logOutColumn->setSpacing(3);
    logOutColumn->addWidget(logOutButton_);
    logOutColumn->addWidget(logOutHint_);

    manageRow->addLayout(newColumn, 1);
    manageRow->addLayout(logOutColumn, 1);
    mainLayout->addLayout(manageRow);
    mainLayout->addSpacing(14);

    auto *presenceRow = new QHBoxLayout();
    presenceRow->setSpacing(7);
    presenceDot_ = new QLabel(main);
    presenceDot_->setFixedSize(12, 16);
    presenceDot_->setAlignment(Qt::AlignCenter);
    presenceDot_->setPixmap(MeeruPaint::presenceBadge(kAvailable, 12, 9));
    presenceText_ = makeLabel(QString::fromUtf8("Online \342\200\224 ready to create a local Meeru identity."), main, 11, QColor(216, 201, 224));
    presenceRow->addWidget(presenceDot_);
    presenceRow->addWidget(presenceText_);
    presenceRow->addStretch();
    mainLayout->addLayout(presenceRow);
    mainLayout->addStretch();

    bodyLayout->addWidget(main, 1);
    windowLayout->addWidget(body, 1);

    auto *bottomBar = new QWidget(root_);
    bottomBar->setObjectName("bottomBar");
    bottomBar->setFixedHeight(50);
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(16, 6, 11, 6);
    bottomLayout->setSpacing(10);
    bottomLayout->addWidget(makeLabel("Meeru will never collect any personal information.", bottomBar, 11, QColor(215, 199, 223)));
    bottomLayout->addStretch();
    bottomLayout->addWidget(makeLabel("(C) MTA Mathery Automation 2014-2026\nAll Rights Reserved", bottomBar, 11, QColor(215, 199, 223)));
    settingsButton_ = new QPushButton(QString::fromUtf8("\342\232\231"), bottomBar);
    settingsButton_->setObjectName("settingsButton");
    settingsButton_->setToolTip("Settings");
    settingsButton_->setFixedSize(24, 24);
    bottomLayout->addWidget(settingsButton_);
    windowLayout->addWidget(bottomBar);

    root_->setStyleSheet(
        "#root { background: #211727; border: 1px solid #8e6aa0; }"
        "#titleBar { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "            stop:0 #72518A, stop:1 #4E3562); border-bottom: 1px solid #634A70; }"
        "#titleBarText { font: bold 11px 'Segoe UI'; color: #FFF7FC; background: transparent; }"
        "#mainPanel { background: #211727; border-left: 1px solid #755681; }"
        "#identityCard { background: #17121c; border: 1px solid #73567e; border-radius: 5px; }"
        "QLineEdit, QComboBox { color: #ffffff; background: #17121c; border: 1px solid #73567e; border-radius: 5px; padding: 0 10px; font: 13px 'Segoe UI'; }"
        "QLineEdit:focus, QComboBox:focus { border-color: #e48c9f; }"
        "QComboBox::drop-down { width: 25px; border: 0; }"
        "QComboBox::drop-down:hover { background: rgba(255,255,255,0.06); }"
        "QComboBox QAbstractItemView { color: #ffffff; background: #211727; border: 1px solid #73567e;"
        "                              outline: 0; padding: 4px; }"
        "QComboBox QAbstractItemView::item { min-height: 26px; padding: 3px 8px; border-radius: 4px; }"
        "QComboBox QAbstractItemView::item:selected { background: #4a3454; color: #ffffff; }"
        "QCheckBox { color: #d8c9df; font: 11px 'Segoe UI'; spacing: 7px; background: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid #9b708e; background: #17121c; }"
        "QCheckBox::indicator:checked { background: #ed8e9d; border-color: #ed8e9d; image: none; }"
        "QPushButton { border-radius: 4px; padding: 0 15px; font: bold 12px 'Segoe UI'; }"
        "#primaryButton { color: #40263c; background: #ef91a1; border: 1px solid #f4a4b1; }"
        "#primaryButton:hover { background: #f4a2b0; }"
        "#secondaryButton { color: #ffffff; background: #3a2843; border: 1px solid #755681; }"
        "#secondaryButton:hover { background: #48334f; }"
        "#dangerButton { color: #ffd9de; background: #4a2630; border: 1px solid #8d4a58; }"
        "#dangerButton:hover { background: #5d2f3b; }"
        "#carouselButton { color: #e6d3ee; background: rgba(255,255,255,0.07);"
        "                  border: 1px solid rgba(255,255,255,0.16); border-radius: 5px;"
        "                  font: bold 20px 'Segoe UI'; padding: 0; }"
        "#carouselButton:hover { background: rgba(255,255,255,0.16); color: #ffffff; }"
        "#carouselButton:pressed { background: rgba(0,0,0,0.20); }"
        "#bottomBar { background: #1d1523; border-top: 1px solid #755681; }"
        "#settingsButton { color: #dbc8df; background: transparent; border: 0; padding: 0; font: 19px 'Segoe UI Symbol'; }"
        "#settingsButton:hover { color: #ffffff; background: #3a2843; }"
    );

    connect(createIdentity_, &QPushButton::clicked, this, &LoginWindow::onContinue);
    connect(importBackup_, &QPushButton::clicked, this, &LoginWindow::onImportBackup);
    connect(newIdentityButton_, &QPushButton::clicked, this, &LoginWindow::onCreateNewIdentity);
    connect(logOutButton_, &QPushButton::clicked, this, &LoginWindow::onLogOut);
    connect(previousButton_, &QPushButton::clicked, this, &LoginWindow::onPreviousIdentity);
    connect(nextButton_, &QPushButton::clicked, this, &LoginWindow::onNextIdentity);
    connect(displayName_, &QLineEdit::returnPressed, this, &LoginWindow::onContinue);
}

void LoginWindow::setBusy(bool busy) {
    createIdentity_->setEnabled(!busy);
    importBackup_->setEnabled(!busy);
    newIdentityButton_->setEnabled(!busy);
    logOutButton_->setEnabled(!busy);
    previousButton_->setEnabled(!busy);
    nextButton_->setEnabled(!busy);
    displayName_->setEnabled(!busy);
    presenceSelector_->setEnabled(!busy);
    autoStart_->setEnabled(!busy);

    if (busy) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
    QApplication::processEvents();
}

void LoginWindow::onContinue() {
    const QString name = displayName_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Meeru Messenger", "Please choose a display name first.");
        displayName_->setFocus();
        return;
    }

    if (!pathsReady_) {
        QString pathError;
        pathsReady_ = paths_.initialize(&pathError);
        if (!pathsReady_) {
            QMessageBox::critical(this, "Meeru Messenger",
                                  "Meeru cannot write to your application data folder.\n\n" + pathError);
            return;
        }
    }

    setBusy(true);

    IdentityStore store(paths_);
    const QString presenceKey = Presence::key(presenceSelector_->currentIndex());
    const bool reusing = hasStoredIdentity();

    LocalProfile profile;
    QString error;
    bool ok = false;

    if (reusing) {
        // Make the identity shown in the carousel the active one, then apply
        // any change to its name or presence.
        const LocalProfile chosen = currentIdentity();
        ok = store.activate(chosen.identityId, &profile, &error)
             && store.updateActive(name, presenceKey, &profile, &error);
    } else {
        ok = store.create(name, presenceKey, &profile, &error);
    }

    if (!ok && reusing) {
        setBusy(false);
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, "Meeru Messenger",
            "Meeru found this identity on this computer but could not unlock it.\n\n" + error
            + "\n\nCreate a new local identity instead? The existing ones will be kept on disk.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        setBusy(true);
        error.clear();
        ok = store.create(name, presenceKey, &profile, &error);
    }

    if (!ok) {
        setBusy(false);
        QMessageBox::critical(this, "Meeru Messenger",
                              "Meeru could not prepare your local identity.\n\n" + error);
        return;
    }

    SettingsStore settings(paths_);
    AppSettings values = settings.load();
    values.activeIdentityId = profile.identityId;
    values.displayName = profile.displayName;
    values.presence = profile.presence;
    values.startWithWindows = autoStart_->isChecked();
    settings.save(values, 0);

    const bool created = !reusing;

    UsageData usage = settings.loadUsage();
    if (created)
        usage.identitiesCreated += 1;
    usage.sessionsStarted += 1;
    settings.saveUsage(usage, 0);

    QString autoStartError;
    const bool autoStartOk = SettingsStore::setAutoStart(autoStart_->isChecked(), &autoStartError);

    setBusy(false);

    if (!autoStartOk) {
        QMessageBox::warning(this, "Meeru Messenger",
                             "Your identity was saved, but Meeru could not change the Windows startup setting.\n\n"
                             + autoStartError);
    }

    if (created && !store.lastVaultWasDeviceProtected()) {
        QMessageBox::warning(this, "Meeru Messenger",
                             "Your identity was created, but Windows data protection was unavailable on this "
                             "system, so the private keys are stored without an extra layer of protection.\n\n"
                             "Keep the Meeru folder in your user profile private.");
    }

    openMainWindow(profile);
}

void LoginWindow::onCreateNewIdentity() {
    if (creatingNew_) {
        creatingNew_ = false;
        showIdentity(currentIndex_);
        return;
    }

    if (!MeeruDialog::confirm(
            this, "Create new Identity",
            "The identities already stored on this computer stay exactly where they are. "
            "Nothing is replaced or deleted.\n\n"
            "This only starts an additional identity with its own keys, contacts and conversations. "
            "You can move between all of them with the arrows next to the picture.",
            "Create new Identity")) {
        return;
    }

    creatingNew_ = true;
    displayName_->clear();
    presenceSelector_->setCurrentIndex(Presence::Available);
    onPresenceChanged(Presence::Available);
    applyModeTexts();
    refreshIdentityArtwork();
    displayName_->setFocus();
}

bool LoginWindow::exportBackupFor(const LocalProfile &profile) {
    PassphraseDialog passphrase(
        true,
        "Your backup file is protected with a passphrase, not with Windows. That is what lets you "
        "restore this identity on another computer.\n\n"
        "Meeru derives the key with Argon2id, so a slow, deliberate passphrase is worth it.",
        this);
    if (passphrase.exec() != QDialog::Accepted)
        return false;

    const QString suggested = QDir::homePath() + "/" + profile.displayName.simplified().replace(' ', '-')
                              + "-" + profile.shortId() + ".meeruid";
    const QString target = QFileDialog::getSaveFileName(
        this, "Save your Meeru backup", suggested, "Meeru identity backup (*.meeruid)");
    if (target.isEmpty())
        return false;

    setBusy(true);
    QString error;
    const bool ok = IdentityBackup::exportIdentity(paths_, profile.identityId,
                                                   passphrase.passphrase(), target, &error);
    setBusy(false);

    if (!ok) {
        MeeruDialog::showMessage(this, "Backup", "Meeru could not write the backup.\n\n" + error);
        return false;
    }

    MeeruDialog::showMessage(
        this, "Backup",
        "Your backup was saved.\n\nThat single file plus the passphrase is everything needed to sign in "
        "as this identity from another copy of Meeru, on this computer or any other. Keep both apart from "
        "each other, and remember Meeru cannot recover either one for you.");
    return true;
}

void LoginWindow::onLogOut() {
    if (!hasStoredIdentity())
        return;

    const LocalProfile profile = currentIdentity();
    LogOutDialog dialog(profile.displayName, IdentityBackup::wasExported(paths_, profile.identityId), this);
    dialog.exec();

    if (dialog.choice() == LogOutDialog::Backup) {
        exportBackupFor(profile);
        return;
    }
    if (dialog.choice() != LogOutDialog::LogOut)
        return;

    if (!MeeruDialog::confirm(
            this, "Log out",
            "Last chance. Meeru is about to erase the identity \"" + profile.displayName
            + "\" and its keys from this computer. If you have no backup file, it can never be "
              "recovered by anyone.",
            "Erase it")) {
        return;
    }

    setBusy(true);

    // Nothing of this identity may still be open when its folder is erased.
    logoFrame_->clearAnimation();
    cardAvatar_->clearImage();

    IdentityStore store(paths_);
    QString error;
    const bool ok = store.deleteIdentity(profile.identityId, &error);
    setBusy(false);

    if (!ok) {
        MeeruDialog::showMessage(this, "Log out", "Meeru could not erase that identity.\n\n" + error);
        return;
    }

    SettingsStore settings(paths_);
    AppSettings values = settings.load();
    if (values.activeIdentityId == profile.identityId) {
        values.activeIdentityId.clear();
        values.displayName.clear();
        values.statusText.clear();
        settings.save(values, 0);
    }

    creatingNew_ = false;
    reloadIdentities();
    MeeruDialog::showMessage(this, "Log out",
                             "That identity is no longer on this computer.");
}

void LoginWindow::onImportBackup() {
    if (!pathsReady_) {
        QString pathError;
        pathsReady_ = paths_.initialize(&pathError);
        if (!pathsReady_) {
            QMessageBox::critical(this, "Meeru Messenger",
                                  "Meeru cannot write to your application data folder.\n\n" + pathError);
            return;
        }
    }

    const QString source = QFileDialog::getOpenFileName(
        this, "Open a Meeru backup", QDir::homePath(),
        "Meeru identity backup (*.meeruid);;All files (*)");
    if (source.isEmpty())
        return;

    PassphraseDialog passphrase(false, "Enter the passphrase that protects this backup file.", this);
    if (passphrase.exec() != QDialog::Accepted)
        return;

    setBusy(true);
    LocalProfile restored;
    QString error;
    const bool ok = IdentityBackup::importIdentity(paths_, source, passphrase.passphrase(), &restored, &error);
    setBusy(false);

    if (!ok) {
        MeeruDialog::showMessage(this, "Import backup", "Meeru could not restore that backup.\n\n" + error);
        return;
    }

    creatingNew_ = false;
    reloadIdentities();
    MeeruDialog::showMessage(this, "Import backup",
                             "Welcome back, " + restored.displayName
                             + ". This identity is ready on this computer, and its keys have been "
                               "re-protected for your Windows account.");
}

void LoginWindow::openMainWindow(const LocalProfile &profile) {
    MainWindow *window = new MainWindow(profile, paths_);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowIcon(windowIcon());
    window->show();
    close();
}
